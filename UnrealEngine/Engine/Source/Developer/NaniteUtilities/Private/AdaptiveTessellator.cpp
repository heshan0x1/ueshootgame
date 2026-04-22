// Copyright Epic Games, Inc. All Rights Reserved.

#include "AdaptiveTessellator.h"
#include "AdaptiveTessellatorMesh.h"
#include "TriangleUtil.h"
#include "LerpVert.h"
#include "Async/ParallelFor.h"

#include "EngineLogs.h"
#include "HAL/IConsoleManager.h"
#include <fstream>

namespace Nanite
{

namespace AdaptiveTessellation
{

enum class AdaptiveTessellationMode
{
	NaniteOriginal     = 0,
	GeometryCoreCompat = 1, // same behavior as NaniteOriginal
	GeometryCore       = 2, // avoids seams at boundary splits
	Validation         = 3, // compare NaniteOriginal and GeometryCoreCompat
	NumModes
};

static TAutoConsoleVariable<int32> CVarAdaptiveTessellationMode(
	TEXT("r.Nanite.AdaptiveTessellation.Mode"),
	2,
	TEXT("For host-side displacement. "
		"0: use 5.6 adaptive tessellation code path, "
		"1: use 5.7+ geometry core 1:1 compatible with 5.6, "
		"2: use 5.7+ geometry core code-path, avoids seams at boundary splits, "
		"3: compare both (validation only) "
		"(read-only, controlled only via .ini)"), 
	ECVF_ReadOnly
);


// [ Garland and Heckbert 1995, "Fast Polygonal Approximation of Terrains and Height Fields" ]
FAdaptiveTessellator::FAdaptiveTessellator(
	TArray< FLerpVert >&	InVerts,
	TArray< uint32 >&		InIndexes,
	TArray< int32 >&		InMaterialIndexes,
	float		InTargetError,
	float		InSampleRate,
	bool		bCrackFree,
	FDispFunc	InGetDisplacement,
	FBoundsFunc	InGetErrorBounds,
	FNumFunc	InGetNumSamples,
	bool        bApplyDisplacement)
	: GetDisplacement( InGetDisplacement )
	, GetErrorBounds( InGetErrorBounds )
	, GetNumSamples( InGetNumSamples )
	, TargetError( InTargetError * InTargetError )	// squared distance
	, SampleRate( InSampleRate )
	, Verts( InVerts )
	, Indexes( InIndexes )
	, MaterialIndexes( InMaterialIndexes )
{
	const EParallelForFlags ParallelForFlags = EParallelForFlags::None;
	
	check(Indexes.Num() % 3 == 0);
	uint32 NumTris = Indexes.Num() / 3;

	check(MaterialIndexes.Num() == NumTris);
	
	AdjEdges.Init( -1, Indexes.Num() );
	Triangles.AddUninitialized( NumTris );

	Displacements.AddUninitialized( Verts.Num() );
	for( int32 TriIndex = 0; TriIndex < MaterialIndexes.Num(); TriIndex++ )
	{
		for( int k = 0; k < 3; k++ )
		{
			uint32 i = Indexes[ TriIndex * 3 + k ];
			Displacements[i] = GetDisplacement( FVector3f( 1.0f, 0.0f, 0.0f ), Verts[i], Verts[i], Verts[i], MaterialIndexes[ TriIndex ] );
		}
	}

	{
		FEdgeHash EdgeHash( Indexes.Num() );
		for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
		{
			EdgeHash.ForAllMatching( EdgeIndex, true,
				[ this ]( int32 CornerIndex )
				{
					return Verts[ Indexes[ CornerIndex ] ].Position;
				},
				[&]( int32 EdgeIndex0, int32 EdgeIndex1 )
				{
					if( AdjEdges[ EdgeIndex0 ] < 0 &&
						AdjEdges[ EdgeIndex1 ] < 0 )
					{
						AdjEdges[ EdgeIndex0 ] = EdgeIndex1;
						AdjEdges[ EdgeIndex1 ] = EdgeIndex0;
					}
				} );
		}
	}

	SplitRequests.SetNum( Triangles.Num() );
	NumSplits = 0;

	ParallelFor( TEXT("FAdaptiveTessellator.FindSplitBVH.PF"), NumTris, 32,
		[&]( uint32 TriIndex )
		{
			FindSplitBVH( TriIndex );
		}, ParallelForFlags);

	int Iter = 0;
	while( NumSplits )
	{
		// Size to atomic count and sort for deterministic order
		SplitRequests.SetNum( NumSplits, EAllowShrinking::No );
		SplitRequests.Sort();

		for( int32 i = 0; i < SplitRequests.Num(); i++ )
		{
			Triangles[ SplitRequests[i] ].RequestIndex = i;
		}
		
		while( SplitRequests.Num() )
		{
			SplitTriangle( SplitRequests.Pop() );
		}

		SplitRequests.SetNum( Triangles.Num() );
		NumSplits = 0;

		ParallelFor( TEXT("FAdaptiveTessellator.FindSplitBVH.PF"), FindRequests.Num(), 32,
			[&]( uint32 i )
			{
				uint32 TriIndex = FindRequests[i];
				FindSplitBVH( TriIndex );
			}, ParallelForFlags);

		FindRequests.Reset();
		Iter++;
	}

	if( bCrackFree )
	{
		TArray< FVector3f > OldDisplacements;
		OldDisplacements.AddUninitialized( Displacements.Num() );
		Swap( Displacements, OldDisplacements );
		
		FHashTable HashTable( 1 << FMath::FloorLog2( Verts.Num() ), Verts.Num() );
		ParallelFor( TEXT("FAdaptiveTessellator.HashVerts.PF"), Verts.Num(), 4096,
			[&]( int32 i )
			{
				HashTable.Add_Concurrent( HashPosition( Verts[i].Position ), i );
			}, ParallelForFlags );

		ParallelFor( TEXT("FAdaptiveTessellator.HashVerts.PF"), Verts.Num(), 4096,
			[&]( int32 i )
			{
				FVector3f	Average( 0.0f );
				int32		Count = 0;

				uint32 Hash = HashPosition( Verts[i].Position );
				for( uint32 OtherIndex = HashTable.First( Hash ); HashTable.IsValid( OtherIndex ); OtherIndex = HashTable.Next( OtherIndex ) )
				{
					if( Verts[i].Position == Verts[ OtherIndex ].Position )
					{
						Average += OldDisplacements[ OtherIndex ];
						Count++;
					}
				}

				Displacements[i] = Average / Count;
			}, ParallelForFlags);
	}

	if (bApplyDisplacement)
	{
		ParallelFor( TEXT("FAdaptiveTessellator.Displace.PF"), Verts.Num(), 4096,
			[&]( int32 i )
			{
				auto& Vertex = Verts[i];
				Vertex.TangentZ.Normalize();
				Vertex.Position += Displacements[i];
			}, ParallelForFlags );
	}
}

void FAdaptiveTessellator::FindSplit( uint32 TriIndex )
{
	Triangles[ TriIndex ].RequestIndex = -1;

	FLerpVert& Vert0 = Verts[ Indexes[ TriIndex * 3 + 0 ] ];
	FLerpVert& Vert1 = Verts[ Indexes[ TriIndex * 3 + 1 ] ];
	FLerpVert& Vert2 = Verts[ Indexes[ TriIndex * 3 + 2 ] ];

	FVector3f Edge01 = Vert1.Position - Vert0.Position;
	FVector3f Edge12 = Vert2.Position - Vert1.Position;
	FVector3f Edge20 = Vert0.Position - Vert2.Position;

	FVector3f EdgeLengths(
		Edge01.Length(),
		Edge12.Length(),
		Edge20.Length() );

	float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
	float SampleArea = EquilateralArea( SampleRate );

	FVector3f& Displacement0 = Displacements[ Indexes[ TriIndex * 3 + 0 ] ];
	FVector3f& Displacement1 = Displacements[ Indexes[ TriIndex * 3 + 1 ] ];
	FVector3f& Displacement2 = Displacements[ Indexes[ TriIndex * 3 + 2 ] ];

	FVector3f	BestSplit = FVector3f::ZeroVector;
	float		BestError = -1.0f;

	auto GetError = [&]( const FVector3f& Barycentrics )
	{
		FVector3f NewDisplacement = GetDisplacement( Barycentrics, Vert0, Vert1, Vert2, MaterialIndexes[ TriIndex ] );

		FVector3f LerpedDisplacement;
		LerpedDisplacement  = Displacement0 * Barycentrics.X;
		LerpedDisplacement += Displacement1 * Barycentrics.Y;
		LerpedDisplacement += Displacement2 * Barycentrics.Z;

		return ( NewDisplacement - LerpedDisplacement ).SizeSquared();
	};

	bool bCouldFlipEdge[3];
	for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
		bCouldFlipEdge[ EdgeIndex ] = CouldFlipEdge( TriIndex * 3 + EdgeIndex );

	// Sample edges
	for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
	{
		// Only consider splitting edges that can't be solved with edge flips
		if( bCouldFlipEdge[ EdgeIndex ] )
			continue;

		if( EdgeLengths[ EdgeIndex ] > SampleRate )
		{
			const uint32 e0 = EdgeIndex;
			const uint32 e1 = (1 << e0) & 3;

			int32 NumSamples = FMath::CeilToInt( EdgeLengths[ EdgeIndex ] / SampleRate ) - 1;
			for( int32 i = 0; i < NumSamples; i++ )
			{
				FVector3f Barycentrics( 0.0f );
				Barycentrics[ e0 ] = float( i + 1 ) / ( NumSamples + 2 );
				Barycentrics[ e1 ] = float( NumSamples + 1 - i ) / ( NumSamples + 2 );

				float Error = GetError( Barycentrics );
				if( BestError < Error )
				{
					BestSplit = Barycentrics;
					BestError = Error;
				}
			}
		}
	}

	if( TriArea > SampleArea )
	{
		int32 NumSamples = FMath::CeilToInt( TriArea / SampleArea );

		// Sample internal area using quasi-random sequence instead of rasterizing
		for( int32 i = 0; i < NumSamples; i++ )
		{
			// Hammersley sequence
			uint32 Reverse = ReverseBits< uint32 >( i + 1 );
			Reverse = Reverse ^ (Reverse >> 1);
			
			FVector2f Random;
			Random.X = float( i + 1 ) / ( NumSamples + 1 );
			Random.Y = float( Reverse >> 8 ) * 0x1p-24f;

			// Square to triangle
			if( Random.X < Random.Y )
			{
				Random.X *= 0.5f;
				Random.Y -= Random.X;
			}
			else
			{
				Random.Y *= 0.5f;
				Random.X -= Random.Y;
			}

			FVector3f Barycentrics;
			Barycentrics.X = Random.X;
			Barycentrics.Y = Random.Y;
			Barycentrics.Z = 1.0f - Barycentrics.X - Barycentrics.Y;

			bool bTooCloseToEdge = false;
			for( uint32 k = 0; k < 3; k++ )
			{
				if( bCouldFlipEdge[k] )
					continue;

				float Dist = Barycentric::DistanceToEdge( Barycentrics[k], EdgeLengths[ (1 << k) & 3 ], TriArea );
				if( Dist < 0.5f * SampleRate )
					bTooCloseToEdge = true;
			}
			if( bTooCloseToEdge )
				continue;

			float Error = GetError( Barycentrics );
			if( BestError < Error )
			{
				BestSplit = Barycentrics;
				BestError = Error;
			}
		}
	}

	if( BestError > TargetError )
	{
		Triangles[ TriIndex ].SplitBarycentrics = BestSplit;
		Triangles[ TriIndex ].RequestIndex = NumSplits++;
		SplitRequests[ Triangles[ TriIndex ].RequestIndex ] = TriIndex;
	}
}

void FAdaptiveTessellator::FindSplitBVH( uint32 TriIndex )
{
	Triangles[ TriIndex ].RequestIndex = -1;

	FLerpVert& Vert0 = Verts[ Indexes[ TriIndex * 3 + 0 ] ];
	FLerpVert& Vert1 = Verts[ Indexes[ TriIndex * 3 + 1 ] ];
	FLerpVert& Vert2 = Verts[ Indexes[ TriIndex * 3 + 2 ] ];

	FVector3f Edge01 = Vert1.Position - Vert0.Position;
	FVector3f Edge12 = Vert2.Position - Vert1.Position;
	FVector3f Edge20 = Vert0.Position - Vert2.Position;

	FVector3f EdgeLengths(
		Edge01.Length(),
		Edge12.Length(),
		Edge20.Length() );

	if( EdgeLengths[0] < SampleRate &&
		EdgeLengths[1] < SampleRate &&
		EdgeLengths[2] < SampleRate )
		return;

	float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
	float SampleArea = EquilateralArea( SampleRate );

	FVector3f& Displacement0 = Displacements[ Indexes[ TriIndex * 3 + 0 ] ];
	FVector3f& Displacement1 = Displacements[ Indexes[ TriIndex * 3 + 1 ] ];
	FVector3f& Displacement2 = Displacements[ Indexes[ TriIndex * 3 + 2 ] ];

	bool bCouldFlipEdge[3];
	for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
		bCouldFlipEdge[ EdgeIndex ] = CouldFlipEdge( TriIndex * 3 + EdgeIndex );

	FVector3f	BestSplit = FVector3f::ZeroVector;
	float		BestError = -1.0f;

	struct FNode
	{
		float	ErrorMin;
		float	ErrorMax;
		uint32	TriX	: 13;
		uint32	TriY	: 13;
		uint32	FlipTri	: 1;
		uint32	Level	: 4;
	};

	TArray< FNode, TInlineAllocator<256> > Candidates;

	FNode Node;
	Node.ErrorMin	= 0.0f;
	Node.ErrorMax	= MAX_flt;
	Node.TriX		= 0;
	Node.TriY		= 0;
	Node.FlipTri	= 0;
	Node.Level		= 0;

	{
		FVector3f Barycentrics[3] =
		{
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
		};

		FVector2f ErrorBounds = GetErrorBounds(
			Barycentrics,
			Vert0,
			Vert1,
			Vert2,
			Displacement0,
			Displacement1,
			Displacement2,
			MaterialIndexes[ TriIndex ] );

		Node.ErrorMin = ErrorBounds.X;
		Node.ErrorMax = ErrorBounds.Y;
	}

	float ErrorMinimum = TargetError;

	while(	Node.ErrorMax >= ErrorMinimum &&
			Node.ErrorMax > BestError * 1.25f + TargetError )
	{
		for( uint32 ChildIndex = 0; ChildIndex < 4; ChildIndex++ )
		{
			FNode ChildNode;
			ChildNode.Level = Node.Level + 1;
			ChildNode.FlipTri = Node.FlipTri;

			/*
				|\		\|\|
				|\|\	  \|
			*/

			ChildNode.TriX = Node.TriX * 2 + ( ChildIndex & 1 );
			ChildNode.TriY = Node.TriY * 2 + ( ChildIndex >> 1 );
			
			if( Node.FlipTri )
			{
				ChildNode.TriX ^= 1;
				ChildNode.TriY ^= 1;
			}

			if( ChildIndex == 3 )
			{
				ChildNode.TriX		^= 1;
				ChildNode.TriY		^= 1;
				ChildNode.FlipTri	^= 1;
			}

			FVector3f Barycentrics[3];
			SubtriangleBarycentrics( ChildNode.TriX, ChildNode.TriY, ChildNode.FlipTri, 1 << ChildNode.Level, Barycentrics );

			{
				FVector2f ErrorBounds = GetErrorBounds(
					Barycentrics,
					Vert0,
					Vert1,
					Vert2,
					Displacement0,
					Displacement1,
					Displacement2,
					MaterialIndexes[ TriIndex ] );

				ChildNode.ErrorMin = ErrorBounds.X;
				ChildNode.ErrorMax = ErrorBounds.Y;

				// Clamp bounds just in case float precision results in them not perfectly nesting
				ChildNode.ErrorMin = FMath::Max( ChildNode.ErrorMin, Node.ErrorMin );
				ChildNode.ErrorMax = FMath::Min( ChildNode.ErrorMax, Node.ErrorMax );
			}

			if( ErrorMinimum > ChildNode.ErrorMax )
				continue;
			if( ErrorMinimum < ChildNode.ErrorMin )
				ErrorMinimum = ChildNode.ErrorMin;

			if( ChildNode.ErrorMax > BestError * 1.25f + TargetError )
			{
				int32 NumSamples = 64;

				bool bStopTraversal = ChildNode.Level == 12 || ChildNode.ErrorMax - ChildNode.ErrorMin < TargetError;
				if( !bStopTraversal )
				{
				#if 0
					bool bSmallEnough =
						LengthSquared( Barycentrics[0], Barycentrics[1], EdgeLengths * EdgeLengths ) < SampleRate * SampleRate &&
						LengthSquared( Barycentrics[1], Barycentrics[2], EdgeLengths * EdgeLengths ) < SampleRate * SampleRate &&
						LengthSquared( Barycentrics[2], Barycentrics[0], EdgeLengths * EdgeLengths ) < SampleRate * SampleRate;
				#else
					float ChildArea = Barycentric::SubtriangleArea( Barycentrics[0], Barycentrics[1], Barycentrics[2], TriArea );
					bool bSmallEnough = ChildArea < SampleArea * 16.0f;

					NumSamples = FMath::Min( NumSamples, FMath::CeilToInt( ChildArea / SampleArea ) );
				#endif

					bStopTraversal = bSmallEnough;
				}

				if( !bStopTraversal )
				{
					NumSamples = FMath::Min( NumSamples, GetNumSamples( Barycentrics, Vert0, Vert1, Vert2, MaterialIndexes[ TriIndex ] ) );
					bStopTraversal = NumSamples <= 16;
				}

				if( bStopTraversal )
				{
					for( int32 i = 0; i < NumSamples; i++ )
					{
						// Hammersley sequence
						uint32 Reverse = ReverseBits< uint32 >( i + 1 );
						Reverse = Reverse ^ (Reverse >> 1);
			
						FVector2f Random;
						Random.X = float( i + 1 ) / ( NumSamples + 1 );
						Random.Y = float( Reverse >> 8 ) * 0x1p-24f;

						// Square to triangle
						if( Random.X < Random.Y )
						{
							Random.X *= 0.5f;
							Random.Y -= Random.X;
						}
						else
						{
							Random.Y *= 0.5f;
							Random.X -= Random.Y;
						}

						FVector3f Split;
						Split.X = Random.X;
						Split.Y = Random.Y;
						Split.Z = 1.0f - Split.X - Split.Y;

						float DistToEdge[3];
						DistToEdge[0] = Barycentric::DistanceToEdge( Split[2], EdgeLengths[0], TriArea );
						DistToEdge[1] = Barycentric::DistanceToEdge( Split[0], EdgeLengths[1], TriArea );
						DistToEdge[2] = Barycentric::DistanceToEdge( Split[1], EdgeLengths[2], TriArea );

						uint32 e0 = FMath::Min3Index( DistToEdge[0], DistToEdge[1], DistToEdge[2] );
						uint32 e1 = (1 << e0) & 3;
						uint32 e2 = (1 << e1) & 3;

						CA_ASSUME(e1 <= 2);
						CA_ASSUME(e2 <= 2);

						bool bTooCloseToEdge = DistToEdge[ e0 ] < 0.5f * SampleRate;
						if( bTooCloseToEdge && !bCouldFlipEdge[ e0 ] )
						{
							Split[ e0 ] = Split[ e0 ] / ( Split[ e0 ] + Split[ e1 ] );
							Split[ e1 ] = 1.0f - Split[ e0 ];
							Split[ e2 ] = 0.0f;

							bool bTooCloseToEdge1 = !bCouldFlipEdge[ e1 ] && Barycentric::DistanceToEdge( Split[ e0 ], EdgeLengths[ e1 ], TriArea ) < 0.5f * SampleRate;
							bool bTooCloseToEdge2 = !bCouldFlipEdge[ e2 ] && Barycentric::DistanceToEdge( Split[ e1 ], EdgeLengths[ e2 ], TriArea ) < 0.5f * SampleRate;
							bTooCloseToEdge = bTooCloseToEdge1 || bTooCloseToEdge2;
						}

						if( !bTooCloseToEdge )
						{
							FVector3f NewDisplacement = GetDisplacement( Split, Vert0, Vert1, Vert2, MaterialIndexes[ TriIndex ] );

							FVector3f LerpedDisplacement;
							LerpedDisplacement  = Displacement0 * Split.X;
							LerpedDisplacement += Displacement1 * Split.Y;
							LerpedDisplacement += Displacement2 * Split.Z;

							float Error = ( NewDisplacement - LerpedDisplacement ).SizeSquared();
							if( BestError < Error )
							{
								BestSplit = Split;
								BestError = Error;
							}
						}
					}
				}
				else
				{
					Candidates.HeapPush( ChildNode,
						[&]( const FNode& Node0, const FNode& Node1 )
						{
							return Node0.ErrorMax > Node1.ErrorMax;
						} );
				}
			}
		}

		if( Candidates.IsEmpty() )
			break;

		Candidates.HeapPop( Node,
			[&]( const FNode& Node0, const FNode& Node1 )
			{
				return Node0.ErrorMax > Node1.ErrorMax;
			}, EAllowShrinking::No );
	}

	if( BestError > TargetError )
	{
		Triangles[ TriIndex ].SplitBarycentrics = BestSplit;
		Triangles[ TriIndex ].RequestIndex = NumSplits++;
		SplitRequests[ Triangles[ TriIndex ].RequestIndex ] = TriIndex;
	}
}

void FAdaptiveTessellator::SplitTriangle( uint32 TriIndex )
{
	FVector3f Barycentrics = Triangles[ TriIndex ].SplitBarycentrics;
	Triangles[ TriIndex ].RequestIndex = -1;

	ensure( FMath::Abs( Barycentrics.X + Barycentrics.Y + Barycentrics.Z - 1.0f ) < 1e-4f );
	ensure( 0.0f <= Barycentrics.X && Barycentrics.X < 1.0f );
	ensure( 0.0f <= Barycentrics.Y && Barycentrics.Y < 1.0f );
	ensure( 0.0f <= Barycentrics.Z && Barycentrics.Z < 1.0f );

	FLerpVert& Vert0 = Verts[ Indexes[ TriIndex * 3 + 0 ] ];
	FLerpVert& Vert1 = Verts[ Indexes[ TriIndex * 3 + 1 ] ];
	FLerpVert& Vert2 = Verts[ Indexes[ TriIndex * 3 + 2 ] ];

	FLerpVert NewVert;
	NewVert  = Vert0 * Barycentrics.X;
	NewVert += Vert1 * Barycentrics.Y;
	NewVert += Vert2 * Barycentrics.Z;

	FVector3f NewDisplacement = GetDisplacement( Barycentrics, Vert0, Vert1, Vert2, MaterialIndexes[ TriIndex ] );

	uint32 NewIndex = Verts.Add( NewVert );
	Displacements.Add( NewDisplacement );

	for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
	{
		if( Barycentrics[ ( EdgeIndex + 2 ) % 3 ] == 0.0f )
		{
			// Split edge
			int32 Edge[2];
			Edge[0] = TriIndex * 3 + EdgeIndex;
			Edge[1] = AdjEdges[ TriIndex * 3 + EdgeIndex ];

			int32 NumNewTris = Edge[1] < 0 ? 1 : 2;

			int32 OldTriIndex[2];
			OldTriIndex[0] = TriIndex;
			OldTriIndex[1] = Edge[1] / 3;

			int32 NewTriIndex[2];
			NewTriIndex[0] = Triangles.AddDefaulted( NumNewTris );
			NewTriIndex[1] = NewTriIndex[0] + 1;

			if( Edge[1] < 0 )
			{
				OldTriIndex[1] = -1;
				NewTriIndex[1] = -1;
			}
			else
			{
				ensure( Verts[ Indexes[ Edge[0] ] ].Position == Verts[ Indexes[ Cycle3( Edge[1] ) ] ].Position );
				ensure( Verts[ Indexes[ Edge[1] ] ].Position == Verts[ Indexes[ Cycle3( Edge[0] ) ] ].Position );
			}

			/*
				  v2
				 /|\
			  e1/ | \e2
			   /  |  \
			v1/  0|1  \v0
			 o====+====o
			v0\  1|0  /v1
			   \  |  /
			  e2\ | /e1
				 \|/
				  v2
			*/
			for( int32 j = 0; j < NumNewTris; j++ )
			{
				const uint32 e0 = Edge[j];
				const uint32 e1 = Cycle3( Edge[j] );
				const uint32 e2 = Cycle3( Edge[j], 2 );

				uint32 OldIndex0 = Indexes[ e0 ];
				uint32 OldIndex1 = Indexes[ e1 ];
				uint32 OldIndex2 = Indexes[ e2 ];

				int32 OldAdjEdge1 = AdjEdges[ e1 ];
				int32 OldAdjEdge2 = AdjEdges[ e2 ];

				if( j == 1 )
				{
					NewVert  = Verts[ OldIndex0 ] * Barycentrics[ ( EdgeIndex + 1 ) % 3 ];
					NewVert += Verts[ OldIndex1 ] * Barycentrics[ EdgeIndex ];

					NewDisplacement = GetDisplacement( FVector3f( 1.0f, 0.0f, 0.0f ), NewVert, NewVert, NewVert, MaterialIndexes[ OldTriIndex[j] ] );

					NewIndex = Verts.Add( NewVert );
					Displacements.Add( NewDisplacement );
				}

				Indexes.AddUninitialized(3);
				AdjEdges.AddUninitialized(3);
				MaterialIndexes.Add( CopyTemp( MaterialIndexes[ OldTriIndex[j] ] ) );

				// replace v0
				uint32 i = OldTriIndex[j] * 3;
				Indexes[ i + 0 ] = NewIndex;
				Indexes[ i + 1 ] = OldIndex1;
				Indexes[ i + 2 ] = OldIndex2;

				AdjEdges[ i + 0 ] = NewTriIndex[j^1] * 3;
				LinkEdge( i + 1, OldAdjEdge1 );
				AdjEdges[ i + 2 ] = NewTriIndex[j] * 3 + 1;

				// replace v1
				i = NewTriIndex[j] * 3;
				Indexes[ i + 0 ] = OldIndex0;
				Indexes[ i + 1 ] = NewIndex;
				Indexes[ i + 2 ] = OldIndex2;

				AdjEdges[ i + 0 ] = OldTriIndex[j^1] * 3;
				AdjEdges[ i + 1 ] = OldTriIndex[j] * 3 + 2;
				LinkEdge( i + 2, OldAdjEdge2 );
			}

			for( int32 j = 0; j < NumNewTris; j++ )
			{
				RemoveSplitRequest( OldTriIndex[j] );

				AddFindRequest( OldTriIndex[j] );
				AddFindRequest( NewTriIndex[j] );

				TryDelaunayFlip( OldTriIndex[j] * 3 + 1 );
				TryDelaunayFlip( NewTriIndex[j] * 3 + 2 );
			}

			return;
		}
	}

	{
		// Poke triangle
		uint32 OldIndexes[3];
		OldIndexes[0] = Indexes[ TriIndex * 3 + 0 ];
		OldIndexes[1] = Indexes[ TriIndex * 3 + 1 ];
		OldIndexes[2] = Indexes[ TriIndex * 3 + 2 ];

		int32 OldAdjEdges[3];
		OldAdjEdges[0] = AdjEdges[ TriIndex * 3 + 0 ];
		OldAdjEdges[1] = AdjEdges[ TriIndex * 3 + 1 ];
		OldAdjEdges[2] = AdjEdges[ TriIndex * 3 + 2 ];

		uint32 NewTriIndex[3];
		NewTriIndex[0] = TriIndex;
		NewTriIndex[1] = Triangles.AddDefaulted(2);
		NewTriIndex[2] = NewTriIndex[1] + 1;

		Indexes.AddUninitialized(6);
		AdjEdges.AddUninitialized(6);
		MaterialIndexes.AddUninitialized(2);

		for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
		{
			const uint32 e0 = EdgeIndex;
			const uint32 e1 = (1 << e0) & 3;
			const uint32 e2 = (1 << e1) & 3;

			uint32 i = NewTriIndex[ EdgeIndex ] * 3;
			Indexes[ i + 0 ] = OldIndexes[ e0 ];
			Indexes[ i + 1 ] = OldIndexes[ e1 ];
			Indexes[ i + 2 ] = NewIndex;

			LinkEdge( i + 0,	OldAdjEdges[ e0 ] );
			AdjEdges[ i + 1 ] = NewTriIndex[ e1 ] * 3 + 2;
			AdjEdges[ i + 2 ] = NewTriIndex[ e2 ] * 3 + 1;

			MaterialIndexes[ NewTriIndex[ EdgeIndex ] ] = MaterialIndexes[ TriIndex ];
		}

		for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
		{
			AddFindRequest( NewTriIndex[ EdgeIndex ] );

			TryDelaunayFlip( NewTriIndex[ EdgeIndex ] * 3 );
		}
	}
}

void FAdaptiveTessellator::AddFindRequest( uint32 TriIndex )
{
	int32& RequestIndex = Triangles[ TriIndex ].RequestIndex;
	if( RequestIndex != -2 )
	{
		check( RequestIndex == -1 );
		FindRequests.Add( TriIndex );
		RequestIndex = -2;
	}
}

void FAdaptiveTessellator::RemoveSplitRequest( uint32 TriIndex )
{
	int32& RequestIndex = Triangles[ TriIndex ].RequestIndex;
	if( RequestIndex >= 0 )
	{
		Triangles[ SplitRequests.Last() ].RequestIndex = RequestIndex;
		SplitRequests.RemoveAtSwap( RequestIndex, EAllowShrinking::No );
		RequestIndex = -1;
	}
}

void FAdaptiveTessellator::LinkEdge( int32 EdgeIndex0, int32 EdgeIndex1 )
{
	AdjEdges[ EdgeIndex0 ] = EdgeIndex1;
	if( EdgeIndex1 >= 0 )
	{
		AdjEdges[ EdgeIndex1 ] = EdgeIndex0;

		check( Verts[ Indexes[ EdgeIndex0 ] ].Position == Verts[ Indexes[ Cycle3( EdgeIndex1 ) ] ].Position );
		check( Verts[ Indexes[ EdgeIndex1 ] ].Position == Verts[ Indexes[ Cycle3( EdgeIndex0 ) ] ].Position );
	}
}

FVector3f FAdaptiveTessellator::GetTriNormal( uint32 TriIndex ) const
{
	FVector3f& p0 = Verts[ Indexes[ TriIndex * 3 + 0 ] ].Position;
	FVector3f& p1 = Verts[ Indexes[ TriIndex * 3 + 1 ] ].Position;
	FVector3f& p2 = Verts[ Indexes[ TriIndex * 3 + 2 ] ].Position;

	FVector3f Edge01 = p1 - p0;
	FVector3f Edge12 = p2 - p1;
	FVector3f Edge20 = p0 - p2;

	return ( Edge01 ^ Edge20 ).GetSafeNormal();
}

bool FAdaptiveTessellator::CouldFlipEdge( uint32 EdgeIndex ) const
{
	int32 AdjEdgeIndex = AdjEdges[ EdgeIndex ];
	if( AdjEdgeIndex < 0 )
		return false;

	if( Indexes[ EdgeIndex ] != Indexes[ Cycle3( AdjEdgeIndex ) ] ||
		Indexes[ Cycle3( EdgeIndex ) ] != Indexes[ AdjEdgeIndex ] )
	{
		return false;
	}

	uint32 TriIndex = EdgeIndex / 3;
	uint32 AdjTriIndex = AdjEdgeIndex / 3;

	if( MaterialIndexes[ TriIndex ] != MaterialIndexes[ AdjTriIndex ] )
		return false;

#if 1
	FVector3f TriNormal = GetTriNormal( TriIndex );
	FVector3f AdjNormal = GetTriNormal( AdjTriIndex );

	return ( TriNormal | AdjNormal ) > 0.999f;
#else
	const FVector3f& a0 = Verts[ Indexes[ AdjTriIndex * 3 + 0 ] ].Position;
	const FVector3f& a1 = Verts[ Indexes[ AdjTriIndex * 3 + 1 ] ].Position;
	const FVector3f& a2 = Verts[ Indexes[ AdjTriIndex * 3 + 2 ] ].Position;

	float AdjArea = 0.5f * ( (a2 - a0) ^ (a1 - a0) ).Size();

	const FVector3f& p0 = Verts[ Indexes[ TriIndex * 3 + 0 ] ].Position;
	const FVector3f& p1 = Verts[ Indexes[ TriIndex * 3 + 1 ] ].Position;
	const FVector3f& p2 = Verts[ Indexes[ TriIndex * 3 + 2 ] ].Position;
	const FVector3f& p3 = Verts[ Indexes[ Cycle3( AdjEdgeIndex, 2 ) ] ].Position;

	FVector3f Cross = (p2 - p0) ^ (p1 - p0);

	// Tetrahedron
	float Volume = (1.0f / 6.0f) * FMath::Abs( (p3 - p0) | Cross );
	float Area = FMath::Max( 0.5f * Cross.Size(), AdjArea );

	float Height = 3.0f * Volume / Area;
	return Height < 1e-2f;
#endif
}

void FAdaptiveTessellator::TryDelaunayFlip( uint32 EdgeIndex )
{
	if( !CouldFlipEdge( EdgeIndex ) )
		return;

	auto ComputeCotangent = [this]( uint32 EdgeIndex )
	{
		const uint32 e0 = EdgeIndex;
		const uint32 e1 = Cycle3( EdgeIndex );
		const uint32 e2 = Cycle3( EdgeIndex, 2 );

		FLerpVert& Vert0 = Verts[ Indexes[ e0 ] ];
		FLerpVert& Vert1 = Verts[ Indexes[ e1 ] ];
		FLerpVert& Vert2 = Verts[ Indexes[ e2 ] ];

		FVector3f Edge01 = Vert1.Position - Vert0.Position;
		FVector3f Edge12 = Vert2.Position - Vert1.Position;
		FVector3f Edge20 = Vert0.Position - Vert2.Position;

		FVector3f EdgeLengthsSqr(
			Edge01.SizeSquared(),
			Edge12.SizeSquared(),
			Edge20.SizeSquared() );

		float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
		return 0.25f * ( -EdgeLengthsSqr.X + EdgeLengthsSqr.Y + EdgeLengthsSqr.Z ) / TriArea;
	};

	float LaplacianWeight;
	LaplacianWeight  = ComputeCotangent( EdgeIndex );
	LaplacianWeight += ComputeCotangent( AdjEdges[ EdgeIndex ] );
	LaplacianWeight *= 0.5f;

	bool bFlipEdge = LaplacianWeight < -1e-6f;
	if( bFlipEdge )
	{
		int32 AdjEdge = AdjEdges[ EdgeIndex ];

		uint32 TriIndex = EdgeIndex / 3;
		uint32 AdjTriIndex = AdjEdge / 3;

		/*
				 v2		            v0,v1		       vA
				 /\		             /|\		       /\
			  e1/  \e2	          e2/ | \e1		    eD/  \eA
			   /    \	           /  |  \		     /    \
			v1/  e0  \v0          /   |   \		    /      \
			 o========o    =>   v2  e0|e0  v2	  vD        vB
			v0\  e0  /v1          \   |   /		    \      /
			   \    /	           \  |  /		     \    /
			  e2\  /e1	          e1\ | /e2		    eC\  /eB
				 \/		             \|/		       \/
				 v2		            v1'v0		       vC
		*/
		const uint32 eA = TriIndex * 3 + ( EdgeIndex + 2 ) % 3;
		const uint32 eB = AdjTriIndex * 3 + ( AdjEdge + 1 ) % 3;
		const uint32 eC = AdjTriIndex * 3 + ( AdjEdge + 2 ) % 3;
		const uint32 eD = TriIndex * 3 + ( EdgeIndex + 1 ) % 3;

		uint32 IndexA = Indexes[ eA ];
		uint32 IndexB = Indexes[ eB ];
		uint32 IndexC = Indexes[ eC ];
		uint32 IndexD = Indexes[ eD ];

		uint32 AdjEdgeA = AdjEdges[ eA ];
		uint32 AdjEdgeB = AdjEdges[ eB ];
		uint32 AdjEdgeC = AdjEdges[ eC ];
		uint32 AdjEdgeD = AdjEdges[ eD ];

		RemoveSplitRequest( TriIndex );
		RemoveSplitRequest( AdjTriIndex );

		AddFindRequest( TriIndex );
		AddFindRequest( AdjTriIndex );

		Indexes[ TriIndex * 3 + 0 ] = IndexC;
		Indexes[ TriIndex * 3 + 1 ] = IndexA;
		Indexes[ TriIndex * 3 + 2 ] = IndexB;

		Indexes[ AdjTriIndex * 3 + 0 ] = IndexA;
		Indexes[ AdjTriIndex * 3 + 1 ] = IndexC;
		Indexes[ AdjTriIndex * 3 + 2 ] = IndexD;

		LinkEdge( TriIndex * 3, AdjTriIndex * 3 );

		LinkEdge( TriIndex * 3 + 1, AdjEdgeA );
		LinkEdge( TriIndex * 3 + 2, AdjEdgeB );
		LinkEdge( AdjTriIndex * 3 + 1, AdjEdgeC );
		LinkEdge( AdjTriIndex * 3 + 2, AdjEdgeD );

		TryDelaunayFlip( TriIndex * 3 + 1 );
		TryDelaunayFlip( TriIndex * 3 + 2 );
		TryDelaunayFlip( AdjTriIndex * 3 + 1 );
		TryDelaunayFlip( AdjTriIndex * 3 + 2 );
	}
}

namespace Detail 
{

class FFunctorAdaptorDisplacementPolicy
{
public:
	using FIndex3i = UE::Geometry::FIndex3i;

	FFunctorAdaptorDisplacementPolicy(FMinimalMesh& InMesh,
		const FDispFunc& InGetDisplacement,
		const FBoundsFunc& InGetErrorBounds,
		const FNumFunc& InGetNumSamples)
		: Mesh(InMesh)
		, GetDisplacementFunc(InGetDisplacement)
		, GetErrorBoundsFunc(InGetErrorBounds)
		, GetNumSamplesFunc(InGetNumSamples)
	{
	}

	inline FVector3f GetVertexDisplacement(const int32 VertexIndex, const int32 TriIndex) const
	{
		const FVector3f Barycentrics(1.f, 0.f, 0.f);
		const FLerpVert Vertex = Mesh.GetLerpVert(VertexIndex);

		return GetDisplacementFunc(Barycentrics, Vertex, Vertex, Vertex, Mesh.GetMaterialIndex(TriIndex));
	}

	inline FVector3f GetDisplacement(const FVector3f Barycentrics, const int TriIndex) const
	{
		const FIndex3i Triangle = Mesh.GetTriangle(TriIndex);

		const FLerpVert TriVerts[3] = {
			Mesh.GetLerpVert(Triangle.A),
            Mesh.GetLerpVert(Triangle.B),
            Mesh.GetLerpVert(Triangle.C) };

		return GetDisplacementFunc(Barycentrics, TriVerts[0], TriVerts[1], TriVerts[2], Mesh.GetMaterialIndex(TriIndex));
	}

	inline FVector2f GetErrorBounds(const FVector3f* const Barycentrics, const FVector3f Displacement0, const FVector3f Displacement1, const FVector3f Displacement2, const int TriIndex)
	{
		const FIndex3i Triangle = Mesh.GetTriangle(TriIndex);

		const FLerpVert TriVerts[3] = {
			Mesh.GetLerpVert(Triangle.A),
            Mesh.GetLerpVert(Triangle.B),
            Mesh.GetLerpVert(Triangle.C) };

		return GetErrorBoundsFunc(Barycentrics, TriVerts[0], TriVerts[1], TriVerts[2], Displacement0, Displacement1, Displacement2, Mesh.GetMaterialIndex(TriIndex));
	}

	inline int32 GetNumSamples(const FVector3f* const Barycentrics, const FIndex3i& Triangle, const int TriIndex) const
	{
		const FLerpVert TriVerts[3] = {
			Mesh.GetLerpVert(Triangle.A),
            Mesh.GetLerpVert(Triangle.B),
            Mesh.GetLerpVert(Triangle.C) };

		return GetNumSamplesFunc(Barycentrics, TriVerts[0], TriVerts[1], TriVerts[2], Mesh.GetMaterialIndex(TriIndex));
	}

	inline bool ShouldRefine(const int32 TriIndex, const TArray<FVector3f>&, FVector3f& Barycentrics, const int32 Level)
	{
		check(false); // not implemented, only used when FOptions::RefinementMethod == Custom
		return false;
	}

private:

	const FMinimalMesh&  Mesh;
	const FDispFunc&     GetDisplacementFunc;
	const FBoundsFunc&   GetErrorBoundsFunc;
	const FNumFunc&      GetNumSamplesFunc;
};

} // end namespace Detail

void TessellateAdaptive(
	TArray< FLerpVert >& Verts,
	TArray< uint32 >& Indexes,
	TArray< int32 >& MaterialIndexes,
	float		TargetError,
	float		SampleRate,
	bool		bCrackFree,
	FDispFunc	GetDisplacement,
	FBoundsFunc	GetErrorBounds,
	FNumFunc	GetNumSamples,
	bool        bApplyDisplacement,
	int32       InMode)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS

	AdaptiveTessellationMode Mode = static_cast<AdaptiveTessellationMode>(CVarAdaptiveTessellationMode.GetValueOnAnyThread());

	if (InMode >= 0)
	{
		Mode = static_cast<AdaptiveTessellationMode>(InMode);
	}
	if (Mode < AdaptiveTessellationMode::NaniteOriginal || Mode >= AdaptiveTessellationMode::NumModes)
	{
		Mode = AdaptiveTessellationMode::GeometryCore;
	}

	// Settings for GeometryCore

	// determine adjacency from hashed vertex positions, not indices 
	constexpr bool bTriangleSoup = true;
	
	using TessellatorT = UE::Geometry::TAdaptiveTessellator<FMinimalMesh, Detail::FFunctorAdaptorDisplacementPolicy>;

	typename TessellatorT::FOptions Options;
	Options.TargetError = TargetError * TargetError; // minimizing squared error, see original FAdaptiveTessellator ctor
	Options.SampleRate = SampleRate;
	Options.bCrackFree = bCrackFree;
	Options.bFinalDisplace = bApplyDisplacement;

	if (Mode == AdaptiveTessellationMode::GeometryCore || 
		Mode == AdaptiveTessellationMode::GeometryCoreCompat)
	{
		FMinimalMesh::EEdgeSplitMode EdgeSplitMode = FMinimalMesh::EEdgeSplitMode::AlwaysCrack;
		if (Mode == AdaptiveTessellationMode::GeometryCore)
		{
			// preserve material and UV seams 
			EdgeSplitMode = FMinimalMesh::EEdgeSplitMode::PreserveSeams;
		}

		FMinimalMesh Mesh(Verts, Indexes, MaterialIndexes, EdgeSplitMode, bTriangleSoup);
        Detail::FFunctorAdaptorDisplacementPolicy DisplacementPolicy(Mesh, GetDisplacement, GetErrorBounds, GetNumSamples);
        TessellatorT Tessellator(Mesh, DisplacementPolicy, Options);
	} 
	else if (Mode == AdaptiveTessellationMode::NaniteOriginal)
	{
		FAdaptiveTessellator AdaptiveTessellator(Verts, Indexes, MaterialIndexes,
			TargetError, SampleRate, bCrackFree, GetDisplacement, GetErrorBounds, GetNumSamples, bApplyDisplacement);
	}
	else if (Mode == AdaptiveTessellationMode::Validation)
	{
		// run both and compare the results

		// Note that the crack-free flag in NaniteOriginal and the EEdgeSplitMode refer to different things.
		// 
		// When the bCrackFree flag is enabled all vertices that have originally been at the same position, will be averaged
		// to stay at identical positions after tessellation, which is mainly needed only for disconnected mesh topologies like
		// triangle soups.
		//
		// The EEdgeSplitMode is a new option that can avoid topological cracks that could occur at edge-splits, which needs
		// to be set to AlwaysCrack for comparison.
		// 
		TArray< FLerpVert > VerticesCopy = Verts;
		TArray< uint32 >    IndexesCopy = Indexes;
		TArray< int32 >     MaterialIndexesCopy = MaterialIndexes;

		FMinimalMesh Mesh(VerticesCopy, IndexesCopy, MaterialIndexesCopy, FMinimalMesh::EEdgeSplitMode::AlwaysCrack, bTriangleSoup);
        Detail::FFunctorAdaptorDisplacementPolicy DisplacementPolicy(Mesh, GetDisplacement, GetErrorBounds, GetNumSamples);
        TessellatorT Tessellator(Mesh, DisplacementPolicy, Options);

		FAdaptiveTessellator AdaptiveTessellator(Verts, Indexes, MaterialIndexes, 
			TargetError, SampleRate, bCrackFree, GetDisplacement, GetErrorBounds, GetNumSamples, bApplyDisplacement);
	
		const bool bEqual = (VerticesCopy == Verts) && (IndexesCopy == Indexes) && (MaterialIndexesCopy == MaterialIndexes);

		if (bEqual)
		{
			UE_LOG(LogStaticMesh, Display, TEXT("Nanite original and GeometryCore adaptive tessellation results are equal."));
		}
		else
		{
			UE_LOG(LogStaticMesh, Error, TEXT("Nanite original and GeometryCore adaptive tessellation results are different."));
		}
	}

	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

} // namespace AdaptiveTessellation

} // namespace Nanite