// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"
#include "Affine.h"
#include "DisplacementMap.h"
#include "LerpVert.h"

#include "AdaptiveTessellatorMesh.h"

namespace Nanite {

inline void InterpolateNormalsAndUVs(
	const FVector3f& Barycentrics,
	const FLerpVert& Vert0,
	const FLerpVert& Vert1,
	const FLerpVert& Vert2,
	FVector2f& OutUV,
	FVector3f& OutNormal)
{
	OutUV = Vert0.UVs[0] * Barycentrics.X;
	OutUV += Vert1.UVs[0] * Barycentrics.Y;
	OutUV += Vert2.UVs[0] * Barycentrics.Z;

	OutNormal = Vert0.TangentZ * Barycentrics.X;
	OutNormal += Vert1.TangentZ * Barycentrics.Y;
	OutNormal += Vert2.TangentZ * Barycentrics.Z;
	OutNormal.Normalize();
}

inline FVector2f GetErrorBounds(
	const FVector3f Barycentrics[3],
	const FVector3f VertexNormals[3],
	const FVector2f VertexUVs[3],
	const FVector3f Displacements[3],
	const FDisplacementMap& DisplacementMap)
{
	float MinBarycentric0 = FMath::Min3(Barycentrics[0].X, Barycentrics[1].X, Barycentrics[2].X);
	float MaxBarycentric0 = FMath::Max3(Barycentrics[0].X, Barycentrics[1].X, Barycentrics[2].X);

	float MinBarycentric1 = FMath::Min3(Barycentrics[0].Y, Barycentrics[1].Y, Barycentrics[2].Y);
	float MaxBarycentric1 = FMath::Max3(Barycentrics[0].Y, Barycentrics[1].Y, Barycentrics[2].Y);

	TAffine<float, 2> Barycentric0(MinBarycentric0, MaxBarycentric0, 0);
	TAffine<float, 2> Barycentric1(MinBarycentric1, MaxBarycentric1, 1);
	TAffine<float, 2> Barycentric2 = TAffine< float, 2 >(1.0f) - Barycentric0 - Barycentric1;

	TAffine<FVector3f, 2> LerpedDisplacement;
	LerpedDisplacement  = TAffine<FVector3f, 2>(Displacements[0]) * Barycentric0;
	LerpedDisplacement += TAffine<FVector3f, 2>(Displacements[1]) * Barycentric1;
	LerpedDisplacement += TAffine<FVector3f, 2>(Displacements[2]) * Barycentric2;

	TAffine<FVector3f, 2> Normal;
	Normal  = TAffine<FVector3f, 2>(VertexNormals[0]) * Barycentric0;
	Normal += TAffine<FVector3f, 2>(VertexNormals[1]) * Barycentric1;
	Normal += TAffine<FVector3f, 2>(VertexNormals[2]) * Barycentric2;
	Normal = Normalize(Normal);

	FVector2f MinUV = { std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
	FVector2f MaxUV = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
	for (int k = 0; k < 3; k++)
	{
		FVector2f UV;
		UV =  VertexUVs[0] * Barycentrics[k].X;
		UV += VertexUVs[1] * Barycentrics[k].Y;
		UV += VertexUVs[2] * Barycentrics[k].Z;

		MinUV = FVector2f::Min(MinUV, UV);
		MaxUV = FVector2f::Max(MaxUV, UV);
	}

	const FVector2f DisplacementBounds = DisplacementMap.Sample(MinUV, MaxUV);

	Normal.SizeSquared();

	TAffine<float, 2> Displacement(DisplacementBounds.X, DisplacementBounds.Y);
	TAffine<float, 2> Error = (Normal * Displacement - LerpedDisplacement).SizeSquared();

	return FVector2f(Error.GetMin(), Error.GetMax());
}

inline FVector2f GetErrorBounds(
	const FVector3f Barycentrics[3],
	const FLerpVert& Vert0,
	const FLerpVert& Vert1,
	const FLerpVert& Vert2,
	const FVector3f& Displacement0,
	const FVector3f& Displacement1,
	const FVector3f& Displacement2,
	const FDisplacementMap& DisplacementMap)
{
	const FVector3f VertexNormals[] = { Vert0.TangentZ, Vert1.TangentZ, Vert2.TangentZ };
	const FVector2f VertexUVs[] = { Vert0.UVs[0], Vert1.UVs[0], Vert2.UVs[0] };
	const FVector3f Displacements[] = { Displacement0, Displacement1, Displacement2 };

	return GetErrorBounds(Barycentrics, VertexNormals, VertexUVs, Displacements, DisplacementMap);
}

template <typename T>
inline int32 GetNumSamples(
	const UE::Math::TVector<T> Barycentrics[3],
	const FVector2f VertexUVs[3],
	const FVector2f MapRes)
{
	FVector2f UVs[3];
	for (int k = 0; k < 3; k++)
	{
		UVs[k] =  VertexUVs[0] * Barycentrics[k].X;
		UVs[k] += VertexUVs[1] * Barycentrics[k].Y;
		UVs[k] += VertexUVs[2] * Barycentrics[k].Z;

		UVs[k].X *= MapRes[0];
		UVs[k].Y *= MapRes[1];
	}

	FVector2f Edge01 = UVs[1] - UVs[0];
	FVector2f Edge12 = UVs[2] - UVs[1];
	FVector2f Edge20 = UVs[0] - UVs[2];

	T MaxEdgeLength = FMath::Sqrt(FMath::Max3(
		Edge01.SizeSquared(),
		Edge12.SizeSquared(),
		Edge20.SizeSquared()));

	T AreaInTexels = FMath::Abs(0.5 * (Edge01 ^ Edge12));

	return static_cast<int32>(FMath::CeilToInt(FMath::Max(MaxEdgeLength, AreaInTexels)));
}

// same as above but use function references as original nanite code
class DisplacementPolicyFunctor
{
public:
	using FIndex3i = UE::Geometry::FIndex3i;

	using FDispFunc = TFunctionRef< FVector3f (
		const FVector3f&,	// Barycentrics,
		const FLerpVert&,	// Vert0,
		const FLerpVert&,	// Vert1,
		const FLerpVert&,	// Vert2,
		int32				// MaterialIndex
	) >;

	using FVectorDispFunc = TFunctionRef < FVector3f(
		const FVector3f&,   //< Undisplaced position
		const FVector2f&,   //< Sampling coordinates
		const FVector2f&,   //< Major axis
		const FVector2f&)>; //< Minor axis

	using FBoundsFunc = TFunctionRef< FVector2f (
		const FVector3f[3],	// Barycentrics[3],
		const FLerpVert&,	// Vert0,
		const FLerpVert&,	// Vert1,
		const FLerpVert&,	// Vert2,
		const FVector3f&,	// Displacement0,
		const FVector3f&,	// Displacement1,
		const FVector3f&,	// Displacement2,
		int32				// MaterialIndex
	) >;

	using FNumFunc = TFunctionRef< int32 (
		const FVector3f[3],	// Barycentrics[3],
		const FLerpVert&,	// Vert0,
		const FLerpVert&,	// Vert1,
		const FLerpVert&,	// Vert2,
		int32				// MaterialIndex
	) >;

	DisplacementPolicyFunctor(FMinimalMesh& InMesh,
		FDispFunc	InGetDisplacement,
		FVectorDispFunc InGetVectorDisplacement,
		FBoundsFunc	InGetErrorBounds,
		FNumFunc	InGetNumSamples)
		: Mesh(InMesh)
		, GetDisplacementFunc(InGetDisplacement)
		, GetVectorDisplacementFunc(InGetVectorDisplacement)
		, GetErrorBoundsFunc(InGetErrorBounds)
		, GetNumSamplesFunc(InGetNumSamples)
	{
	}

	FVector3f GetVertexDisplacement(const int32 VertexIndex, const int32 TriIndex) const
	{
		return GetDisplacementFunc(FVector3f(1.f, 0.f, 0.f), Mesh.Verts[VertexIndex], Mesh.Verts[VertexIndex], Mesh.Verts[VertexIndex], Mesh.MaterialIndexes[TriIndex]);
	}

	FVector3f GetDisplacement(const FVector3f Barycentrics, const int TriIndex) const
	{
		const FIndex3i Triangle = Mesh.GetTriangle(TriIndex);
		return GetDisplacementFunc(Barycentrics, Mesh.Verts[Triangle.A], Mesh.Verts[Triangle.B], Mesh.Verts[Triangle.C], Mesh.MaterialIndexes[TriIndex]);
	}

	FVector2f GetErrorBounds(const FVector3f* const Barycentrics, const FVector3f Displacement0, const FVector3f Displacement1, const FVector3f Displacement2, const int TriIndex )
	{
		return GetErrorBoundsFunc( Barycentrics, 
			                       Mesh.Verts[Mesh.GetVertexIndex(TriIndex, 0)], 
							       Mesh.Verts[Mesh.GetVertexIndex(TriIndex, 1)],
							       Mesh.Verts[Mesh.GetVertexIndex(TriIndex, 2)],
								   Displacement0, Displacement1, Displacement2,
								   Mesh.MaterialIndexes[TriIndex] );
	}

	int32 GetNumSamples(const FVector3f* const Barycentrics, const FIndex3i& Triangle, const int TriIndex) const
	{
		return GetNumSamplesFunc(Barycentrics, Mesh.Verts[Triangle.A], Mesh.Verts[Triangle.B], Mesh.Verts[Triangle.C], Mesh.MaterialIndexes[TriIndex]);
	}

private:
	const FMinimalMesh&        Mesh;
	FDispFunc	               GetDisplacementFunc;
	FVectorDispFunc            GetVectorDisplacementFunc;
	FBoundsFunc	               GetErrorBoundsFunc;
	FNumFunc	               GetNumSamplesFunc;
};

} // namespace Nanite
