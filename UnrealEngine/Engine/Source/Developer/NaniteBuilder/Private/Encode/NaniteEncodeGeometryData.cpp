// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeGeometryData.h"

#include "Math/UnrealMath.h"
#include "Async/ParallelFor.h"

#include "Cluster.h"
#include "ClusterDAG.h"
#include "NaniteDefinitions.h"
#include "NaniteEncodeShared.h"
#include "NaniteEncodeMaterial.h"
#include "NaniteEncodeSkinning.h"
#include "NaniteEncodeVertReuseBatch.h"

namespace Nanite
{

static float DecodeUVFloat(uint32 EncodedValue, uint32 NumMantissaBits)
{
	const uint32 ExponentAndMantissaMask	= (1u << (NANITE_UV_FLOAT_NUM_EXPONENT_BITS + NumMantissaBits)) - 1u;
	const bool bNeg							= (EncodedValue <= ExponentAndMantissaMask);
	const uint32 ExponentAndMantissa		= (bNeg ? ~EncodedValue : EncodedValue) & ExponentAndMantissaMask;

	const uint32 FloatBits	= 0x3F000000u + (ExponentAndMantissa << (23 - NumMantissaBits));
	float Result			= (float&)FloatBits;
	Result					= FMath::Min(Result * 2.0f - 1.0f, Result);		// Stretch denormals from [0.5,1.0] to [0.0,1.0]

	return bNeg ? -Result : Result;
}

static void VerifyUVFloatEncoding(float Value, uint32 EncodedValue, uint32 NumMantissaBits)
{
	check(FMath::IsFinite(Value));	// NaN and Inf should have been handled already

	const uint32 NumValues = 1u << (1 + NumMantissaBits + NANITE_UV_FLOAT_NUM_EXPONENT_BITS);
	
	const float DecodedValue = DecodeUVFloat(EncodedValue, NumMantissaBits);
	const float Error = FMath::Abs(DecodedValue - Value);

	// Verify that none of the neighbor code points are closer to the original float value.
	if (EncodedValue > 0u)
	{
		const float PrevValue = DecodeUVFloat(EncodedValue - 1u, NumMantissaBits);
		check(FMath::Abs(PrevValue - Value) >= Error);
	}

	if (EncodedValue + 1u < NumValues)
	{
		const float NextValue = DecodeUVFloat(EncodedValue + 1u, NumMantissaBits);
		check(FMath::Abs(NextValue - Value) >= Error);
	}
}

static uint32 EncodeUVFloat(float Value, uint32 NumMantissaBits)
{
	// Encode UV floats as a custom float type where [0,1] is denormal, so it gets uniform precision.
	// As UVs are encoded in clusters as ranges of encoded values, a few modifications to the usual
	// float encoding are made to preserve the original float order when the encoded values are interpreted as uints:
	// 1. Positive values use 1 as sign bit.
	// 2. Negative values use 0 as sign bit and have their exponent and mantissa bits inverted.

	checkSlow(FMath::IsFinite(Value));

	const uint32 SignBitPosition = NANITE_UV_FLOAT_NUM_EXPONENT_BITS + NumMantissaBits;
	const uint32 FloatUInt = (uint32&)Value;
	const uint32 Exponent = (FloatUInt >> 23) & 0xFFu;
	const uint32 Mantissa = FloatUInt & 0x7FFFFFu;
	const uint32 AbsFloatUInt = FloatUInt & 0x7FFFFFFFu;

	uint32 Result;
	if (AbsFloatUInt < 0x3F800000u)
	{
		// Denormal encoding
		// Note: Mantissa can overflow into first non-denormal value (1.0f),
		// but that is desirable to get correct round-to-nearest behavior.
		const float AbsFloat = (float&)AbsFloatUInt;
		Result = uint32(double(AbsFloat * float(1u << NumMantissaBits)) + 0.5);	// Cast to double to make sure +0.5 is lossless
	}
	else
	{
		// Normal encoding
		// Extract exponent and mantissa bits from 32-bit float-
		const uint32 Shift = (23 - NumMantissaBits);
		const uint32 Tmp = (AbsFloatUInt - 0x3F000000u) + (1u << (Shift - 1));	// Bias to round to nearest
		Result = FMath::Min(Tmp >> Shift, (1u << SignBitPosition) - 1u);		// Clamp to largest UV float value
	}

	// Produce a mask that for positive values only flips the sign bit
	// and for negative values only flips the exponent and mantissa bits.
	const uint32 SignMask = (1u << SignBitPosition) - (FloatUInt >> 31u);
	Result ^= SignMask;

#if DO_GUARD_SLOW
	VerifyUVFloatEncoding(Value, Result, NumMantissaBits);
#endif
	return Result;
}

static int32 ShortestWrap(int32 Value, uint32 NumBits)
{
	if (NumBits == 0)
	{
		check(Value == 0);
		return 0;
	}
	const int32 Shift = 32 - NumBits;
	const int32 NumValues = (1 << NumBits);
	const int32 MinValue = -(NumValues >> 1);
	const int32 MaxValue = (NumValues >> 1) - 1;

	Value = (Value << Shift) >> Shift;
	check(Value >= MinValue && Value <= MaxValue);
	return Value;
}

static uint32 EncodeZigZag(int32 X)
{
	return uint32((X << 1) ^ (X >> 31));
}

static int32 DecodeZigZag(uint32 X)
{
	return int32(X >> 1) ^ -int32(X & 1);
}


FORCEINLINE static FVector2f OctahedronEncode(FVector3f N)
{
	FVector3f AbsN = N.GetAbs();
	N /= (AbsN.X + AbsN.Y + AbsN.Z);

	if (N.Z < 0.0)
	{
		AbsN = N.GetAbs();
		N.X = (N.X >= 0.0f) ? (1.0f - AbsN.Y) : (AbsN.Y - 1.0f);
		N.Y = (N.Y >= 0.0f) ? (1.0f - AbsN.X) : (AbsN.X - 1.0f);
	}
	
	return FVector2f(N.X, N.Y);
}

FORCEINLINE static void OctahedronEncode(FVector3f N, int32& X, int32& Y, int32 QuantizationBits)
{
	const int32 QuantizationMaxValue = (1 << QuantizationBits) - 1;
	const float Scale = 0.5f * (float)QuantizationMaxValue;
	const float Bias = 0.5f * (float)QuantizationMaxValue + 0.5f;

	FVector2f Coord = OctahedronEncode(N);

	X = FMath::Clamp(int32(Coord.X * Scale + Bias), 0, QuantizationMaxValue);
	Y = FMath::Clamp(int32(Coord.Y * Scale + Bias), 0, QuantizationMaxValue);
}

FORCEINLINE static FVector3f OctahedronDecode(int32 X, int32 Y, int32 QuantizationBits)
{
	const int32 QuantizationMaxValue = (1 << QuantizationBits) - 1;
	float fx = (float)X * (2.0f / (float)QuantizationMaxValue) - 1.0f;
	float fy = (float)Y * (2.0f / (float)QuantizationMaxValue) - 1.0f;
	float fz = 1.0f - FMath::Abs(fx) - FMath::Abs(fy);
	float t = FMath::Clamp(-fz, 0.0f, 1.0f);
	fx += (fx >= 0.0f ? -t : t);
	fy += (fy >= 0.0f ? -t : t);

	return FVector3f(fx, fy, fz).GetUnsafeNormal();
}

FORCEINLINE static void OctahedronEncodePreciseSIMD( FVector3f N, int32& X, int32& Y, int32 QuantizationBits )
{
	const int32 QuantizationMaxValue = ( 1 << QuantizationBits ) - 1;
	FVector2f ScalarCoord = OctahedronEncode( N );

	const VectorRegister4f Scale = VectorSetFloat1( 0.5f * (float)QuantizationMaxValue );
	const VectorRegister4f RcpScale = VectorSetFloat1( 2.0f / (float)QuantizationMaxValue );
	VectorRegister4Int IntCoord = VectorFloatToInt( VectorMultiplyAdd( MakeVectorRegister( ScalarCoord.X, ScalarCoord.Y, ScalarCoord.X, ScalarCoord.Y ), Scale, Scale ) );	// x0, y0, x1, y1
	IntCoord = VectorIntAdd( IntCoord, MakeVectorRegisterInt( 0, 0, 1, 1 ) );
	VectorRegister4f Coord = VectorMultiplyAdd( VectorIntToFloat( IntCoord ), RcpScale, GlobalVectorConstants::FloatMinusOne );	// Coord = Coord * 2.0f / QuantizationMaxValue - 1.0f

	VectorRegister4f Nx = VectorSwizzle( Coord, 0, 2, 0, 2 );
	VectorRegister4f Ny = VectorSwizzle( Coord, 1, 1, 3, 3 );
	VectorRegister4f Nz = VectorSubtract( VectorSubtract( VectorOneFloat(), VectorAbs( Nx ) ), VectorAbs( Ny ) );			// Nz = 1.0f - abs(Nx) - abs(Ny)

	VectorRegister4f T = VectorMin( Nz, VectorZeroFloat() );	// T = min(Nz, 0.0f)
	
	VectorRegister4f NxSign = VectorBitwiseAnd( Nx, GlobalVectorConstants::SignBit() );
	VectorRegister4f NySign = VectorBitwiseAnd( Ny, GlobalVectorConstants::SignBit() );

	Nx = VectorAdd(Nx, VectorBitwiseXor( T, NxSign ) );	// Nx += T ^ NxSign
	Ny = VectorAdd(Ny, VectorBitwiseXor( T, NySign ) );	// Ny += T ^ NySign
	
	VectorRegister4f Dots = VectorMultiplyAdd(Nx, VectorSetFloat1(N.X), VectorMultiplyAdd(Ny, VectorSetFloat1(N.Y), VectorMultiply(Nz, VectorSetFloat1(N.Z))));
	VectorRegister4f Lengths = VectorSqrt(VectorMultiplyAdd(Nx, Nx, VectorMultiplyAdd(Ny, Ny, VectorMultiply(Nz, Nz))));
	Dots = VectorDivide(Dots, Lengths);

	VectorRegister4f Mask = MakeVectorRegister( 0xFFFFFFFCu, 0xFFFFFFFCu, 0xFFFFFFFCu, 0xFFFFFFFCu );
	VectorRegister4f LaneIndices = MakeVectorRegister( 0u, 1u, 2u, 3u );
	Dots = VectorBitwiseOr( VectorBitwiseAnd( Dots, Mask ), LaneIndices );
	
	// Calculate max component
	VectorRegister4f MaxDot = VectorMax( Dots, VectorSwizzle( Dots, 2, 3, 0, 1 ) );
	MaxDot = VectorMax( MaxDot, VectorSwizzle( MaxDot, 1, 2, 3, 0 ) );

	float fIndex = VectorGetComponent( MaxDot, 0 );
	uint32 Index = *(uint32*)&fIndex;
	
	uint32 IntCoordValues[ 4 ];
	VectorIntStore( IntCoord, IntCoordValues );
	X = FMath::Clamp((int32)(IntCoordValues[0] + ( Index & 1 )), 0, QuantizationMaxValue);
	Y = FMath::Clamp((int32)(IntCoordValues[1] + ( ( Index >> 1 ) & 1 )), 0, QuantizationMaxValue);
}

FORCEINLINE static void OctahedronEncodePrecise(FVector3f N, int32& X, int32& Y, int32 QuantizationBits)
{
	const int32 QuantizationMaxValue = (1 << QuantizationBits) - 1;
	FVector2f Coord = OctahedronEncode(N);

	const float Scale = 0.5f * (float)QuantizationMaxValue;
	const float Bias = 0.5f * (float)QuantizationMaxValue;
	int32 NX = FMath::Clamp(int32(Coord.X * Scale + Bias), 0, QuantizationMaxValue);
	int32 NY = FMath::Clamp(int32(Coord.Y * Scale + Bias), 0, QuantizationMaxValue);

	float MinError = 1.0f;
	int32 BestNX = 0;
	int32 BestNY = 0;
	for (int32 OffsetY = 0; OffsetY < 2; OffsetY++)
	{
		for (int32 OffsetX = 0; OffsetX < 2; OffsetX++)
		{
			int32 TX = NX + OffsetX;
			int32 TY = NY + OffsetY;
			if (TX <= QuantizationMaxValue && TY <= QuantizationMaxValue)
			{
				FVector3f RN = OctahedronDecode(TX, TY, QuantizationBits);
				float Error = FMath::Abs(1.0f - (RN | N));
				if (Error < MinError)
				{
					MinError = Error;
					BestNX = TX;
					BestNY = TY;
				}
			}
		}
	}

	X = BestNX;
	Y = BestNY;
}

FORCEINLINE static uint32 PackNormal(FVector3f Normal, uint32 QuantizationBits)
{
	int32 X, Y;
	OctahedronEncodePreciseSIMD(Normal, X, Y, QuantizationBits);

#if 0
	// Test against non-SIMD version
	int32 X2, Y2;
	OctahedronEncodePrecise(Normal, X2, Y2, QuantizationBits);
	FVector3f N0 = OctahedronDecode( X, Y, QuantizationBits );
	FVector3f N1 = OctahedronDecode( X2, Y2, QuantizationBits );
	float dt0 = Normal | N0;
	float dt1 = Normal | N1;
	check( dt0 >= dt1*0.99999f );
#endif
	
	return (Y << QuantizationBits) | X;
}

FORCEINLINE static FVector3f UnpackNormal(uint32 PackedNormal, uint32 QuantizationBits)
{
	const uint32 QuantizationMaxValue = (1u << QuantizationBits) - 1u;
	const uint32 UX = PackedNormal & QuantizationMaxValue;
	const uint32 UY = PackedNormal >> QuantizationBits;
	float X = float(UX) * (2.0f / float(QuantizationMaxValue)) - 1.0f;
	float Y = float(UY) * (2.0f / float(QuantizationMaxValue)) - 1.0f;
	const float Z = 1.0f - FMath::Abs(X) - FMath::Abs(Y);
	const float T = FMath::Clamp(-Z, 0.0f, 1.0f);
	X += (X >= 0.0f) ? -T : T;
	Y += (Y >= 0.0f) ? -T : T;

	return FVector3f(X, Y, Z).GetUnsafeNormal();
}

static bool PackTangent(uint32& QuantizedTangentAngle, FVector3f TangentX, FVector3f TangentZ, uint32 NumTangentBits)
{
	FVector3f LocalTangentX = TangentX;
	FVector3f LocalTangentZ = TangentZ;
	
	// Conditionally swap X and Z, if abs(Z)>abs(X).
	// After this, we know the largest component is in X or Y and at least one of them is going to be non-zero.
	checkSlow(TangentZ.IsNormalized());
	const bool bSwapXZ = (FMath::Abs(LocalTangentZ.Z) > FMath::Abs(LocalTangentZ.X));
	if (bSwapXZ)
	{
		Swap(LocalTangentZ.X, LocalTangentZ.Z);
		Swap(LocalTangentX.X, LocalTangentX.Z);
	}

	FVector3f LocalTangentRefX = FVector3f(-LocalTangentZ.Y, LocalTangentZ.X, 0.0f).GetSafeNormal();
	FVector3f LocalTangentRefY = (LocalTangentZ ^ LocalTangentRefX);

	const float X = LocalTangentX | LocalTangentRefX;
	const float Y = LocalTangentX | LocalTangentRefY;
	const float LenSq = X * X + Y * Y;

	if (LenSq >= 0.0001f)
	{
		float Angle = FMath::Atan2(Y, X);
		if (Angle < PI) Angle += 2.0f * PI;

		const float UnitAngle = Angle / (2.0f * PI);

		int IntAngle = FMath::FloorToInt(UnitAngle * float(1 << NumTangentBits) + 0.5f);
		QuantizedTangentAngle = uint32(IntAngle & ((1 << NumTangentBits) - 1));
		return true;
	}

	return false;
}

static FVector3f UnpackTangent(uint32& QuantizedTangentAngle, FVector3f TangentZ, uint32 NumTangentBits)
{
	FVector3f LocalTangentZ = TangentZ;
	
	const bool bSwapXZ = (FMath::Abs(TangentZ.Z) > FMath::Abs(TangentZ.X));
	if (bSwapXZ)
	{
		Swap(LocalTangentZ.X, LocalTangentZ.Z);
	}

	const FVector3f LocalTangentRefX = FVector3f(-LocalTangentZ.Y, LocalTangentZ.X, 0.0f).GetSafeNormal();
	const FVector3f LocalTangentRefY = (LocalTangentZ ^ LocalTangentRefX);

	const float UnpackedAngle = float(QuantizedTangentAngle) / float(1 << NumTangentBits) * 2.0f * PI;
	FVector3f UnpackedTangentX = (LocalTangentRefX * FMath::Cos(UnpackedAngle) + LocalTangentRefY * FMath::Sin(UnpackedAngle)).GetUnsafeNormal();

	if (bSwapXZ)
	{
		Swap(UnpackedTangentX.X, UnpackedTangentX.Z);
	}

	return UnpackedTangentX;
}

static void CalculateEncodingInfo(FEncodingInfo& Info, const FCluster& Cluster, int32 NormalPrecision, int32 TangentPrecision, int32 BoneWeightPrecision)
{
	const uint32 NumClusterVerts	= Cluster.Verts.Num();
	const uint32 NumClusterTris		= Cluster.NumTris;
	const uint32 MaxBones			= Cluster.Verts.Format.NumBoneInfluences;

	FMemory::Memzero(Info);

	// Write triangles indices. Indices are stored in a dense packed bitstream using ceil(log2(NumClusterVerices)) bits per index. The shaders implement unaligned bitstream reads to support this.
	const uint32 BitsPerIndex = NumClusterVerts > 1 && NumClusterTris > 1 ? (FGenericPlatformMath::FloorLog2(NumClusterVerts - 1) + 1) : 1;
	const uint32 BitsPerTriangle = BitsPerIndex + 2 * 5;	// Base index + two 5-bit offsets
	Info.BitsPerIndex = BitsPerIndex;

	FPageSections& GpuSizes = Info.GpuSizes;
	GpuSizes.Cluster = sizeof(FPackedCluster);
	GpuSizes.MaterialTable = CalcMaterialTableSize(Cluster) * sizeof(uint32);
	GpuSizes.VertReuseBatchInfo = Cluster.NumTris && Cluster.MaterialRanges.Num() > 3 ? CalcVertReuseBatchInfoSize(Cluster.MaterialRanges) * sizeof(uint32) : 0;
	GpuSizes.DecodeInfo = Cluster.Verts.Format.NumTexCoords * sizeof(FPackedUVHeader) + (MaxBones > 0 ? sizeof(FPackedBoneInfluenceHeader) : 0);
	GpuSizes.Index = (NumClusterTris * BitsPerTriangle + 31) / 32 * 4;

	GpuSizes.BrickData = Cluster.Bricks.Num() * sizeof(FPackedBrick);

	const uint32 NumPositions = (Cluster.NumTris != 0) ? NumClusterVerts : 0;
#if NANITE_USE_UNCOMPRESSED_VERTEX_DATA
	const uint32 AttribBytesPerVertex = (3 * sizeof(float) + (Cluster.Verts.Format.bHasTangents ? (4 * sizeof(float)) : 0) + sizeof(uint32) + Cluster.Verts.Format.NumTexCoords * 2 * sizeof(float));

	Info.BitsPerAttribute = AttribBytesPerVertex * 8;
	Info.ColorMin = FIntVector4(0, 0, 0, 0);
	Info.ColorBits = FIntVector4(8, 8, 8, 8);
	Info.ColorMode = NANITE_VERTEX_COLOR_MODE_VARIABLE;
	Info.NormalPrecision = 0;
	Info.TangentPrecision = 0;

	// TODO: Nanite-Skinning: Implement uncompressed path

	GpuSizes.Position = NumPositions * 3 * sizeof(float);
	GpuSizes.Attribute = NumClusterVerts * AttribBytesPerVertex;
#else
	Info.BitsPerAttribute = 2 * NormalPrecision;

	if (Cluster.Verts.Format.bHasTangents)
	{
		Info.BitsPerAttribute += 1 + TangentPrecision;
	}

	check(NumClusterVerts > 0);
	const bool bIsLeaf = (Cluster.GeneratingGroupIndex == MAX_uint32);

	// Normals
	Info.NormalPrecision = NormalPrecision;
	Info.TangentPrecision = TangentPrecision;

	// Vertex colors
	Info.ColorMode = NANITE_VERTEX_COLOR_MODE_CONSTANT;
	Info.ColorMin = FIntVector4(255, 255, 255, 255);
	if (Cluster.Verts.Format.bHasColors)
	{
		FIntVector4 ColorMin = FIntVector4( 255, 255, 255, 255);
		FIntVector4 ColorMax = FIntVector4( 0, 0, 0, 0);
		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			FColor Color = Cluster.Verts.GetColor(i).ToFColor(false);
			ColorMin.X = FMath::Min(ColorMin.X, (int32)Color.R);
			ColorMin.Y = FMath::Min(ColorMin.Y, (int32)Color.G);
			ColorMin.Z = FMath::Min(ColorMin.Z, (int32)Color.B);
			ColorMin.W = FMath::Min(ColorMin.W, (int32)Color.A);
			ColorMax.X = FMath::Max(ColorMax.X, (int32)Color.R);
			ColorMax.Y = FMath::Max(ColorMax.Y, (int32)Color.G);
			ColorMax.Z = FMath::Max(ColorMax.Z, (int32)Color.B);
			ColorMax.W = FMath::Max(ColorMax.W, (int32)Color.A);
		}

		const FIntVector4 ColorDelta = ColorMax - ColorMin;
		const int32 R_Bits = FMath::CeilLogTwo(ColorDelta.X + 1);
		const int32 G_Bits = FMath::CeilLogTwo(ColorDelta.Y + 1);
		const int32 B_Bits = FMath::CeilLogTwo(ColorDelta.Z + 1);
		const int32 A_Bits = FMath::CeilLogTwo(ColorDelta.W + 1);
		
		uint32 NumColorBits = R_Bits + G_Bits + B_Bits + A_Bits;
		Info.BitsPerAttribute += NumColorBits;
		Info.ColorMin = ColorMin;
		Info.ColorBits = FIntVector4(R_Bits, G_Bits, B_Bits, A_Bits);
		if (NumColorBits > 0)
		{
			Info.ColorMode = NANITE_VERTEX_COLOR_MODE_VARIABLE;
		}
	}

	const int NumMantissaBits = NANITE_UV_FLOAT_NUM_MANTISSA_BITS;	//TODO: make this a build setting
	for( uint32 UVIndex = 0; UVIndex < Cluster.Verts.Format.NumTexCoords; UVIndex++ )
	{
		FUintVector2 UVMin = FUintVector2(0xFFFFFFFFu, 0xFFFFFFFFu);
		FUintVector2 UVMax = FUintVector2(0u, 0u);

		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			const FVector2f& UV = Cluster.Verts.GetUVs(i)[UVIndex];

			const uint32 EncodedU = EncodeUVFloat(UV.X, NumMantissaBits);
			const uint32 EncodedV = EncodeUVFloat(UV.Y, NumMantissaBits);

			UVMin.X = FMath::Min(UVMin.X, EncodedU);
			UVMin.Y = FMath::Min(UVMin.Y, EncodedV);
			UVMax.X = FMath::Max(UVMax.X, EncodedU);
			UVMax.Y = FMath::Max(UVMax.Y, EncodedV);
		}

		const FUintVector2 UVDelta = UVMax - UVMin;

		FUVInfo& UVInfo = Info.UVs[UVIndex];
		UVInfo.Min				= UVMin;
		UVInfo.NumBits.X		= FMath::CeilLogTwo(UVDelta.X + 1u);
		UVInfo.NumBits.Y		= FMath::CeilLogTwo(UVDelta.Y + 1u);

		Info.BitsPerAttribute	+= UVInfo.NumBits.X + UVInfo.NumBits.Y;
	}

	if (MaxBones > 0)
	{
		CalculateInfluences(Info.BoneInfluence, Cluster, BoneWeightPrecision);

		// TODO: Nanite-Skinning: Make this more compact. Range of indices? Palette of indices? Omit the last weight?
		const uint32 VertexInfluenceSize	= ( NumClusterVerts * Info.BoneInfluence.NumVertexBoneInfluences * ( Info.BoneInfluence.NumVertexBoneIndexBits + Info.BoneInfluence.NumVertexBoneWeightBits ) + 31) / 32 * 4;
		GpuSizes.BoneInfluence				= VertexInfluenceSize;

		check(IsAligned(GpuSizes.BoneInfluence, 4));
	}

	const uint32 PositionBitsPerVertex = Cluster.QuantizedPosBits.X + Cluster.QuantizedPosBits.Y + Cluster.QuantizedPosBits.Z;
	GpuSizes.Position = (NumPositions * PositionBitsPerVertex + 31) / 32 * 4;
	GpuSizes.Attribute = (NumClusterVerts * Info.BitsPerAttribute + 31) / 32 * 4;
#endif
}

void EncodeGeometryData(	const uint32 LocalClusterIndex, const FCluster& Cluster, const FEncodingInfo& EncodingInfo,
							const TArrayView<uint16> PageDependencies, const TArray<TMap<FVariableVertex, FVertexMapEntry>>& PageVertexMaps,
							TMap<FVariableVertex, uint32>& UniqueVertices, uint32& NumCodedVertices, FPageStreams& Streams)
{
	const uint32 NumClusterVerts = Cluster.Verts.Num();
	const uint32 NumClusterTris = Cluster.NumTris;

	Streams.VertexRefBitmask.AddZeroed(NANITE_MAX_CLUSTER_VERTICES / 32);

	TArray<uint32> UniqueToVertexIndex;

	bool bUseVertexRefs = NumClusterTris > 0 && !NANITE_USE_UNCOMPRESSED_VERTEX_DATA;	// TODO: Skip voxels for now. Currently, voxel almost never match parents exactly.
	if( !bUseVertexRefs )
	{
		NumCodedVertices = NumClusterVerts;
	}
	else
	{
		// Find vertices from same page we can reference instead of storing duplicates
		struct FVertexRef
		{
			uint32 PageIndex;
			uint32 LocalClusterIndex;
			uint32 VertexIndex;
		};
		TArray<FVertexRef> VertexRefs;

		for (uint32 VertexIndex = 0; VertexIndex < NumClusterVerts; VertexIndex++)
		{
			FVariableVertex Vertex;
			Vertex.Data = &Cluster.Verts.Array[ VertexIndex * Cluster.Verts.GetVertSize() ];
			Vertex.SizeInBytes = Cluster.Verts.GetVertSize() * sizeof(float);

			FVertexRef VertexRef = {};
			bool bFound = false;

			// Look for vertex in parents
			for (int32 SrcPageIndexIndex = 0; SrcPageIndexIndex < PageDependencies.Num(); SrcPageIndexIndex++)
			{
				uint32 SrcPageIndex = PageDependencies[SrcPageIndexIndex];
				const FVertexMapEntry* EntryPtr = PageVertexMaps[SrcPageIndex].Find(Vertex);
				if (EntryPtr)
				{
					VertexRef = FVertexRef{ (uint32)SrcPageIndexIndex + 1, EntryPtr->LocalClusterIndex, EntryPtr->VertexIndex };
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				// Look for vertex in current page
				uint32* VertexPtr = UniqueVertices.Find(Vertex);
				if (VertexPtr)
				{
					VertexRef = FVertexRef{ 0, (*VertexPtr >> NANITE_MAX_CLUSTER_VERTICES_BITS), *VertexPtr & NANITE_MAX_CLUSTER_VERTICES_MASK };
					bFound = true;
				}
			}

			if(bFound)
			{
				VertexRefs.Add(VertexRef);
				const uint32 BitIndex = (LocalClusterIndex << NANITE_MAX_CLUSTER_VERTICES_BITS) + VertexIndex;
				Streams.VertexRefBitmask[BitIndex >> 5] |= 1u << (BitIndex & 31);
			}
			else
			{
				uint32 Val = (LocalClusterIndex << NANITE_MAX_CLUSTER_VERTICES_BITS) | (uint32)VertexIndex;
				UniqueVertices.Add(Vertex, Val);
				UniqueToVertexIndex.Add(VertexIndex);
			}
		}
		NumCodedVertices = UniqueToVertexIndex.Num();

		struct FClusterRef
		{
			uint32 PageIndex;
			uint32 ClusterIndex;

			bool operator==(const FClusterRef& Other) const { return PageIndex == Other.PageIndex && ClusterIndex == Other.ClusterIndex; }
			bool operator<(const FClusterRef& Other) const { return (PageIndex != Other.PageIndex) ? (PageIndex < Other.PageIndex) : (ClusterIndex == Other.ClusterIndex); }
		};

		// Make list of unique Page-Cluster pairs
		TArray<FClusterRef> ClusterRefs;
		for (const FVertexRef& Ref : VertexRefs)
			ClusterRefs.AddUnique(FClusterRef{ Ref.PageIndex, Ref.LocalClusterIndex });
	
		ClusterRefs.Sort();

		for (const FClusterRef& Ref : ClusterRefs)
		{
			Streams.PageClusterPair.Add((Ref.PageIndex << NANITE_MAX_CLUSTERS_PER_PAGE_BITS) | Ref.ClusterIndex);
		}

		// Write vertex refs using Page-Cluster index + vertex index
		uint32 PrevVertexIndex = 0;
		for (const FVertexRef& Ref : VertexRefs)
		{
			uint32 PageClusterIndex = ClusterRefs.Find(FClusterRef{ Ref.PageIndex, Ref.LocalClusterIndex });
			check(PageClusterIndex < 256);
			const uint32 VertexIndexDelta = (Ref.VertexIndex - PrevVertexIndex) & 0xFF;
			Streams.VertexRef.Add(uint16((PageClusterIndex << NANITE_MAX_CLUSTER_VERTICES_BITS) | EncodeZigZag(ShortestWrap(VertexIndexDelta, 8))));
			PrevVertexIndex = Ref.VertexIndex;
		}
	}

	const uint32 BitsPerIndex = EncodingInfo.BitsPerIndex;
	
	// Write triangle indices
#if NANITE_USE_STRIP_INDICES
	for (uint32 i = 0; i < NANITE_MAX_CLUSTER_TRIANGLES / 32; i++)
	{
		Streams.StripBitmask.Add(Cluster.StripDesc.Bitmasks[i][0]);
		Streams.StripBitmask.Add(Cluster.StripDesc.Bitmasks[i][1]);
		Streams.StripBitmask.Add(Cluster.StripDesc.Bitmasks[i][2]);
	}
	Streams.Index.Append(Cluster.StripIndexData);
#else
	for (uint32 i = 0; i < NumClusterTris * 3; i++)
	{
		uint32 Index = Cluster.Indexes[i];
		Streams.Index.Add(Cluster.Indexes[i]);
	}
#endif

	check(NumClusterVerts > 0);

#if NANITE_USE_UNCOMPRESSED_VERTEX_DATA
	FBitWriter BitWriter_Position(Streams.LowByte);
	for (uint32 VertexIndex = 0; VertexIndex < NumClusterVerts; VertexIndex++)
	{
		const FVector3f& Position = Cluster.Verts.GetPosition(VertexIndex);
		BitWriter_Position.PutBits(*(uint32*)&Position.X, 32);
		BitWriter_Position.PutBits(*(uint32*)&Position.Y, 32);
		BitWriter_Position.PutBits(*(uint32*)&Position.Z, 32);
	}
	BitWriter_Position.Flush(sizeof(uint32));

	FBitWriter BitWriter_Attribute(Streams.MidByte);
	for (uint32 VertexIndex = 0; VertexIndex < NumClusterVerts; VertexIndex++)
	{
		// Normal
		const FVector3f& Normal = Cluster.Verts.GetNormal(VertexIndex);
		BitWriter_Attribute.PutBits(*(uint32*)&Normal.X, 32);
		BitWriter_Attribute.PutBits(*(uint32*)&Normal.Y, 32);
		BitWriter_Attribute.PutBits(*(uint32*)&Normal.Z, 32);

		if(Cluster.Verts.Format.bHasTangents)
		{
			const FVector3f TangentX = Cluster.Verts.GetTangentX(VertexIndex);
			BitWriter_Attribute.PutBits(*(uint32*)&TangentX.X, 32);
			BitWriter_Attribute.PutBits(*(uint32*)&TangentX.Y, 32);
			BitWriter_Attribute.PutBits(*(uint32*)&TangentX.Z, 32);

			const float TangentYSign = Cluster.Verts.GetTangentYSign(VertexIndex) < 0.0f ? -1.0f : 1.0f;
			BitWriter_Attribute.PutBits(*(uint32*)&TangentYSign, 32);
		}

		// Color
		uint32 ColorDW = Cluster.Verts.Format.bHasColors ? Cluster.Verts.GetColor(VertexIndex).ToFColor(false).DWColor() : 0xFFFFFFFFu;
		BitWriter_Attribute.PutBits(ColorDW, 32);

		// UVs
		if (Cluster.Verts.Format.NumTexCoords > 0)
		{
			const FVector2f* UVs = Cluster.Verts.GetUVs(VertexIndex);
			for (uint32 TexCoordIndex = 0; TexCoordIndex < Cluster.Verts.Format.NumTexCoords; TexCoordIndex++)
			{
				const FVector2f UV = (TexCoordIndex < Cluster.Verts.Format.NumTexCoords) ? UVs[TexCoordIndex] : FVector2f(0.0f);
				BitWriter_Attribute.PutBits(*(uint32*)&UV.X, 32);
				BitWriter_Attribute.PutBits(*(uint32*)&UV.Y, 32);
			}
		}
	}
	BitWriter_Attribute.Flush(sizeof(uint32));
#else
	const uint32 NumUniqueToVertices = bUseVertexRefs ? UniqueToVertexIndex.Num() : NumClusterVerts;

	// Generate quantized texture coordinates
	TArray<FIntVector2, TInlineAllocator<NANITE_MAX_CLUSTER_VERTICES*NANITE_MAX_UVS>> PackedUVs;
	PackedUVs.AddUninitialized( NumClusterVerts * Cluster.Verts.Format.NumTexCoords );

	const uint32 NumMantissaBits = NANITE_UV_FLOAT_NUM_MANTISSA_BITS;
	for( uint32 UVIndex = 0; UVIndex < Cluster.Verts.Format.NumTexCoords; UVIndex++ )
	{
		const FUVInfo& UVInfo = EncodingInfo.UVs[UVIndex];
		const uint32 NumTexCoordValuesU = 1u << UVInfo.NumBits.X;
		const uint32 NumTexCoordValuesV = 1u << UVInfo.NumBits.Y;

		for (uint32 LocalVertexIndex = 0; LocalVertexIndex < NumUniqueToVertices; LocalVertexIndex++)
		{
			uint32 VertexIndex = LocalVertexIndex;
			if( bUseVertexRefs )
				VertexIndex = UniqueToVertexIndex[LocalVertexIndex];

			const FVector2f UV = (UVIndex < Cluster.Verts.Format.NumTexCoords) ? Cluster.Verts.GetUVs(VertexIndex)[UVIndex] : FVector2f(0.0f);

			uint32 EncodedU = EncodeUVFloat(UV.X, NumMantissaBits);
			uint32 EncodedV = EncodeUVFloat(UV.Y, NumMantissaBits);

			check(EncodedU >= UVInfo.Min.X);
			check(EncodedV >= UVInfo.Min.Y);
			EncodedU -= UVInfo.Min.X;
			EncodedV -= UVInfo.Min.Y;
			
			check(EncodedU >= 0 && EncodedU < NumTexCoordValuesU);
			check(EncodedV >= 0 && EncodedV < NumTexCoordValuesV);
			PackedUVs[NumClusterVerts * UVIndex + VertexIndex].X = (int32)EncodedU;
			PackedUVs[NumClusterVerts * UVIndex + VertexIndex].Y = (int32)EncodedV;
		}		
	}

	auto WriteZigZagDelta = [&](const int32 Delta, const uint32 NumBytes) {
		const uint32 Value = EncodeZigZag(Delta);
		checkSlow(DecodeZigZag(Value) == Delta);
		
		checkSlow(NumBytes <= 3);
		checkSlow(Value < (1u << (NumBytes*8)));

		if (NumBytes >= 3)
		{
			Streams.HighByte.Add((Value >> 16) & 0xFFu);
		}

		if (NumBytes >= 2)
		{
			Streams.MidByte.Add((Value >> 8) & 0xFFu);
		}

		if (NumBytes >= 1)
		{
			Streams.LowByte.Add(Value & 0xFFu);
		}
	};

	const uint32 BytesPerPositionComponent = (FMath::Max3(Cluster.QuantizedPosBits.X, Cluster.QuantizedPosBits.Y, Cluster.QuantizedPosBits.Z) + 7) / 8;
	const uint32 BytesPerNormalComponent = (EncodingInfo.NormalPrecision + 7) / 8;
	const uint32 BytesPerTangentComponent = (EncodingInfo.TangentPrecision + 1 + 7) / 8;

	// Position
	if (Cluster.NumTris != 0)
	{	
		FIntVector PrevPosition = FIntVector((1 << Cluster.QuantizedPosBits.X) >> 1, (1 << Cluster.QuantizedPosBits.Y) >> 1, (1 << Cluster.QuantizedPosBits.Z) >> 1);

		for (uint32 LocalVertexIndex = 0; LocalVertexIndex < NumUniqueToVertices; LocalVertexIndex++)
		{
			uint32 VertexIndex = LocalVertexIndex;
			if( bUseVertexRefs )
				VertexIndex = UniqueToVertexIndex[LocalVertexIndex];

			const FIntVector& Position = Cluster.QuantizedPositions[VertexIndex];
			FIntVector PositionDelta = Position - PrevPosition;

			PositionDelta.X = ShortestWrap(PositionDelta.X, Cluster.QuantizedPosBits.X);
			PositionDelta.Y = ShortestWrap(PositionDelta.Y, Cluster.QuantizedPosBits.Y);
			PositionDelta.Z = ShortestWrap(PositionDelta.Z, Cluster.QuantizedPosBits.Z);

			WriteZigZagDelta(PositionDelta.X, BytesPerPositionComponent);
			WriteZigZagDelta(PositionDelta.Y, BytesPerPositionComponent);
			WriteZigZagDelta(PositionDelta.Z, BytesPerPositionComponent);
			PrevPosition = Position;
		}
	}

	FIntPoint PrevNormal = FIntPoint::ZeroValue;

	TArray< uint32, TInlineAllocator<NANITE_MAX_CLUSTER_VERTICES> > PackedNormals;
	PackedNormals.AddUninitialized( NumClusterVerts );

	// Normal
	for (uint32 LocalVertexIndex = 0; LocalVertexIndex < NumUniqueToVertices; LocalVertexIndex++)
	{
		uint32 VertexIndex = LocalVertexIndex;
		if( bUseVertexRefs )
			VertexIndex = UniqueToVertexIndex[LocalVertexIndex];

		const uint32 PackedNormal = PackNormal(Cluster.Verts.GetNormal(VertexIndex), EncodingInfo.NormalPrecision);
		const FIntPoint Normal = FIntPoint(PackedNormal & ((1u << EncodingInfo.NormalPrecision) - 1u), PackedNormal >> EncodingInfo.NormalPrecision);
		PackedNormals[LocalVertexIndex] = PackedNormal;
			
		FIntPoint NormalDelta = Normal - PrevNormal;
		NormalDelta.X = ShortestWrap(NormalDelta.X, EncodingInfo.NormalPrecision);
		NormalDelta.Y = ShortestWrap(NormalDelta.Y, EncodingInfo.NormalPrecision);
		PrevNormal = Normal;

		WriteZigZagDelta(NormalDelta.X, BytesPerNormalComponent);
		WriteZigZagDelta(NormalDelta.Y, BytesPerNormalComponent);
	}


	// Tangent
	if (Cluster.Verts.Format.bHasTangents)
	{
		uint32 PrevTangentBits = 0u;
		for (uint32 LocalVertexIndex = 0; LocalVertexIndex < NumUniqueToVertices; LocalVertexIndex++)
		{
			uint32 VertexIndex = LocalVertexIndex;
			if( bUseVertexRefs )
				VertexIndex = UniqueToVertexIndex[LocalVertexIndex];

			const uint32 PackedTangentZ = PackedNormals[LocalVertexIndex];

			FVector3f TangentX = Cluster.Verts.GetTangentX(VertexIndex);
			const FVector3f UnpackedTangentZ = UnpackNormal(PackedTangentZ, EncodingInfo.NormalPrecision);
			checkSlow(UnpackedTangentZ.IsNormalized());

			uint32 TangentBits = PrevTangentBits;	// HACK: If tangent space has collapsed, just repeat the tangent used by the previous vertex
			if(TangentX.SquaredLength() > 1e-8f)
			{
				TangentX = TangentX.GetUnsafeNormal();
				
				const bool bTangentYSign = Cluster.Verts.GetTangentYSign(VertexIndex) < 0.0f;
				uint32 QuantizedTangentAngle;
				if (PackTangent(QuantizedTangentAngle, TangentX, UnpackedTangentZ, EncodingInfo.TangentPrecision))
				{
					TangentBits = (bTangentYSign ? (1 << EncodingInfo.TangentPrecision) : 0) | QuantizedTangentAngle;
				}
			}
			
			const uint32 TangentDelta = ShortestWrap(TangentBits - PrevTangentBits, EncodingInfo.TangentPrecision + 1);
			WriteZigZagDelta(TangentDelta, BytesPerTangentComponent);
				
			PrevTangentBits = TangentBits;
		}
	}

	// Color
	if (EncodingInfo.ColorMode == NANITE_VERTEX_COLOR_MODE_VARIABLE)
	{
		FIntVector4 PrevColor = FIntVector4(0);
		for (uint32 LocalVertexIndex = 0; LocalVertexIndex < NumUniqueToVertices; LocalVertexIndex++)
		{
			uint32 VertexIndex = LocalVertexIndex;
			if( bUseVertexRefs )
				VertexIndex = UniqueToVertexIndex[LocalVertexIndex];

			const FColor Color = Cluster.Verts.GetColor(VertexIndex).ToFColor(false);
			const FIntVector4 ColorValue = FIntVector4(Color.R, Color.G, Color.B, Color.A) - EncodingInfo.ColorMin;
			FIntVector4 ColorDelta = ColorValue - PrevColor;

			ColorDelta.X = ShortestWrap(ColorDelta.X, EncodingInfo.ColorBits.X);
			ColorDelta.Y = ShortestWrap(ColorDelta.Y, EncodingInfo.ColorBits.Y);
			ColorDelta.Z = ShortestWrap(ColorDelta.Z, EncodingInfo.ColorBits.Z);
			ColorDelta.W = ShortestWrap(ColorDelta.W, EncodingInfo.ColorBits.W);

			WriteZigZagDelta(ColorDelta.X, 1);
			WriteZigZagDelta(ColorDelta.Y, 1);
			WriteZigZagDelta(ColorDelta.Z, 1);
			WriteZigZagDelta(ColorDelta.W, 1);

			PrevColor = ColorValue;
		}
	}
		
	// UV
	for (uint32 TexCoordIndex = 0; TexCoordIndex < Cluster.Verts.Format.NumTexCoords; TexCoordIndex++)
	{
		const int32 NumTexCoordBitsU = EncodingInfo.UVs[TexCoordIndex].NumBits.X;
		const int32 NumTexCoordBitsV = EncodingInfo.UVs[TexCoordIndex].NumBits.Y;
		const uint32 BytesPerTexCoordComponent = (FMath::Max(NumTexCoordBitsU, NumTexCoordBitsV) + 7) / 8;
			
		FIntVector2 PrevUV = FIntVector2::ZeroValue;
		for (uint32 LocalVertexIndex = 0; LocalVertexIndex < NumUniqueToVertices; LocalVertexIndex++)
		{
			uint32 VertexIndex = LocalVertexIndex;
			if( bUseVertexRefs )
				VertexIndex = UniqueToVertexIndex[LocalVertexIndex];

			const FIntVector2 UV = PackedUVs[NumClusterVerts * TexCoordIndex + VertexIndex];

			FIntVector2 UVDelta = UV - PrevUV;
			UVDelta.X = ShortestWrap(UVDelta.X, NumTexCoordBitsU);
			UVDelta.Y = ShortestWrap(UVDelta.Y, NumTexCoordBitsV);
			WriteZigZagDelta(UVDelta.X, BytesPerTexCoordComponent);
			WriteZigZagDelta(UVDelta.Y, BytesPerTexCoordComponent);
			PrevUV = UV;
		}
	}

	const uint32 NumVertexBones = EncodingInfo.BoneInfluence.NumVertexBoneInfluences;
	if (NumVertexBones > 0)
	{
		// TODO: Nanite-Skinning: support parent references
		FBitWriter BitWriter(Streams.BoneInfluence);
		
		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			const FVector2f* BoneInfluences = Cluster.Verts.GetBoneInfluences(i);
			for (uint32 j = 0; j < NumVertexBones; j++)
			{
				const uint32 BoneIndex = (uint32)BoneInfluences[j].X;
				const uint32 BoneWeight = (uint32)BoneInfluences[j].Y;
				BitWriter.PutBits(BoneWeight ? BoneIndex : 0u,	EncodingInfo.BoneInfluence.NumVertexBoneIndexBits);
				
				if(EncodingInfo.BoneInfluence.NumVertexBoneWeightBits > 0)
				{
					BitWriter.PutBits(BoneWeight, EncodingInfo.BoneInfluence.NumVertexBoneWeightBits);
				}
			}
		}
		BitWriter.Flush(sizeof(uint32));
	}


#endif
}

TArray<TMap<FVariableVertex, FVertexMapEntry>> BuildVertexMaps(const TArray<FPage>& Pages, const TArray<FCluster>& Clusters, const TArray<FClusterGroupPart>& Parts)
{
	TArray<TMap<FVariableVertex, FVertexMapEntry>> VertexMaps;
	VertexMaps.SetNum(Pages.Num());

	ParallelFor( TEXT("NaniteEncode.BuildVertexMaps.PF"), Pages.Num(), 1, [&VertexMaps, &Pages, &Clusters, &Parts](int32 PageIndex)
	{
		const FPage& Page = Pages[PageIndex];
		ProcessPageClusters(Page, Parts, [&](uint32 LocalClusterIndex, uint32 ClusterIndex)
		{
			const FCluster& Cluster = Clusters[ClusterIndex];

			if (Cluster.Verts.Num() == 0)	// TODO: Skip voxels for now. Currently, voxel almost never match parents exactly.
				return;

			for (uint32 VertexIndex = 0; VertexIndex < Cluster.Verts.Num(); VertexIndex++)
			{
				FVariableVertex Vertex;
				Vertex.Data = &Cluster.Verts.Array[VertexIndex * Cluster.Verts.GetVertSize()];
				Vertex.SizeInBytes = Cluster.Verts.GetVertSize() * sizeof(float);
				FVertexMapEntry Entry;
				Entry.LocalClusterIndex = LocalClusterIndex;
				Entry.VertexIndex = VertexIndex;
				VertexMaps[PageIndex].Add(Vertex, Entry);
			}
		});
	});
	return VertexMaps;
}


void CalculateEncodingInfos(
	TArray<FEncodingInfo>& EncodingInfos,
	const TArray<FCluster>& Clusters,
	int32 NormalPrecision,
	int32 TangentPrecision,
	int32 BoneWeightPrecision
)
{
	uint32 NumClusters = Clusters.Num();
	EncodingInfos.SetNumUninitialized(NumClusters);

	ParallelFor(TEXT("NaniteEncode.CalculateEncodingInfos.PF"), Clusters.Num(), 128,
		[&](uint32 ClusterIndex)
		{
			CalculateEncodingInfo(EncodingInfos[ClusterIndex], Clusters[ClusterIndex], NormalPrecision, TangentPrecision, BoneWeightPrecision);
		});
}

} // namespace Nanite
