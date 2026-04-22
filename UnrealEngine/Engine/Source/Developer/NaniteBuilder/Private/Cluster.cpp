// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster.h"
#include "GraphPartitioner.h"
#include "NaniteRayTracingScene.h"
#include "Rasterizer.h"
#include "VectorUtil.h"
#include "ClusterDAG.h"
#include "NaniteBuilder.h"
#include "MonteCarlo.h"

#include "MeshUtilitiesCommon.h"

namespace Nanite
{

template< bool bHasTangents, bool bHasColors >
void CorrectAttributes( float* Attributes )
{
	float* AttributesPtr = Attributes;

	FVector3f& Normal = *reinterpret_cast< FVector3f* >( AttributesPtr );
	Normal.Normalize();
	AttributesPtr += 3;

	if( bHasTangents )
	{
		FVector3f& TangentX = *reinterpret_cast< FVector3f* >( AttributesPtr );
		AttributesPtr += 3;
	
		TangentX -= ( TangentX | Normal ) * Normal;
		TangentX.Normalize();

		float& TangentYSign = *AttributesPtr++;
		TangentYSign = TangentYSign < 0.0f ? -1.0f : 1.0f;
	}

	if( bHasColors )
	{
		FLinearColor& Color = *reinterpret_cast< FLinearColor* >( AttributesPtr );
		AttributesPtr += 3;

		Color = Color.GetClamped();
	}
}

typedef void (CorrectAttributesFunction)( float* Attributes );

static CorrectAttributesFunction* CorrectAttributesFunctions[ 2 ][ 2 ] =	// [ bHasTangents ][ bHasColors ]
{
	{	CorrectAttributes<false, false>,	CorrectAttributes<false, true>	},
	{	CorrectAttributes<true, false>,		CorrectAttributes<true, true>	}
};

FCluster::FCluster(
	const FConstMeshBuildVertexView& InVerts,
	TArrayView< const uint32 > InIndexes,
	TArrayView< const int32 > InMaterialIndexes,
	const FVertexFormat& InFormat,
	uint32 Begin, uint32 End,
	TArrayView< const uint32 > SortedIndexes,
	TArrayView< const uint32 > SortedTo,
	const FAdjacency& Adjacency )
{
	Verts.Format = InFormat;
	Verts.InitFormat();

	GUID = (uint64(Begin) << 32) | End;
	
	NumTris = End - Begin;

	Verts.Reserve( NumTris );
	Indexes.Reserve( 3 * NumTris );
	MaterialIndexes.Reserve( NumTris );
	ExternalEdges.Reserve( 3 * NumTris );
	NumExternalEdges = 0;

	check(InMaterialIndexes.Num() * 3 == InIndexes.Num());

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( NumTris );

	for( uint32 i = Begin; i < End; i++ )
	{
		uint32 TriIndex = SortedIndexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = InIndexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				NewIndex = Verts.AddUninitialized();
				OldToNewIndex.Add( OldIndex, NewIndex );

				Verts.GetPosition( NewIndex ) = InVerts.Position[OldIndex];
				Verts.GetNormal( NewIndex ) = InVerts.TangentZ[OldIndex];

				if( Verts.Format.bHasTangents )
				{
					const float TangentYSign = ((InVerts.TangentZ[OldIndex] ^ InVerts.TangentX[OldIndex]) | InVerts.TangentY[OldIndex]);
					Verts.GetTangentX( NewIndex ) = InVerts.TangentX[OldIndex];
					Verts.GetTangentYSign( NewIndex ) = TangentYSign < 0.0f ? -1.0f : 1.0f;
				}
	
				if( Verts.Format.bHasColors )
				{
					Verts.GetColor( NewIndex ) = InVerts.Color[OldIndex].ReinterpretAsLinear();
				}

				if( Verts.Format.NumTexCoords > 0 )
				{
					FVector2f* UVs = Verts.GetUVs( NewIndex );
					for( uint32 UVIndex = 0; UVIndex < Verts.Format.NumTexCoords; UVIndex++ )
					{
						UVs[UVIndex] = InVerts.UVs[UVIndex][OldIndex];
					}
				}

				if( Verts.Format.NumBoneInfluences > 0 )
				{
					FVector2f* BoneInfluences = Verts.GetBoneInfluences(NewIndex);
					for (uint32 Influence = 0; Influence < Verts.Format.NumBoneInfluences; Influence++)
					{
						BoneInfluences[Influence].X = InVerts.BoneIndices[Influence][OldIndex];
						BoneInfluences[Influence].Y = InVerts.BoneWeights[Influence][OldIndex];
					}
				}
			}

			Indexes.Add( NewIndex );

			int32 EdgeIndex = TriIndex * 3 + k;
			int32 AdjCount = 0;
			
			Adjacency.ForAll( EdgeIndex,
				[ &AdjCount, Begin, End, &SortedTo ]( int32 EdgeIndex, int32 AdjIndex )
				{
					uint32 AdjTri = SortedTo[ AdjIndex / 3 ];
					if( AdjTri < Begin || AdjTri >= End )
						AdjCount++;
				} );

			ExternalEdges.Add( (int8)AdjCount );
			NumExternalEdges += AdjCount != 0 ? 1 : 0;
		}

		MaterialIndexes.Add( InMaterialIndexes[ TriIndex ] );
	}

	Verts.Sanitize();

	for( uint32 VertIndex = 0; VertIndex < Verts.Num(); VertIndex++ )
	{
		// Make sure this vertex is valid from the start
		CorrectAttributesFunctions[ Verts.Format.bHasTangents ][ Verts.Format.bHasColors ]( Verts.GetAttributes( VertIndex ) );
	}

	Bound();
}

// Split
FCluster::FCluster(
	FCluster& SrcCluster,
	uint32 Begin, uint32 End,
	TArrayView< const uint32 > SortedIndexes,
	TArrayView< const uint32 > SortedTo,
	const FAdjacency& Adjacency )
	: MipLevel( SrcCluster.MipLevel )
{
	GUID = Murmur64( { SrcCluster.GUID, (uint64)Begin, (uint64)End } );

	uint32 NumElements = End - Begin;
	check( NumElements <= ClusterSize );

	Verts.Format = SrcCluster.Verts.Format;
	Verts.InitFormat();
	
	if( SrcCluster.NumTris )
	{
		NumTris = NumElements;
	
		Verts.Reserve( NumElements );
		Indexes.Reserve( 3 * NumElements );
		MaterialIndexes.Reserve( NumElements );
		ExternalEdges.Reserve( 3 * NumElements );
		NumExternalEdges = 0;
	
		TMap< uint32, uint32 > OldToNewIndex;
		OldToNewIndex.Reserve( NumTris );
	
		for( uint32 i = Begin; i < End; i++ )
		{
			uint32 TriIndex = SortedIndexes[i];
	
			for( uint32 k = 0; k < 3; k++ )
			{
				uint32 OldIndex = SrcCluster.Indexes[ TriIndex * 3 + k ];
				uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
				uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;
	
				if( NewIndex == ~0u )
				{
					NewIndex = Verts.Add( &SrcCluster.Verts.GetPosition( OldIndex ) );
					OldToNewIndex.Add( OldIndex, NewIndex );
				}
	
				Indexes.Add( NewIndex );
	
				int32 EdgeIndex = TriIndex * 3 + k;
				int32 AdjCount = SrcCluster.ExternalEdges[ EdgeIndex ];
				
				Adjacency.ForAll( EdgeIndex,
					[ &AdjCount, Begin, End, &SortedTo ]( int32 EdgeIndex, int32 AdjIndex )
					{
						uint32 AdjTri = SortedTo[ AdjIndex / 3 ];
						if( AdjTri < Begin || AdjTri >= End )
							AdjCount++;
					} );
	
				ExternalEdges.Add( (int8)AdjCount );
				NumExternalEdges += AdjCount != 0 ? 1 : 0;
			}
	
			MaterialIndexes.Add( SrcCluster.MaterialIndexes[ TriIndex ] );
		}
	}
	else
	{
		Verts.Reserve( NumElements );
		MaterialIndexes.Reserve( NumElements );

		for( uint32 i = Begin; i < End; i++ )
		{
			uint32 BrickIndex = SortedIndexes[i];

			FBrick Brick = SrcCluster.Bricks[ BrickIndex ];
			uint32 NumVoxels = FMath::CountBits( Brick.VoxelMask );

			uint32 OldIndex = Brick.VertOffset;
			uint32 NewIndex = Brick.VertOffset = Verts.Add( &SrcCluster.Verts.GetPosition( OldIndex ), NumVoxels );

			Bricks.Add( Brick );
			MaterialIndexes.Add( SrcCluster.MaterialIndexes[ BrickIndex ] );
		}
	}

	Bound();
	check( MaterialIndexes.Num() > 0 );
}

// Merge triangles
FCluster::FCluster( const FClusterDAG& DAG, TArrayView< const FClusterRef > Children )
{
	uint32 NumVertsGuess = 0;
	for( FClusterRef ChildRef : Children )
	{
		const FCluster& Child = ChildRef.GetCluster( DAG );

		const uint8 NumBoneInfluences = ChildRef.IsInstance() ?
			uint8(DAG.AssemblyInstanceData[ ChildRef.InstanceIndex ].NumBoneInfluences) :
			Child.Verts.Format.NumBoneInfluences;

		Verts.Format.NumTexCoords		= FMath::Max( Verts.Format.NumTexCoords,		Child.Verts.Format.NumTexCoords );
		Verts.Format.NumBoneInfluences	= FMath::Max( Verts.Format.NumBoneInfluences,	NumBoneInfluences );
		Verts.Format.bHasTangents		|= Child.Verts.Format.bHasTangents;
		Verts.Format.bHasColors			|= Child.Verts.Format.bHasColors;

		if( Child.NumTris == 0 )
		{
			// VOXELTODO - improve representation of voxels
			// currently: one triangle per voxel brick
			NumVertsGuess	+= 3 * Child.Bricks.Num();
			NumTris			+= Child.Bricks.Num();

			if ( Child.Bricks.Num() > 0 )
			{
				bHasVoxelTriangles = true;
			}
		}
		else
		{
			NumVertsGuess	+= Child.Verts.Num();
			NumTris			+= Child.NumTris;
		}

		// Can jump multiple levels but guarantee it steps at least 1.
		MipLevel	= FMath::Max( MipLevel,		Child.MipLevel + 1 );

		GUID = Murmur64( { GUID, Child.GUID } );
	}
	if( NumTris == 0 )
		return;

	Verts.InitFormat();

	Verts.Reserve( NumVertsGuess );
	Indexes.Reserve( 3 * NumTris );
	MaterialIndexes.Reserve( NumTris );
	ExternalEdges.Reserve( 3 * NumTris );

	VoxelTriangle.Init( false, NumTris );

	uint32 TriangleIndex = 0;

	FHashTable VertHashTable( 1 << FMath::FloorLog2( NumVertsGuess ), NumVertsGuess );

	uint32 VoxelSeed = 0;
	const bool bAllowVoxels = DAG.Settings.ShapePreservation == ENaniteShapePreservation::Voxelize;

	for( FClusterRef ChildRef : Children )
	{
		const FCluster& Child = ChildRef.GetCluster( DAG );

		// VOXELTODO - improve representation of voxels
		// currently: one triangle per voxel brick
		auto BrickToTriangle = [&]( uint32 BrickIndex )
		{
			const FBrick& Brick = Child.Bricks[ BrickIndex ];

			const uint32 NumVoxels = FMath::CountBits( Brick.VoxelMask );

			FVector3f AvgPosition( 0.0f );
			for( uint32 VertIndex = Brick.VertOffset; VertIndex < Brick.VertOffset + NumVoxels; VertIndex++ )
			{
				AvgPosition += Child.Verts.GetPosition( VertIndex );
			}
			AvgPosition /= float(NumVoxels);

			// pick random voxel from brick
			const uint32 VertIndex = Brick.VertOffset + EvolveSobolSeed(VoxelSeed) % NumVoxels;

			uint32 NewIndex = Verts.AddUninitialized(3);
			Verts.GetPosition( NewIndex ) = Child.Verts.GetPosition( VertIndex );
			CopyAttributes( NewIndex, VertIndex, Child );

			FMemory::Memcpy( &Verts.GetPosition( NewIndex + 1 ), &Verts.GetPosition( NewIndex ), Verts.GetVertSize() * sizeof( float ) );
			FMemory::Memcpy( &Verts.GetPosition( NewIndex + 2 ), &Verts.GetPosition( NewIndex ), Verts.GetVertSize() * sizeof( float ) );

			FVector3f N	= Verts.GetNormal( NewIndex );

			if( DAG.Settings.bVoxelNDF )
			{
				uint32 NormalSeed = 0;

				// It would nice to sample the NDF instead of VNDF but don't have a way for getting unweighted samples for that.
				FVector3f V = UniformSampleSphere( SobolSampler( BrickIndex, NormalSeed ) );

				float NDF = Verts.GetColor( NewIndex ).A;

				FVector3f Alpha;
				if( NDF > 0.5f )
					Alpha = FVector3f( 1.0f, 1.0f, 2.0f - 2.0f * NDF );
				else
					Alpha = FVector3f( 2.0f * NDF, 2.0f * NDF, 1.0f );

				FMatrix44f TangentToWorld = GetTangentBasisFrisvad( Verts.GetNormal( NewIndex ) );

				FVector3f TangentV	= TangentToWorld.GetTransposed().TransformVector( V );
				FVector3f SphereV	= ( TangentV * Alpha ).GetSafeNormal();
				FVector3f SphereN	= ( V + UniformSampleSphere( SobolSampler( BrickIndex, NormalSeed ) ) ).GetSafeNormal();
				FVector3f TangentN	= ( SphereN * Alpha ).GetSafeNormal();
				N					= TangentToWorld.GetTransposed().TransformVector( TangentN );
			}

			FMatrix44f TriBasis = GetTangentBasisFrisvad( N );

			// Random spin
			float E = ( EvolveSobolSeed( VoxelSeed ) >> 8 ) * 5.96046447754e-08f; // * 2^-24
			float Theta = 2.0f * PI * E;
			float SinTheta, CosTheta;
			FMath::SinCos( &SinTheta, &CosTheta, Theta );

			FVector3f X = TriBasis.TransformVector( FVector3f(  CosTheta, SinTheta, 0.0f ) );
			FVector3f Y = TriBasis.TransformVector( FVector3f( -SinTheta, CosTheta, 0.0f ) );

			float TriArea = 4.0f * float(NumVoxels);	// inverse average projected area
			float TriScale = Child.LODError * FMath::Sqrt( TriArea );
			X *= TriScale;
			Y *= TriScale;

			Verts.GetPosition( NewIndex + 0 ) = AvgPosition - (1.0f / 3.0f) * X - (1.0f / 3.0f) * Y;
			Verts.GetPosition( NewIndex + 1 ) = AvgPosition - (1.0f / 3.0f) * X + (2.0f / 3.0f) * Y;
			Verts.GetPosition( NewIndex + 2 ) = AvgPosition + (2.0f / 3.0f) * X - (1.0f / 3.0f) * Y;
					
			Indexes.Add( NewIndex + 0 );
			Indexes.Add( NewIndex + 1 );
			Indexes.Add( NewIndex + 2 );
					
			MaterialIndexes.Add( Child.MaterialIndexes[ BrickIndex ] );

			VoxelTriangle[ TriangleIndex++ ] = true;

			return NewIndex;
		};

		if( ChildRef.IsInstance() )
		{
			const FMatrix44f& Transform = ChildRef.GetTransform( DAG );
			const FMatrix44f NormalTransform = Transform.GetMatrixWithoutScale();

			// TODO Recalculate instead?
			const float MaxScale = Transform.GetScaleVector().GetMax();
			Bounds		+= Child.Bounds.TransformBy( Transform );
			SurfaceArea	+= Child.SurfaceArea * FMath::Square(MaxScale);

			auto TransformVert = [&]( uint32 VertIndex )
			{
				FVector3f& Position = Verts.GetPosition( VertIndex );
				Position = Transform.TransformPosition( Position );

				FVector3f& Normal = Verts.GetNormal( VertIndex );
				Normal = NormalTransform.TransformVector( Normal );
				if( Verts.Format.bHasTangents )
				{
					FVector3f& TangentX = Verts.GetTangentX( VertIndex );
					TangentX = NormalTransform.TransformVector( TangentX );
					// ASSEMBLYTODO GetTangentYSign needs to negate if transform determinent is <0
				}

				// Instanced clusters' verts receive their assembly part's influences when being transformed to local
				const FAssemblyInstanceData& InstanceData = DAG.AssemblyInstanceData[ ChildRef.InstanceIndex ];
				if (InstanceData.NumBoneInfluences > 0)
				{
					check( Verts.Format.NumBoneInfluences >= InstanceData.NumBoneInfluences );
					FMemory::Memcpy(
						Verts.GetBoneInfluences( VertIndex ),
						&DAG.AssemblyBoneInfluences[ InstanceData.FirstBoneInfluence ],
						InstanceData.NumBoneInfluences * sizeof( FVector2f ));
				}
				FMemory::Memzero(
					Verts.GetBoneInfluences( VertIndex ) + InstanceData.NumBoneInfluences,
					(Verts.Format.NumBoneInfluences - InstanceData.NumBoneInfluences) * sizeof( FVector2f ) );

				CorrectAttributesFunctions[ Verts.Format.bHasTangents ][ Verts.Format.bHasColors ]( Verts.GetAttributes( VertIndex ) );
			};

			if( bAllowVoxels && Child.NumTris == 0 )
			{
				for( int32 BrickIndex = 0; BrickIndex < Child.Bricks.Num(); BrickIndex++ )
				{
					uint32 NewIndex = BrickToTriangle( BrickIndex );

					TransformVert( NewIndex + 0 );
					TransformVert( NewIndex + 1 );
					TransformVert( NewIndex + 2 );
				}
			}
			else
			{
				for( int32 i = 0; i < Child.Indexes.Num(); i++ )
				{
					uint32 NewIndex = AddVertMismatched( Child.Indexes[i], Child, VertHashTable, TransformVert );

					Indexes.Add( NewIndex );
				}
			}

			TriangleIndex += Child.NumTris;
		}
		else
		{
			Bounds		+= Child.Bounds;
			SurfaceArea	+= Child.SurfaceArea;

			if( bAllowVoxels && Child.NumTris == 0 )
			{
				for( int32 BrickIndex = 0; BrickIndex < Child.Bricks.Num(); BrickIndex++ )
				{
					BrickToTriangle( BrickIndex );
				}
			}
			else if( Verts.Format.Matches( Child.Verts.Format ) )
			{
				for( int32 i = 0; i < Child.Indexes.Num(); i++ )
				{
					const float* ChildVert = (float*)&Child.Verts.GetPosition( Child.Indexes[i] );

					uint32 NewIndex = Verts.FindOrAddHash( ChildVert, Verts.Num(), VertHashTable );
					if( NewIndex == Verts.Num() )
					{
						Verts.Add( ChildVert );
					}

					Indexes.Add( NewIndex );
				}
			}
			else
			{
				for( int32 i = 0; i < Child.Indexes.Num(); i++ )
				{
					uint32 NewIndex = AddVertMismatched( Child.Indexes[i], Child, VertHashTable, [&]( uint32 NewIndex ){} );

					Indexes.Add( NewIndex );
				}
			}

			TriangleIndex += Child.NumTris;
		}

		ExternalEdges.Append( Child.ExternalEdges );

		if( Child.NumTris > 0 )
		{
			MaterialIndexes.Append( Child.MaterialIndexes );
		}
	}

	// TODO Clear ExternalEdges when this cluster contains all triangles for this level.
	FAdjacency Adjacency = BuildAdjacency();

	int32 ChildIndex = 0;
	int32 MinIndex = 0;
	int32 MaxIndex = Children[0].GetCluster( DAG ).ExternalEdges.Num();

	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		if( EdgeIndex >= MaxIndex )
		{
			ChildIndex++;
			MinIndex = MaxIndex;
			MaxIndex += Children[ ChildIndex ].GetCluster( DAG ).ExternalEdges.Num();
		}

		int32 AdjCount = ExternalEdges[ EdgeIndex ];

		Adjacency.ForAll( EdgeIndex,
			[ &AdjCount, MinIndex, MaxIndex ]( int32 EdgeIndex, int32 AdjIndex )
			{
				if( AdjIndex < MinIndex || AdjIndex >= MaxIndex )
					AdjCount--;
			} );

		// This seems like a sloppy workaround for a bug elsewhere but it is possible an interior edge is moved during simplification to
		// match another cluster and it isn't reflected in this count. Sounds unlikely but any hole closing could do this.
		// The only way to catch it would be to rebuild full adjacency after every pass which isn't practical.
		AdjCount = FMath::Max( AdjCount, 0 );

		ExternalEdges[ EdgeIndex ] = (int8)AdjCount;
		NumExternalEdges += AdjCount != 0 ? 1 : 0;
	}

	ensure( NumTris == Indexes.Num() / 3 );
	check( MaterialIndexes.Num() > 0 );
}

float FCluster::Simplify( const FClusterDAG& DAG, uint32 TargetNumTris, float TargetError, uint32 LimitNumTris, const FRayTracingFallbackBuildSettings* RayTracingFallbackBuildSettings )
{
	if( ( TargetNumTris >= NumTris && TargetError == 0.0f ) || LimitNumTris >= NumTris )
	{
		return 0.0f;
	}

	float UVArea[ MAX_STATIC_TEXCOORDS ] = { 0.0f };
	if( Verts.Format.NumTexCoords > 0 )
	{
		for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
		{
			uint32 Index0 = Indexes[ TriIndex * 3 + 0 ];
			uint32 Index1 = Indexes[ TriIndex * 3 + 1 ];
			uint32 Index2 = Indexes[ TriIndex * 3 + 2 ];

			FVector2f* UV0 = Verts.GetUVs( Index0 );
			FVector2f* UV1 = Verts.GetUVs( Index1 );
			FVector2f* UV2 = Verts.GetUVs( Index2 );

			for( uint32 UVIndex = 0; UVIndex < Verts.Format.NumTexCoords; UVIndex++ )
			{
				FVector2f EdgeUV1 = UV1[ UVIndex ] - UV0[ UVIndex ];
				FVector2f EdgeUV2 = UV2[ UVIndex ] - UV0[ UVIndex ];
				float SignedArea = 0.5f * ( EdgeUV1 ^ EdgeUV2 );
				UVArea[ UVIndex ] += FMath::Abs( SignedArea );

				// Force an attribute discontinuity for UV mirroring edges.
				// Quadric could account for this but requires much larger UV weights which raises error on meshes which have no visible issues otherwise.
				MaterialIndexes[ TriIndex ] |= ( SignedArea >= 0.0f ? 1 : 0 ) << ( UVIndex + 24 );
			}
		}
	}

	float TriangleSize = FMath::Sqrt( SurfaceArea / (float)NumTris );
	
	FFloat32 CurrentSize( FMath::Max( TriangleSize, THRESH_POINTS_ARE_SAME ) );
	FFloat32 DesiredSize( 0.25f );
	FFloat32 FloatScale( 1.0f );

	// Lossless scaling by only changing the float exponent.
	int32 Exponent = FMath::Clamp( (int)DesiredSize.Components.Exponent - (int)CurrentSize.Components.Exponent, -126, 127 );
	FloatScale.Components.Exponent = Exponent + 127;	//ExpBias
	// Scale ~= DesiredSize / CurrentSize
	float PositionScale = FloatScale.FloatValue;

	for( uint32 i = 0; i < Verts.Num(); i++ )
	{
		Verts.GetPosition(i) *= PositionScale;
	}
	TargetError *= PositionScale;

	uint32 NumAttributes = Verts.GetVertSize() - 3;
	float* AttributeWeights = (float*)FMemory_Alloca( NumAttributes * sizeof( float ) );
	float* WeightsPtr = AttributeWeights;

	// Normal
	*WeightsPtr++ = 1.0f;
	*WeightsPtr++ = 1.0f;
	*WeightsPtr++ = 1.0f;

	if( Verts.Format.bHasTangents )
	{
		// Tangent X
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;

		// Tangent Y Sign
		*WeightsPtr++ = 0.5f;
	}

	if( Verts.Format.bHasColors )
	{
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
		*WeightsPtr++ = 0.0625f;
	}

	// Normalize UVWeights
	for( uint32 UVIndex = 0; UVIndex < Verts.Format.NumTexCoords; UVIndex++ )
	{
		float UVWeight = 0.0f;
		if( DAG.Settings.bLerpUVs )
		{
			float TriangleUVSize = FMath::Sqrt( UVArea[UVIndex] / (float)NumTris );
			TriangleUVSize = FMath::Max( TriangleUVSize, THRESH_UVS_ARE_SAME );
			UVWeight =  1.0f / ( 128.0f * TriangleUVSize );
		}
		*WeightsPtr++ = UVWeight;
		*WeightsPtr++ = UVWeight;
	}

	for (uint32 Influence = 0; Influence < Verts.Format.NumBoneInfluences; Influence++)
	{
		// Set all bone index/weight values to 0.0 so that the closest
		// original vertex to the new position will copy its data wholesale.
		// Similar to the !bLerpUV path, but always used for skinning data.
		float InfluenceWeight = 0.0f;

		*WeightsPtr++ = InfluenceWeight; // Bone index
		*WeightsPtr++ = InfluenceWeight; // Bone weight
	}

	check( ( WeightsPtr - AttributeWeights ) == NumAttributes );

	FMeshSimplifier Simplifier( Verts.Array.GetData(), Verts.Num(), Indexes.GetData(), Indexes.Num(), MaterialIndexes.GetData(), NumAttributes );

	TMap< TTuple< FVector3f, FVector3f >, int8 > LockedEdges;

	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		if( ExternalEdges[ EdgeIndex ] )
		{
			uint32 VertIndex0 = Indexes[ EdgeIndex ];
			uint32 VertIndex1 = Indexes[ Cycle3( EdgeIndex ) ];
	
			const FVector3f& Position0 = Verts.GetPosition( VertIndex0 );
			const FVector3f& Position1 = Verts.GetPosition( VertIndex1 );

			Simplifier.LockPosition( Position0 );
			Simplifier.LockPosition( Position1 );

			LockedEdges.Add( MakeTuple( Position0, Position1 ), ExternalEdges[ EdgeIndex ] );
		}
	}

	Simplifier.SetAttributeWeights( AttributeWeights );
	Simplifier.SetCorrectAttributes( CorrectAttributesFunctions[ Verts.Format.bHasTangents ][ Verts.Format.bHasColors ] );
	Simplifier.SetEdgeWeight( 2.0f );
	Simplifier.SetMaxEdgeLengthFactor( DAG.Settings.MaxEdgeLengthFactor );

	float MaxErrorSqr = Simplifier.Simplify(
		Verts.Num(), TargetNumTris, FMath::Square( TargetError ),
		0, LimitNumTris, MAX_flt );

	check( Simplifier.GetRemainingNumVerts() > 0 );
	check( Simplifier.GetRemainingNumTris() > 0 );

	if ( RayTracingFallbackBuildSettings && RayTracingFallbackBuildSettings->FoliageOverOcclusionBias > 0.0f )
	{
		if ( bHasVoxelTriangles )
		{
			Simplifier.ShrinkVoxelTriangles( RayTracingFallbackBuildSettings->FoliageOverOcclusionBias, VoxelTriangle );
		}
		else
		{
			Simplifier.ShrinkTriGroupWithMostSurfaceAreaLoss( RayTracingFallbackBuildSettings->FoliageOverOcclusionBias );
		}
	}
	else if( DAG.Settings.ShapePreservation == ENaniteShapePreservation::PreserveArea )
	{
		Simplifier.PreserveSurfaceArea();
	}

	Simplifier.Compact();
	
	Verts.SetNum( Simplifier.GetRemainingNumVerts() );
	Indexes.SetNum( Simplifier.GetRemainingNumTris() * 3 );
	MaterialIndexes.SetNum( Simplifier.GetRemainingNumTris() );
	ExternalEdges.Init( 0, Simplifier.GetRemainingNumTris() * 3 );

	NumTris = Simplifier.GetRemainingNumTris();

	NumExternalEdges = 0;
	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		auto Edge = MakeTuple(
			Verts.GetPosition( Indexes[ EdgeIndex ] ),
			Verts.GetPosition( Indexes[ Cycle3( EdgeIndex ) ] )
		);
		int8* AdjCount = LockedEdges.Find( Edge );
		if( AdjCount )
		{
			ExternalEdges[ EdgeIndex ] = *AdjCount;
			NumExternalEdges++;
		}
	}

	float InvScale = 1.0f / PositionScale;
	for( uint32 i = 0; i < Verts.Num(); i++ )
	{
		Verts.GetPosition(i) *= InvScale;
		Bounds += Verts.GetPosition(i);
	}

	for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
	{
		// Remove UV mirroring bits
		MaterialIndexes[ TriIndex ] &= 0xffffff;
	}

	return FMath::Sqrt( MaxErrorSqr ) * InvScale;
}

void FCluster::Split( FGraphPartitioner& Partitioner, const FAdjacency& Adjacency ) const
{
	FDisjointSet DisjointSet( NumTris );
	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		Adjacency.ForAll( EdgeIndex,
			[ &DisjointSet ]( int32 EdgeIndex0, int32 EdgeIndex1 )
			{
				if( EdgeIndex0 > EdgeIndex1 )
					DisjointSet.UnionSequential( EdgeIndex0 / 3, EdgeIndex1 / 3 );
			} );
	}

	auto GetCenter = [ this ]( uint32 TriIndex )
	{
		FVector3f Center;
		Center  = Verts.GetPosition( Indexes[ TriIndex * 3 + 0 ] );
		Center += Verts.GetPosition( Indexes[ TriIndex * 3 + 1 ] );
		Center += Verts.GetPosition( Indexes[ TriIndex * 3 + 2 ] );
		return Center * (1.0f / 3.0f);
	};

	Partitioner.BuildLocalityLinks( DisjointSet, Bounds, MaterialIndexes, GetCenter );

	auto* RESTRICT Graph = Partitioner.NewGraph( NumTris * 3 );

	for( uint32 i = 0; i < NumTris; i++ )
	{
		Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

		uint32 TriIndex = Partitioner.Indexes[i];

		// Add shared edges
		for( int k = 0; k < 3; k++ )
		{
			Adjacency.ForAll( 3 * TriIndex + k,
				[ &Partitioner, Graph ]( int32 EdgeIndex, int32 AdjIndex )
				{
					Partitioner.AddAdjacency( Graph, AdjIndex / 3, 4 * 65 );
				} );
		}

		Partitioner.AddLocalityLinks( Graph, TriIndex, 1 );
	}
	Graph->AdjacencyOffset[ NumTris ] = Graph->Adjacency.Num();

	Partitioner.PartitionStrict( Graph, false );
}

FAdjacency FCluster::BuildAdjacency() const
{
	FAdjacency Adjacency( Indexes.Num() );
	FEdgeHash EdgeHash( Indexes.Num() );

	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		Adjacency.Direct[ EdgeIndex ] = -1;

		EdgeHash.ForAllMatching( EdgeIndex, true,
			[ this ]( int32 CornerIndex )
			{
				return Verts.GetPosition( Indexes[ CornerIndex ] );
			},
			[&]( int32 EdgeIndex, int32 OtherEdgeIndex )
			{
				Adjacency.Link( EdgeIndex, OtherEdgeIndex );
			} );
	}

	return Adjacency;
}

uint32 FVertexArray::FindOrAddHash( const float* Vert, uint32 VertIndex, FHashTable& HashTable )
{
	const FVector3f& Position = *reinterpret_cast< const FVector3f* >( Vert );

	uint32 Hash = HashPosition( Position );
	uint32 NewIndex;
	for( NewIndex = HashTable.First( Hash ); HashTable.IsValid( NewIndex ); NewIndex = HashTable.Next( NewIndex ) )
	{
		uint32 i;
		for( i = 0; i < VertSize; i++ )
		{
			if( Vert[i] != Array[ NewIndex * VertSize + i ] )
				break;
		}
		if( i == VertSize )
			break;
	}
	if( !HashTable.IsValid( NewIndex ) )
	{
		NewIndex = VertIndex;
		HashTable.Add( Hash, NewIndex );
	}

	return NewIndex;
}

template< typename TTransformFunc >
inline uint32 FCluster::AddVertMismatched(
	uint32 SrcVertIndex,
	const FCluster& SrcCluster,
	FHashTable& HashTable,
	TTransformFunc&& TransformFunc )
{
	// Create a temporary new vertex that will hold copied and default-initialized data
	const uint32 TempIndex = Verts.Num();
	Verts.AddUninitialized();
	Verts.GetPosition( TempIndex ) = SrcCluster.Verts.GetPosition( SrcVertIndex );
	CopyAttributes( TempIndex, SrcVertIndex, SrcCluster );

	TransformFunc( TempIndex );

	uint32 NewIndex = Verts.FindOrAddHash( (float*)&Verts.GetPosition( TempIndex ), TempIndex, HashTable );
	if( NewIndex != TempIndex )
	{
		// Already exists, remove the temporary
		Verts.RemoveAt( TempIndex );
	}

	return NewIndex;
}

void FCluster::CopyAttributes(
	uint32 DstVertIndex,
	uint32 SrcVertIndex,
	const FCluster& SrcCluster )
{
	Verts.CopyAttributes( DstVertIndex, SrcVertIndex, SrcCluster.Verts );
}

void FCluster::LerpAttributes(
	uint32 DstVertIndex,
	uint32 SrcTriIndex,
	const FCluster& SrcCluster,
	const FVector3f& Barycentrics )
{
	FUintVector3 SrcVertIndexes(
		SrcCluster.Indexes[ SrcTriIndex * 3 + 0 ],
		SrcCluster.Indexes[ SrcTriIndex * 3 + 1 ],
		SrcCluster.Indexes[ SrcTriIndex * 3 + 2 ] );

	Verts.LerpAttributes( DstVertIndex, SrcVertIndexes, SrcCluster.Verts, Barycentrics );
}

// TODO move
void FVertexArray::CopyAttributes( 
	uint32 DstVertIndex,
	uint32 SrcVertIndex,
	const FVertexArray& SrcVerts )
{
	if( Format.Matches( SrcVerts.Format ) )
	{
		uint32 AttrSize = VertSize - 3;
		FMemory::Memcpy( GetAttributes( DstVertIndex ), SrcVerts.GetAttributes( SrcVertIndex ), AttrSize * sizeof( float ) );
		return;
	}

	GetNormal( DstVertIndex ) = SrcVerts.GetNormal( SrcVertIndex );

	if( Format.bHasTangents )
	{
		GetTangentX( DstVertIndex )		= SrcVerts.Format.bHasTangents ? SrcVerts.GetTangentX( SrcVertIndex ) : FVector3f(0.0f);
		GetTangentYSign( DstVertIndex )	= SrcVerts.Format.bHasTangents ? SrcVerts.GetTangentYSign( SrcVertIndex ) : 1.0f;
	}

	if( Format.bHasColors )
	{
		GetColor( DstVertIndex ) = SrcVerts.Format.bHasColors ? SrcVerts.GetColor( SrcVertIndex ) : FLinearColor::White;
	}

	const uint32 NumUVsToCopy = FMath::Min( Format.NumTexCoords, SrcVerts.Format.NumTexCoords );
	FMemory::Memcpy( GetUVs( DstVertIndex ), SrcVerts.GetUVs( SrcVertIndex ), NumUVsToCopy * sizeof( FVector2f ) );
	if( Format.NumTexCoords > NumUVsToCopy )
		FMemory::Memzero( GetUVs( DstVertIndex ) + NumUVsToCopy, ( Format.NumTexCoords - NumUVsToCopy ) * sizeof( FVector2f ) );

	const uint32 NumInfluencesToCopy = FMath::Min( Format.NumBoneInfluences, SrcVerts.Format.NumBoneInfluences );
	if( NumInfluencesToCopy > 0 )
		FMemory::Memcpy( GetBoneInfluences( DstVertIndex ), SrcVerts.GetBoneInfluences( SrcVertIndex ), NumInfluencesToCopy * sizeof( FVector2f ) );
	if( Format.NumBoneInfluences > NumInfluencesToCopy )
		FMemory::Memzero( GetBoneInfluences( DstVertIndex ) + NumInfluencesToCopy, ( Format.NumBoneInfluences - NumInfluencesToCopy ) * sizeof( FVector2f ) );
}

void FVertexArray::LerpAttributes(
	uint32 DstVertIndex,
	const FUintVector3& SrcVertIndexes,
	const FVertexArray& SrcVerts,
	const FVector3f& Barycentrics )
{
	check( Format.NumTexCoords		>= SrcVerts.Format.NumTexCoords );
	check( Format.NumBoneInfluences	>= SrcVerts.Format.NumBoneInfluences );

	if( Format.Matches( SrcVerts.Format ) )
	{
		const float* SrcAttributes0 = SrcVerts.GetAttributes( SrcVertIndexes[0] );
		const float* SrcAttributes1 = SrcVerts.GetAttributes( SrcVertIndexes[1] );
		const float* SrcAttributes2 = SrcVerts.GetAttributes( SrcVertIndexes[2] );

		float* DstAttributes = GetAttributes( DstVertIndex );
		uint32 AttrSize = Format.GetBoneInfluenceOffset() - 3;
		for( uint32 i = 0; i < AttrSize; i++ )
		{
			DstAttributes[i] =
				SrcAttributes0[i] * Barycentrics[0] +
				SrcAttributes1[i] * Barycentrics[1] +
				SrcAttributes2[i] * Barycentrics[2];
		}
	}
	else
	{
		GetNormal( DstVertIndex ) =
			SrcVerts.GetNormal( SrcVertIndexes[0] ) * Barycentrics[0] +
			SrcVerts.GetNormal( SrcVertIndexes[1] ) * Barycentrics[1] +
			SrcVerts.GetNormal( SrcVertIndexes[2] ) * Barycentrics[2];

		if( Format.bHasTangents )
		{
			if( SrcVerts.Format.bHasTangents )
			{
				GetTangentX( DstVertIndex ) =
					SrcVerts.GetTangentX( SrcVertIndexes[0] ) * Barycentrics[0] +
					SrcVerts.GetTangentX( SrcVertIndexes[1] ) * Barycentrics[1] +
					SrcVerts.GetTangentX( SrcVertIndexes[2] ) * Barycentrics[2];

				// Need to lerp?
				GetTangentYSign( DstVertIndex ) =
					SrcVerts.GetTangentYSign( SrcVertIndexes[0] ) * Barycentrics[0] +
					SrcVerts.GetTangentYSign( SrcVertIndexes[1] ) * Barycentrics[1] +
					SrcVerts.GetTangentYSign( SrcVertIndexes[2] ) * Barycentrics[2];
			}
			else
			{
				// TODO
				GetTangentX( DstVertIndex ) = FVector3f(0.0f);
				GetTangentYSign( DstVertIndex ) = 1.0f;
			}
		}

		if( Format.bHasColors )
		{
			if( SrcVerts.Format.bHasColors )
			{
				GetColor( DstVertIndex ) =
					SrcVerts.GetColor( SrcVertIndexes[0] ) * Barycentrics[0] +
					SrcVerts.GetColor( SrcVertIndexes[1] ) * Barycentrics[1] +
					SrcVerts.GetColor( SrcVertIndexes[2] ) * Barycentrics[2];
			}
			else
				GetColor( DstVertIndex ) = FLinearColor::White;
		}

		for( uint32 UVIndex = 0; UVIndex < SrcVerts.Format.NumTexCoords; UVIndex++ )
		{
			GetUVs( DstVertIndex )[ UVIndex ] =
				SrcVerts.GetUVs( SrcVertIndexes[0] )[ UVIndex ] * Barycentrics[0] +
				SrcVerts.GetUVs( SrcVertIndexes[1] )[ UVIndex ] * Barycentrics[1] +
				SrcVerts.GetUVs( SrcVertIndexes[2] )[ UVIndex ] * Barycentrics[2];
		}
		FMemory::Memzero( GetUVs( DstVertIndex ) + SrcVerts.Format.NumTexCoords, ( Format.NumTexCoords - SrcVerts.Format.NumTexCoords ) * sizeof( FVector2f ) );
	}

	if( SrcVerts.Format.NumBoneInfluences > 0 )
	{
		// Copy dominant skinning attributes instead of interpolating them
		int32 DomCorner = FMath::Max3Index( Barycentrics[0], Barycentrics[1], Barycentrics[2] );
		uint32 DomIndex = SrcVertIndexes[ DomCorner ];
		FMemory::Memcpy( GetBoneInfluences( DstVertIndex ), SrcVerts.GetBoneInfluences( DomIndex ), SrcVerts.Format.NumBoneInfluences * sizeof( FVector2f ) );
		FMemory::Memzero( GetBoneInfluences( DstVertIndex ) + SrcVerts.Format.NumBoneInfluences, ( Format.NumBoneInfluences - SrcVerts.Format.NumBoneInfluences ) * sizeof( FVector2f ) );
	}
}

void FCluster::Bound()
{
	Bounds = FBounds3f();
	SurfaceArea = 0.0f;
	
	TArray< FVector3f, TInlineAllocator<128> > Positions;
	Positions.SetNum( Verts.Num(), EAllowShrinking::No );

	for( uint32 i = 0; i < Verts.Num(); i++ )
	{
		Positions[i] = Verts.GetPosition(i);
		Bounds += Positions[i];
	}
	SphereBounds = FSphere3f( Positions.GetData(), Positions.Num() );
	LODBounds = SphereBounds;
	
	float MaxEdgeLength2 = 0.0f;
	for( int i = 0; i < Indexes.Num(); i += 3 )
	{
		FVector3f v[3];
		v[0] = Verts.GetPosition( Indexes[ i + 0 ] );
		v[1] = Verts.GetPosition( Indexes[ i + 1 ] );
		v[2] = Verts.GetPosition( Indexes[ i + 2 ] );

		FVector3f Edge01 = v[1] - v[0];
		FVector3f Edge12 = v[2] - v[1];
		FVector3f Edge20 = v[0] - v[2];

		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge01.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge12.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge20.SizeSquared() );

		float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
		SurfaceArea += TriArea;
	}
	EdgeLength = FMath::Sqrt( MaxEdgeLength2 );
}

#if RAY_TRACE_VOXELS

#if 0

// [Loubet and Neyret 2017, "Hybrid mesh-volume LoDs for all-scale pre-filtering of complex 3D assets"]
static void GenerateRay( uint32 SampleIndex, uint32& Seed, FVector3f VoxelCenter, float VoxelSize, FVector3f& Origin, FVector3f& Direction, FVector2f& Time )
{
	Direction = UniformSampleSphere( SobolSampler( SampleIndex, Seed ) );

	Origin = FVector3f( LatticeSampler( SampleIndex, Seed ) ) - 0.5f;
	
	FVector2f Gaussian0 = GaussianSampleDisk( SobolSampler( SampleIndex, Seed ), 0.6f, 1.5f );
	FVector2f Gaussian1 = GaussianSampleDisk( SobolSampler( SampleIndex, Seed ), 0.6f, 1.5f );

	Origin += FVector3f( Gaussian0.X, Gaussian0.Y, Gaussian1.X );
	Origin *= VoxelSize;
	Origin += VoxelCenter;

	Time[0] = 0.0f;
	Time[1] = VoxelSize;
}

#elif 1

static void GenerateRay( uint32 SampleIndex, uint32& Seed, FVector3f VoxelCenter, float VoxelSize, FVector3f& Origin, FVector3f& Direction, FVector2f& Time )
{
	do
	{
		Direction = UniformSampleSphere( SobolSampler( SampleIndex, Seed ) );

		// [ Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ]
		const float Sign = Direction.Z >= 0.0f ? 1.0f : -1.0f;
		const float a = -1.0f / ( Sign + Direction.Z );
		const float b = Direction.X * Direction.Y * a;
	
		FVector3f TangentX( 1.0f +	Sign * a * FMath::Square( Direction.X ), Sign * b, -Sign * Direction.X );
		FVector3f TangentY( b,		Sign + a * FMath::Square( Direction.Y ), -Direction.Y );

		FVector2f Disk = UniformSampleDisk( SobolSampler( SampleIndex, Seed ) );
		Disk *= VoxelSize * 0.5f * UE_SQRT_3;

		Origin  = TangentX * Disk.X;
		Origin += TangentY * Disk.Y;

		// Reject sample if it doesn't hit voxel
		const FVector3f InvDir			= 1.0f / Direction;
		const FVector3f Center			= -Origin * InvDir;
		const FVector3f Extent			= InvDir.GetAbs() * ( VoxelSize * 0.5f );
		const FVector3f MinIntersection = Center - Extent;
		const FVector3f MaxIntersection = Center + Extent;

		Time[0] = MinIntersection.GetMax();
		Time[1] = MaxIntersection.GetMin();
	} while( Time[0] >= Time[1] );

	Origin += VoxelCenter;

	// Force start to zero, negative isn't supported
	Origin += Direction * Time[0];
	Time[1] -= Time[0];
	Time[0] = 0.0f;
}

#else

static void GenerateRay( uint32 SampleIndex, uint32& Seed, FVector3f VoxelCenter, float VoxelSize, FVector3f& Origin, FVector3f& Direction, FVector2f& Time )
{
	//FVector4f Rand = LatticeSampler( SampleIndex, Seed );

	Direction = UniformSampleSphere( SobolSampler( SampleIndex, Seed ) );

	// [ Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ]
	const float Sign = Direction.Z >= 0.0f ? 1.0f : -1.0f;
	const float a = -1.0f / ( Sign + Direction.Z );
	const float b = Direction.X * Direction.Y * a;
	
	FVector3f TangentX( 1.0f +	Sign * a * FMath::Square( Direction.X ), Sign * b, -Sign * Direction.X );
	FVector3f TangentY( b,		Sign + a * FMath::Square( Direction.Y ), -Direction.Y );

	//FVector2f Disk = UniformSampleDisk( FVector2f( Rand.Z, Rand.W ) ) * 0.5f;
	FVector2f Disk = GaussianSampleDisk( SobolSampler( SampleIndex, Seed ), 0.5f, 1.0f );
	Disk *= VoxelSize;

	Origin  = VoxelCenter;
	Origin += TangentX * Disk.X;
	Origin += TangentY * Disk.Y;

	Time[0] = -0.5f * VoxelSize;
	Time[1] = +0.5f * VoxelSize;
}

#endif

static void GenerateRayAligned( uint32 SampleIndex, uint32& Seed, FVector3f VoxelCenter, float VoxelSize, FVector3f& Origin, FVector3f& Direction, FVector2f& Time )
{
	uint32 RandIndex = SampleIndex + EvolveSobolSeed( Seed );
	uint32 Face = RandIndex % 6;
	float Sign = (Face & 1) ? 1.0f : -1.0f;

	const int32 SwizzleZ = Face >> 1;
	const int32 SwizzleX = ( 1 << SwizzleZ ) & 3;
	const int32 SwizzleY = ( 1 << SwizzleX ) & 3;

	FVector2f Sobol = SobolSampler( SampleIndex, Seed );

	Origin = VoxelCenter;
	Origin[ SwizzleX ] += VoxelSize * ( Sobol.X - 1.0f );
	Origin[ SwizzleY ] += VoxelSize * ( Sobol.Y - 1.0f );
	Origin[ SwizzleZ ] -= VoxelSize * 0.5f * Sign;

	Direction[ SwizzleX ] = 0.0f;
	Direction[ SwizzleY ] = 0.0f;
	Direction[ SwizzleZ ] = Sign;

	Time[0] = 0.0f;
	Time[1] = VoxelSize;
}

bool TestCrosshair( const FRayTracingScene& RayTracingScene, const FVector3f& VoxelCenter, float VoxelSize, uint32& HitInstanceIndex, uint32& HitClusterIndex, uint32& HitTriIndex, FVector3f& HitBarycentrics )
{
	FVector2f Time( 0.0f, VoxelSize );
	for( int j = 0; j < 3; j++ )
	{
		FVector3f Origin = VoxelCenter;
		Origin[j] -= 0.5f * VoxelSize;
		FVector3f Direction( 0.0f );
		Direction[j] = 1.0f;

		// TODO use Ray4
		FRay1 Ray = {};
		Ray.SetRay( Origin, Direction, Time );

		RayTracingScene.Intersect1( Ray );
		if( RayTracingScene.GetHit( Ray, HitInstanceIndex, HitClusterIndex, HitTriIndex, HitBarycentrics ) )
			return true;
	}
	return false;
}
#endif

void FCluster::Voxelize( FClusterDAG& DAG, TArrayView< const FClusterRef > Children, float VoxelSize )
{
	if( DAG.Settings.bVoxelNDF || ( DAG.Settings.bVoxelOpacity && DAG.Settings.NumRays > 1 ) )
		Verts.Format.bHasColors = true;

	for( FClusterRef ChildRef : Children )
	{
		const FCluster& Child = ChildRef.GetCluster( DAG );

		const uint8 NumBoneInfluences = ChildRef.IsInstance() ?
			uint8(DAG.AssemblyInstanceData[ ChildRef.InstanceIndex ].NumBoneInfluences) :
			Child.Verts.Format.NumBoneInfluences;

		Verts.Format.NumTexCoords		= FMath::Max( Verts.Format.NumTexCoords,		Child.Verts.Format.NumTexCoords );
		Verts.Format.NumBoneInfluences	= FMath::Max( Verts.Format.NumBoneInfluences,	NumBoneInfluences );
		Verts.Format.bHasTangents		|= Child.Verts.Format.bHasTangents;
		Verts.Format.bHasColors			|= Child.Verts.Format.bHasColors;

		// Can jump multiple levels but guarantee it steps at least 1.
		MipLevel	= FMath::Max( MipLevel,		Child.MipLevel + 1 );

		GUID = Murmur64( { GUID, Child.GUID } );
	}

#if RAY_TRACE_VOXELS
	// We have to take into account the worst-case vertex format of the whole scene
	Verts.Format.NumTexCoords		= FMath::Max( Verts.Format.NumTexCoords,		DAG.MaxTexCoords );
	Verts.Format.NumBoneInfluences	= FMath::Max( Verts.Format.NumBoneInfluences,	DAG.MaxBoneInfluences );
	Verts.Format.bHasTangents		|= DAG.bHasTangents;
	Verts.Format.bHasColors			|= DAG.bHasColors;
#endif

	Verts.InitFormat();

	TMap< FIntVector3, uint32 > VoxelMap;

	check( VoxelSize > 0.0f );
	const float RcpVoxelSize = 1.0f / VoxelSize;

	for( FClusterRef ChildRef : Children )
	{
		const FCluster& Child = ChildRef.GetCluster( DAG );

		FMatrix44f Transform = FMatrix44f::Identity;
		bool bTransform = ChildRef.IsInstance();
		if( bTransform )
		{
			Transform = ChildRef.GetTransform( DAG );
		}

		if( Child.NumTris )
		{
			for( uint32 TriIndex = 0; TriIndex < Child.NumTris; TriIndex++ )
			{
				FVector3f Triangle[3];
				for( int k = 0; k < 3; k++ )
				{
					Triangle[k] = Child.Verts.GetPosition( Child.Indexes[ TriIndex * 3 + k ] );
					if( bTransform )
						Triangle[k] = Transform.TransformPosition( Triangle[k] );
					Triangle[k] *= RcpVoxelSize;
				}

				VoxelizeTri26( Triangle,
					[&]( const FIntVector3& Voxel, const FVector3f& Barycentrics )
					{
						VoxelMap.FindOrAdd( Voxel, VoxelMap.Num() );
					} );
			}
		}
		else
		{
			auto AddVoxels = [&]( const FVector3f& Center, float Extent )
			{
				FBounds3f VoxelBounds = { Center - Extent, Center + Extent };
				if( bTransform )
					VoxelBounds = VoxelBounds.TransformBy( Transform );

				const FIntVector3 MinVoxel = FloorToInt( FVector3f( VoxelBounds.Min ) * RcpVoxelSize );
				const FIntVector3 MaxVoxel = FloorToInt( FVector3f( VoxelBounds.Max ) * RcpVoxelSize );

				for( int32 z = MinVoxel.Z; z <= MaxVoxel.Z; z++ )
				{
					for( int32 y = MinVoxel.Y; y <= MaxVoxel.Y; y++ )
					{
						for( int32 x = MinVoxel.X; x <= MaxVoxel.X; x++ )
						{
							VoxelMap.FindOrAdd( FIntVector3(x,y,z), VoxelMap.Num() );
						}
					}
				}
			};

			for( int32 BrickIndex = 0; BrickIndex < Child.Bricks.Num(); BrickIndex++ )
			{
				int32 MaterialIndex = Child.MaterialIndexes[ BrickIndex ];

				uint32 NumVoxels = FMath::CountBits( Child.Bricks[ BrickIndex ].VoxelMask );
				for( uint32 i = 0; i < NumVoxels; i++ )
				{
					uint32 VertIndex = Child.Bricks[ BrickIndex ].VertOffset + i;

					AddVoxels( Child.Verts.GetPosition( VertIndex ), Child.LODError * 0.5f );
				}
			}

#if RAY_TRACE_VOXELS
			for( const FIntVector3& Voxel : Child.ExtraVoxels )
			{
				AddVoxels( ( Voxel + 0.5f ) * Child.LODError, Child.LODError * 0.5f );
			}
#endif
		}
	}

#if RAY_TRACE_VOXELS
	const FRayTracingScene& RayTracingScene = DAG.RayTracingScene;

	check( ExtraVoxels.Num() == 0 );

	const float RayBackUp = VoxelSize * DAG.Settings.RayBackUp;

	const uint32 NumRayPackets = FMath::DivideAndRoundUp( DAG.Settings.NumRays, 16u );
	const uint32 NumRayVariations = 64;

	TArray< FRay16 > Rays;
	if( DAG.Settings.NumRays > 1 )
	{
		Rays.AddZeroed( NumRayPackets * NumRayVariations );

		for( int32 i = 0; i < Rays.Num(); i++ )
		{
			for( uint32 j = 0; j < 16; j++ )
			{
				uint32 SampleIndex = ReverseBits( 16 * i + j );
				uint32 Seed = 0;

				FVector3f Origin;
				FVector3f Direction;
				FVector2f Time;
				if( DAG.Settings.bSeparable )
				{
					GenerateRayAligned( SampleIndex, Seed, FVector3f( 0.0f ), VoxelSize, Origin, Direction, Time );
						
					Origin -= Direction * VoxelSize;
					Time[1] += VoxelSize * 2.0f;
				}
				else
				{
					GenerateRay( SampleIndex, Seed, FVector3f( 0.0f ), VoxelSize, Origin, Direction, Time );
						
					Origin -= Direction * RayBackUp;
					Time[1] += RayBackUp;
				}

				Rays[i].SetRay( j, Origin, Direction, Time );
			}
		}
	}

	static_assert( NANITE_ASSEMBLY_TRANSFORM_INDEX_BITS <= 25 );
	struct FSampledVoxel
	{
		float			Coverage;	//TEMP
		TSGGX< float >	NDF;

		uint32			ClusterIndex;
		uint32			InstanceIndex	: NANITE_ASSEMBLY_TRANSFORM_INDEX_BITS;
		uint32			TriIndex		: 32 - NANITE_ASSEMBLY_TRANSFORM_INDEX_BITS;
		uint16			Barycentrics[2];
	};

	TArray< FSampledVoxel > SampledVoxels;
	SampledVoxels.AddZeroed( VoxelMap.Num() );

	ParallelFor( TEXT("Nanite.VoxelizeTrace.PF"), VoxelMap.Num(), 4,
		[&]( int32 VoxelIndex )
		{
			FIntVector3 Voxel = VoxelMap.Get( FSetElementId::FromInteger( VoxelIndex ) ).Key;

			FVector3f VoxelCenter = ( Voxel + 0.5f ) * VoxelSize;

			FSampledVoxel& SampledVoxel = SampledVoxels[ VoxelIndex ];

			uint32		HitInstanceIndex = 0;
			uint32		HitClusterIndex = 0;
			uint32		HitTriIndex = 0;
			FVector3f	HitBarycentrics;
			uint16		HitCount = 0;
			uint16		RayCount = 0;
			if( DAG.Settings.NumRays > 1 )
			{
				uint32 HitCountDim[3] = {};
				uint32 RayCountDim[3] = {};

				int32 VaritationIndex = VoxelIndex & ( NumRayVariations - 1 );
				for( uint32 PacketIndex = 0; PacketIndex < NumRayPackets; PacketIndex++ )
				{
					FRay16 Ray16 = Rays[ PacketIndex + VaritationIndex * NumRayPackets ];
					for( int j = 0; j < 16; j++ )
					{
						Ray16.ray.org_x[j] += VoxelCenter.X;
						Ray16.ray.org_y[j] += VoxelCenter.Y;
						Ray16.ray.org_z[j] += VoxelCenter.Z;
					}

					RayTracingScene.Intersect16( Ray16 );
					RayCount += 16;

					for( int j = 0; j < 16; j++ )
					{
						uint32 Dim = FMath::Max3Index( FMath::Abs( Ray16.ray.dir_x[j] ), FMath::Abs( Ray16.ray.dir_y[j] ), FMath::Abs( Ray16.ray.dir_z[j] ) );
						RayCountDim[ Dim ]++;

						if( RayTracingScene.GetHit( Ray16, j, HitInstanceIndex, HitClusterIndex, HitTriIndex, HitBarycentrics ) )
						{
							if( DAG.Settings.bSeparable )
							{
								if( Ray16.ray.tfar[j] < VoxelSize ||
									Ray16.ray.tfar[j] > VoxelSize * 2.0f )
								{
									RayCount--;
									RayCountDim[ Dim ]--;
									continue;
								}
							}
							else if( Ray16.ray.tfar[j] < RayBackUp )
							{
								RayCount--;
								continue;
							}

							HitCount++;
							HitCountDim[ Dim ]++;

							// Sample attributes from hit triangle
							FCluster& HitCluster = DAG.Clusters[ HitClusterIndex ];

							FVector3f HitNormal =
								HitCluster.Verts.GetNormal( HitCluster.Indexes[ HitTriIndex * 3 + 0 ] ) * HitBarycentrics[0] +
								HitCluster.Verts.GetNormal( HitCluster.Indexes[ HitTriIndex * 3 + 1 ] ) * HitBarycentrics[1] +
								HitCluster.Verts.GetNormal( HitCluster.Indexes[ HitTriIndex * 3 + 2 ] ) * HitBarycentrics[2];

							if( HitInstanceIndex != ~0u )
							{
								const FMatrix44f& Transform = DAG.AssemblyInstanceData[ HitInstanceIndex ].Transform;
								HitNormal = Transform.GetMatrixWithoutScale().TransformVector( HitNormal );
							}

							HitNormal.Normalize();
							SampledVoxel.NDF += HitNormal;
						}
					}
				}

				if( DAG.Settings.bSeparable )
				{
					// Force covered if all rays along 1 axis hit something
					if( ( RayCountDim[0] && RayCountDim[0] == HitCountDim[0] ) ||
						( RayCountDim[1] && RayCountDim[1] == HitCountDim[1] ) ||
						( RayCountDim[2] && RayCountDim[2] == HitCountDim[2] ) )
					{
						uint32 Dummy1, Dummy2, Dummy3;
						FVector3f Dummy4;
						if( TestCrosshair( RayTracingScene, VoxelCenter, VoxelSize, Dummy1, Dummy2, Dummy3, Dummy4 ) )
							RayCount = HitCount;
					}
				}
			}
			else
			{
				if( DAG.Settings.bSeparable )
				{
					RayCount++;
					if( TestCrosshair( RayTracingScene, VoxelCenter, VoxelSize, HitInstanceIndex, HitClusterIndex, HitTriIndex, HitBarycentrics ) )
						HitCount++;
				}
				else
				{
					uint32 TileID;
					TileID  = FMath::MortonCode3( Voxel.X & 1023 );
					TileID |= FMath::MortonCode3( Voxel.Y & 1023 ) << 1;
					TileID |= FMath::MortonCode3( Voxel.Z & 1023 ) << 2;

					FRay1 Ray = {};
					{
						uint32 SampleIndex = ReverseBits( TileID );
						uint32 Seed = 0;

						FVector3f Origin;
						FVector3f Direction;
						FVector2f Time;
						GenerateRay( SampleIndex, Seed, VoxelCenter, VoxelSize, Origin, Direction, Time );
						Ray.SetRay( Origin, Direction, Time );
					}

					RayTracingScene.Intersect1( Ray );
					RayCount++;

					if( RayTracingScene.GetHit( Ray, HitInstanceIndex, HitClusterIndex, HitTriIndex, HitBarycentrics ) )
						HitCount++;
				}
			}

			if( RayCount > 0 )
			{
				SampledVoxel.NDF /= RayCount;
				SampledVoxel.Coverage = (float)HitCount / RayCount;
			}

			if( HitCount > 0 )
			{
				SampledVoxel.ClusterIndex		= HitClusterIndex;
				SampledVoxel.InstanceIndex		= HitInstanceIndex;
				SampledVoxel.TriIndex			= HitTriIndex;
				SampledVoxel.Barycentrics[0]	= (uint16)FMath::RoundToInt32( HitBarycentrics[0] * 65535.0f );
				SampledVoxel.Barycentrics[1]	= (uint16)FMath::RoundToInt32( HitBarycentrics[1] * 65535.0f );
				SampledVoxel.Barycentrics[1]	= FMath::Min< uint16 >( SampledVoxel.Barycentrics[1], 65535 - SampledVoxel.Barycentrics[0] );
			}
		},
		EParallelForFlags::Unbalanced );

	uint32 Seed = 0;
	if( DAG.Settings.NumRays > 1 && !DAG.Settings.bVoxelOpacity )
	{
		FBinaryHeap< float > CoverageHeap( VoxelMap.Num(), VoxelMap.GetMaxIndex() );
		float CoverageSum = 0.0f;

		for( auto It = VoxelMap.CreateIterator(); It; ++It )
		{
			FSampledVoxel& SampledVoxel = SampledVoxels[ It.Value() ];

			if( SampledVoxel.Coverage > 0.0f )
			{
				CoverageHeap.Add( SampledVoxel.Coverage, It.GetId().AsInteger() );
				CoverageSum += SampledVoxel.Coverage;
			}
			else
			{
				// Remember rejected voxels, so their volume still gets sampled at higher levels
				if( DAG.Settings.NumRays < 32 )
					ExtraVoxels.Add( It.Key() );

				It.RemoveCurrent();
			}
		}

		while( (float)CoverageHeap.Num() > CoverageSum )
		{
			uint32 VoxelIndex	= CoverageHeap.Top();
			float Coverage		= CoverageHeap.GetKey( VoxelIndex );
			CoverageHeap.Pop();

			FSetElementId VoxelId = FSetElementId::FromInteger( VoxelIndex );
			
			FIntVector3 Voxel = VoxelMap.Get( VoxelId ).Key;
			VoxelMap.Remove( VoxelId );

			// Remember rejected voxels, so their volume still gets sampled at higher levels
			ExtraVoxels.Add( Voxel );

			FSampledVoxel& SelfData = SampledVoxels[ VoxelIndex ];

			float TotalWeight = 0.0f;

			// Distribute coverage to neighbors
			struct FNeighbor
			{
				FSetElementId	Id;
				float			Weight;
			};
			TArray< FNeighbor, TFixedAllocator<27> > Neighbors;
			for( int32 z = -1; z <= 1; z++ )
			{
				for( int32 y = -1; y <= 1; y++ )
				{
					for( int32 x = -1; x <= 1; x++ )
					{
						FSetElementId NeighborId = VoxelMap.FindId( Voxel + FIntVector3(x,y,z) );
						if( NeighborId.IsValidId() )
						{
							FVector3f Direction( FIntVector3(x,y,z) );
							Direction.Normalize();

							FSampledVoxel& NeighborData = SampledVoxels[ VoxelMap.Get( NeighborId ).Value ];

							float Weight = SelfData.NDF.ProjectedArea( Direction );

							if (Weight > 0.0f)
							{
								Neighbors.Add( { NeighborId, Weight } );
								TotalWeight += Weight;
							}
						}
					}
				}
			}

			for( FNeighbor& Neighbor : Neighbors )
			{
				uint32 AdjIndex = Neighbor.Id.AsInteger();

				FSampledVoxel& NeighborData = SampledVoxels[ VoxelMap.Get( Neighbor.Id ).Value ];

				float SrcCoverage = Coverage * Neighbor.Weight / TotalWeight;
				float DstCoverage = CoverageHeap.GetKey( AdjIndex );

				// Average of over and under case
				float SrcBlend = 1.0f - 0.5f * DstCoverage;
				float DstBlend = 1.0f - 0.5f * SrcCoverage;

				// Stochastic choice of src or dst attributes
				float Rand = ( EvolveSobolSeed( Seed ) >> 8 ) * 5.96046447754e-08f; // * 2^-24
				if( SrcCoverage * SrcBlend > ( SrcCoverage * SrcBlend + DstCoverage * DstBlend ) * Rand )
				{
					NeighborData.ClusterIndex		= SelfData.ClusterIndex;
					NeighborData.InstanceIndex		= SelfData.InstanceIndex;
					NeighborData.TriIndex			= SelfData.TriIndex;
					NeighborData.Barycentrics[0]	= SelfData.Barycentrics[0];
					NeighborData.Barycentrics[1]	= SelfData.Barycentrics[1];
				}

				SrcBlend *= Neighbor.Weight / TotalWeight;

				NeighborData.NDF =
					SrcBlend * SelfData.NDF +
					DstBlend * NeighborData.NDF;

				NeighborData.Coverage = SrcCoverage + DstCoverage - SrcCoverage * DstCoverage;

				CoverageHeap.Update( NeighborData.Coverage, AdjIndex );
			}
		}
	}
	else
	{
		for( auto It = VoxelMap.CreateIterator(); It; ++It )
		{
			FSampledVoxel& SampledVoxel = SampledVoxels[ It.Value() ];

			if( SampledVoxel.Coverage == 0.0f )
			{
				// Remember rejected voxels, so their volume still gets sampled at higher levels
				if( DAG.Settings.NumRays < 32 )
					ExtraVoxels.Add( It.Key() );

				It.RemoveCurrent();
			}
		}
	}

	// Create Verts, lerp attributes
	for( auto& Voxel : VoxelMap )
	{
		uint32 OldIndex = Voxel.Value;
		uint32 NewIndex = Voxel.Value = Verts.AddUninitialized();

		FSampledVoxel& SampledVoxel = SampledVoxels[ OldIndex ];

		const FCluster& Cluster = DAG.Clusters[ SampledVoxel.ClusterIndex ];

		MaterialIndexes.Add( Cluster.MaterialIndexes[ SampledVoxel.TriIndex ] );

		Verts.GetPosition( NewIndex ) = ( Voxel.Key + 0.5f ) * VoxelSize;

		FVector3f Barycentrics;
		Barycentrics[0] = SampledVoxel.Barycentrics[0] / 65535.0f;
		Barycentrics[1] = SampledVoxel.Barycentrics[1] / 65535.0f;
		Barycentrics[2] = 1.0f - Barycentrics[0] - Barycentrics[1];

		LerpAttributes( NewIndex, SampledVoxel.TriIndex, Cluster, Barycentrics );

		if( SampledVoxel.InstanceIndex != NANITE_MAX_ASSEMBLY_TRANSFORMS )
		{
			const FAssemblyInstanceData& InstanceData = DAG.AssemblyInstanceData[ SampledVoxel.InstanceIndex ];

			if( Verts.Format.bHasTangents )
			{
				FVector3f& TangentX = Verts.GetTangentX( NewIndex );
				TangentX = InstanceData.Transform.GetMatrixWithoutScale().TransformVector( TangentX );
				// ASSEMBLYTODO GetTangentYSign needs to negate if transform determinent is <0
			}

			// Instanced clusters' verts receive their assembly part's influences when being transformed to local
			if( InstanceData.NumBoneInfluences > 0 )
			{
				check( Verts.Format.NumBoneInfluences >= InstanceData.NumBoneInfluences );
				FMemory::Memcpy(
					Verts.GetBoneInfluences( NewIndex ),
					&DAG.AssemblyBoneInfluences[ InstanceData.FirstBoneInfluence ],
					InstanceData.NumBoneInfluences * sizeof( FVector2f ) );
			}
			FMemory::Memzero(
				Verts.GetBoneInfluences( NewIndex ) + InstanceData.NumBoneInfluences,
				(Verts.Format.NumBoneInfluences - InstanceData.NumBoneInfluences) * sizeof( FVector2f ) );
		}

		if( DAG.Settings.bVoxelNDF )
		{
			FVector3f AvgNormal;
			FVector2f Alpha;
			SampledVoxel.NDF.FitIsotropic( AvgNormal, Alpha );

			Verts.GetNormal( NewIndex ) = AvgNormal;
			//GetColor( NewIndex ).A = (2.0f / PI) * FMath::Atan2( Alpha.X, Alpha.Y );
			if( Alpha.X > Alpha.Y )
				Verts.GetColor( NewIndex ).A = 1.0f - 0.5f * Alpha.Y / Alpha.X;
			else
				Verts.GetColor( NewIndex ).A = 0.5f * Alpha.X / Alpha.Y;
		}

		if( DAG.Settings.bVoxelOpacity )
		{
			Verts.GetColor( NewIndex ).B = SampledVoxel.Coverage;
		}

		CorrectAttributesFunctions[ Verts.Format.bHasTangents ][ Verts.Format.bHasColors ]( Verts.GetAttributes( NewIndex ) );
	}

	if( VoxelMap.Num() == 0 )
	{
		// VOXELTODO:	Silly workaround for the case where no voxels are hit by rays.
		//				Solve this properly.

		const FCluster& FirstChild = Children[0].GetCluster( DAG );
		// ASSEMBLYTODO
		const FVector3f Center = FirstChild.Verts.GetPosition( 0 ) * RcpVoxelSize;
		const FIntVector3 Voxel = FloorToInt( Center );

		VoxelMap.Add( Voxel, 0 );

		Verts.AddUninitialized();
		MaterialIndexes.Add( FirstChild.MaterialIndexes[ 0 ] );

		Verts.GetPosition( 0 ) = ( Voxel + 0.5f ) * VoxelSize;

		CopyAttributes( 0, 0, FirstChild );
	}
#endif

	check( MaterialIndexes.Num() > 0 );

	VoxelsToBricks( VoxelMap );
}

void FCluster::VoxelsToBricks( TMap< FIntVector3, uint32 >& VoxelMap )
{
	check( Bricks.IsEmpty() );

	FVertexArray	NewVerts( Verts.Format );
	TArray< int32 > NewMaterialIndexes;

	TSet< FIntVector4 > BrickSet;
	for( auto& Voxel : VoxelMap )
		BrickSet.FindOrAdd( FIntVector4( Voxel.Key & ~3, MaterialIndexes[ Voxel.Value ] ) );

	TArray< FIntVector4 > SortedBricks = BrickSet.Array();
	SortedBricks.Sort(
		[]( const FIntVector4& A, const FIntVector4& B )
		{
			if( A.W != B.W )
				return A.W < B.W;
			else if( A.Z != B.Z )
				return A.Z < B.Z;
			else if( A.Y != B.Y )
				return A.Y < B.Y;
			else
				return A.X < B.X;
		} );

	for( FIntVector4& Candidate : SortedBricks )
	{
		FBrick Brick;
		Brick.VoxelMask = 0;
		Brick.Position = FIntVector3( Candidate );
		Brick.VertOffset = NewVerts.Num();

		FIntVector3 BrickMin( MAX_int32 );
		bool bBrickValid = false;
		for( uint32 z = 0; z < 4; z++ )
		{
			for( uint32 y = 0; y < 4; y++ )
			{
				for( uint32 x = 0; x < 4; x++ )
				{
					FIntVector3 Voxel = Brick.Position + FIntVector3(x,y,z);
					uint32* VertIndex = VoxelMap.Find( Voxel );
					if( VertIndex && MaterialIndexes[ *VertIndex ] == Candidate.W )
					{
						BrickMin = BrickMin.ComponentMin( Voxel );
						bBrickValid = true;
					}
				}
			}
		}

		if( !bBrickValid )
			continue;	// No voxels left in brick. Skip it.

		Brick.Position = BrickMin;

		uint32 VoxelIndex = 0;
		for( uint32 z = 0; z < 4; z++ )
		{
			for( uint32 y = 0; y < 4; y++ )
			{
				for( uint32 x = 0; x < 4; x++ )
				{
					FIntVector3 Voxel = Brick.Position + FIntVector3(x,y,z);
					uint32* VertIndex = VoxelMap.Find( Voxel );
					if( VertIndex && MaterialIndexes[ *VertIndex ] == Candidate.W )
					{
						Brick.VoxelMask |= 1ull << VoxelIndex;
						VoxelMap.Remove( Voxel );

						uint32 OldIndex = *VertIndex;
						uint32 NewIndex = NewVerts.Add( &Verts.GetPosition( OldIndex ) );
					}

					VoxelIndex++;
				}
			}
		}

		Bricks.Add( Brick );
		NewMaterialIndexes.Add( Candidate.W );
	}
	check( VoxelMap.IsEmpty() );

	Swap( Verts,			NewVerts );
	Swap( MaterialIndexes,	NewMaterialIndexes );
}

void FCluster::BuildMaterialRanges()
{
	check( MaterialRanges.Num() == 0 );
	check( NumTris * 3 == Indexes.Num() );

	TArray< int32, TInlineAllocator<128> > MaterialElements;
	TArray< int32, TInlineAllocator<64> > MaterialCounts;

	MaterialElements.AddUninitialized( MaterialIndexes.Num() );
	MaterialCounts.AddZeroed( NANITE_MAX_CLUSTER_MATERIALS );

	// Tally up number per material index
	for( int32 i = 0; i < MaterialIndexes.Num(); i++ )
	{
		MaterialElements[i] = i;
		MaterialCounts[ MaterialIndexes[i] ]++;
	}

	// Sort by range count descending, and material index ascending.
	// This groups the material ranges from largest to smallest, which is
	// more efficient for evaluating the sequences on the GPU, and also makes
	// the minus one encoding work (the first range must have more than 1 tri).
	MaterialElements.Sort(
		[&]( int32 A, int32 B )
		{
			int32 IndexA = MaterialIndexes[A];
			int32 IndexB = MaterialIndexes[B];
			int32 CountA = MaterialCounts[ IndexA ];
			int32 CountB = MaterialCounts[ IndexB ];

			if( CountA != CountB )
				return CountA > CountB;

			if( IndexA != IndexB )
				return IndexA < IndexB;

			return A < B;
		} );

	FMaterialRange CurrentRange;
	CurrentRange.RangeStart = 0;
	CurrentRange.RangeLength = 0;
	CurrentRange.MaterialIndex = MaterialElements.Num() > 0 ? MaterialIndexes[ MaterialElements[0] ] : 0;

	for( int32 i = 0; i < MaterialElements.Num(); i++ )
	{
		int32 MaterialIndex = MaterialIndexes[ MaterialElements[i] ];

		// Material changed, so add current range and reset
		if (CurrentRange.RangeLength > 0 && MaterialIndex != CurrentRange.MaterialIndex)
		{
			MaterialRanges.Add(CurrentRange);

			CurrentRange.RangeStart = i;
			CurrentRange.RangeLength = 1;
			CurrentRange.MaterialIndex = MaterialIndex;
		}
		else
		{
			++CurrentRange.RangeLength;
		}
	}

	// Add last triangle to range
	if (CurrentRange.RangeLength > 0)
	{
		MaterialRanges.Add(CurrentRange);
	}

	if( NumTris )
	{
		TArray< uint32 >	NewIndexes;
		TArray< int32 >		NewMaterialIndexes;
	
		NewIndexes.AddUninitialized( Indexes.Num() );
		NewMaterialIndexes.AddUninitialized( MaterialIndexes.Num() );
	
		for( uint32 NewIndex = 0; NewIndex < NumTris; NewIndex++ )
		{
			uint32 OldIndex = MaterialElements[ NewIndex ];
			NewIndexes[ NewIndex * 3 + 0 ] = Indexes[ OldIndex * 3 + 0 ];
			NewIndexes[ NewIndex * 3 + 1 ] = Indexes[ OldIndex * 3 + 1 ];
			NewIndexes[ NewIndex * 3 + 2 ] = Indexes[ OldIndex * 3 + 2 ];
			NewMaterialIndexes[ NewIndex ] = MaterialIndexes[ OldIndex ];
		}
		Swap( Indexes,			NewIndexes );
		Swap( MaterialIndexes,	NewMaterialIndexes );
	}
	else
	{
		FVertexArray		NewVerts( Verts.Format );
		TArray< int32 >		NewMaterialIndexes;
		TArray< FBrick >	NewBricks;
	
		NewVerts.Reserve( Verts.Num() );
		NewMaterialIndexes.AddUninitialized( MaterialIndexes.Num() );
		NewBricks.AddUninitialized( Bricks.Num() );

		for( int32 NewIndex = 0; NewIndex < MaterialElements.Num(); NewIndex++ )
		{
			int32 OldIndex = MaterialElements[ NewIndex ];

			NewMaterialIndexes[ NewIndex ] = MaterialIndexes[ OldIndex ];

			FBrick& OldBrick = Bricks[ OldIndex ];
			FBrick& NewBrick = NewBricks[ NewIndex ];

			uint32 NumVoxels = FMath::CountBits( OldBrick.VoxelMask );
			
			NewBrick = OldBrick;
			NewBrick.VertOffset = NewVerts.Add( &Verts.GetPosition( OldBrick.VertOffset ), NumVoxels );
		}
		Swap( Verts,			NewVerts );
		Swap( MaterialIndexes,	NewMaterialIndexes );
		Swap( Bricks,			NewBricks );
	}
}

static void SanitizeFloat( float& X, float MinValue, float MaxValue, float DefaultValue )
{
	if( X >= MinValue && X <= MaxValue )
		;
	else if( X < MinValue )
		X = MinValue;
	else if( X > MaxValue )
		X = MaxValue;
	else
		X = DefaultValue;
}

static void SanitizeVector( FVector3f& V, float MaxValue, FVector3f DefaultValue )
{
	if ( !(	V.X >= -MaxValue && V.X <= MaxValue &&
			V.Y >= -MaxValue && V.Y <= MaxValue &&
			V.Z >= -MaxValue && V.Z <= MaxValue ) )	// Don't flip condition. This is intentionally written like this to be NaN-safe.
	{
		V = DefaultValue;
	}
}

void FVertexArray::Sanitize()
{
	const float FltThreshold = NANITE_MAX_COORDINATE_VALUE;

	for( uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++ )
	{
		FVector3f& Position = GetPosition( VertexIndex );
		SanitizeFloat( Position.X, -FltThreshold, FltThreshold, 0.0f );
		SanitizeFloat( Position.Y, -FltThreshold, FltThreshold, 0.0f );
		SanitizeFloat( Position.Z, -FltThreshold, FltThreshold, 0.0f );

		FVector3f& Normal = GetNormal( VertexIndex );
		SanitizeVector( Normal, FltThreshold, FVector3f::UpVector );

		if( Format.bHasTangents )
		{
			FVector3f& TangentX = GetTangentX( VertexIndex );
			SanitizeVector( TangentX, FltThreshold, FVector3f::ForwardVector );

			float& TangentYSign = GetTangentYSign( VertexIndex );
			TangentYSign = TangentYSign < 0.0f ? -1.0f : 1.0f;
		}

		if( Format.bHasColors )
		{
			FLinearColor& Color = GetColor( VertexIndex );
			SanitizeFloat( Color.R, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.G, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.B, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.A, 0.0f, 1.0f, 1.0f );
		}

		if( Format.NumTexCoords > 0 )
		{
			FVector2f* UVs = GetUVs( VertexIndex );
			for( uint32 UVIndex = 0; UVIndex < Format.NumTexCoords; UVIndex++ )
			{
				SanitizeFloat( UVs[ UVIndex ].X, -FltThreshold, FltThreshold, 0.0f );
				SanitizeFloat( UVs[ UVIndex ].Y, -FltThreshold, FltThreshold, 0.0f );
			}
		}

		if( Format.NumBoneInfluences > 0 )
		{
			FVector2f* BoneInfluences = GetBoneInfluences( VertexIndex );
			for( uint32 Influence = 0; Influence < Format.NumBoneInfluences; Influence++ )
			{
				SanitizeFloat( BoneInfluences[Influence].X, 0.0f, FltThreshold, 0.0f );
				SanitizeFloat( BoneInfluences[Influence].Y, 0.0f, FltThreshold, 0.0f );
			}
		}
	}
}

FArchive& operator<<(FArchive& Ar, FMaterialRange& Range)
{
	Ar << Range.RangeStart;
	Ar << Range.RangeLength;
	Ar << Range.MaterialIndex;
	Ar << Range.BatchTriCounts;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FStripDesc& Desc)
{
	for (uint32 i = 0; i < 4; i++)
	{
		for (uint32 j = 0; j < 3; j++)
		{
			Ar << Desc.Bitmasks[i][j];
		}
	}
	Ar << Desc.NumPrevRefVerticesBeforeDwords;
	Ar << Desc.NumPrevNewVerticesBeforeDwords;
	return Ar;
}

FClusterAreaWeightedTriangleSampler::FClusterAreaWeightedTriangleSampler()
	: Cluster(nullptr)
{
}

void FClusterAreaWeightedTriangleSampler::Init(const FCluster* InCluster)
{
	Cluster = InCluster;
	Initialize();
}

float FClusterAreaWeightedTriangleSampler::GetWeights(TArray<float>& OutWeights)
{
	//If these hit, you're trying to get weights on a sampler that's not been initialized.
	check(Cluster);

	TConstArrayView<uint32> Indexes = Cluster->Indexes;

	float Total = 0.0f;
	OutWeights.Empty(Cluster->NumTris);

	int32 First = 0;
	int32 Last = Cluster->NumTris;
	for (int32 i = First; i < Last; i += 3)
	{
		FVector3f V0 = Cluster->Verts.GetPosition(Indexes[i + 0]);
		FVector3f V1 = Cluster->Verts.GetPosition(Indexes[i + 1]);
		FVector3f V2 = Cluster->Verts.GetPosition(Indexes[i + 2]);

		float Area = ((V1 - V0) ^ (V2 - V0)).Size() * 0.5f;
		OutWeights.Add(Area);
		Total += Area;
	}
	return Total;
}

} // namespace Nanite
