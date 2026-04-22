// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdaptiveTessellator.h"

#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"
#include "IndexTypes.h"
#include "LerpVert.h"
#include "TriangleUtil.h"

namespace Nanite {

// Mesh topology implementation for UE::Geometry::FAdaptiveTesselation.
//
// TriEdgeIndex is always in [0..2] and corresponds to enumerating triangle edges and vertices in order
// The edge connects the vertices Tri[TriEdgeIndex], Tri[(TriEdgeIndex+1)%3]. 
// 
class FMinimalMesh
{
  public:
	using FIndex3i = UE::Geometry::FIndex3i;

	using RealType = float;
	using VecType  = FVector3f;

	enum class EEdgeSplitMode
	{
		AlwaysCrack,          //< when splitting edges, always duplicate vertex
		PreserveSeams,        //< only split vertex if there is a material or UV seam
		CrackFree             //< never split, always average
	};
	
	FMinimalMesh(
		TArray<FLerpVert>&	InVerts,           //< per-vertex information. original list will be preserved, but new entries will be added
		TArray<uint32>&		InIndexes,         //< per triangle triplets of vertices pointing into InVerts. 
		TArray<int32>&		InMaterialIndexes, //< per-triangle material index
		EEdgeSplitMode      InEdgeSplitMode,   //< see enum
		bool                bInTriangleSoup)   //< if true, adjacency will be determined by vertex positions, otherwise based on indices (faster)
		: Verts(InVerts)
		, Indexes(InIndexes)
		, MaterialIndexes(InMaterialIndexes)
		, EdgeSplitMode(InEdgeSplitMode)
		, bTriangleSoup(bInTriangleSoup)
	{
		AdjEdges.Init( -1, HalfEdgeCount() );

		// create the topology. directed edges will map to their opposite directed edge in opposite direction
		if (!bTriangleSoup)
		{
			struct FSortEdge 
			{
				size_t EdgeIndex;
				int32  VtxA, VtxB;

				FORCEINLINE bool operator<(const FSortEdge& other) const
				{
					return VtxA < other.VtxA || (VtxA == other.VtxA && VtxB < other.VtxB);
				}
			};

			TArray<FSortEdge> SortedHalfEdges;
			SortedHalfEdges.Reserve(AdjEdges.Num());

			const size_t NumEdges = HalfEdgeCount();
			for( size_t i = 0; i < NumEdges; ++i )
			{
				const int32 V0 = Indexes[i];
				const int32 V1 = Indexes[Cycle3(i)];
				SortedHalfEdges.Emplace( i, V0 < V1 ? V0 : V1, V0 < V1 ? V1 : V0 );
			}
			SortedHalfEdges.Sort();

			for ( size_t i = 0; i < NumEdges-1; ++i)
			{
				if (SortedHalfEdges[i].VtxA == SortedHalfEdges[i+1].VtxA && SortedHalfEdges[i].VtxB == SortedHalfEdges[i+1].VtxB)
				{
					AdjEdges[SortedHalfEdges[i  ].EdgeIndex] = SortedHalfEdges[i+1].EdgeIndex;
					AdjEdges[SortedHalfEdges[i+1].EdgeIndex] = SortedHalfEdges[i  ].EdgeIndex;
					++i;
				}
			}
		}
		else
		{
			FEdgeHash EdgeHash( HalfEdgeCount() );
			for( int32 EdgeIndex = 0; EdgeIndex < HalfEdgeCount(); EdgeIndex++ )
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
	}

	////////////////////////////////////////////////////////////////////////////////////////////
	// Policy-interface implementation begin
	////////////////////////////////////////////////////////////////////////////////////////////
	
	FORCEINLINE int32 GetVertexIndex(const int32 TriIndex, const int32 TriEdgeIndex) const
	{
		check(TriEdgeIndex < 3);
		check(TriIndex < MaxTriID());

		return Indexes[TriIndex * 3 + TriEdgeIndex];
	}

	FORCEINLINE FIndex3i GetTriangle(const int32 TriIndex) const
	{
		check(TriIndex < MaxTriID());
		check(TriIndex >= 0);

		check(Indexes[TriIndex * 3 + 0] <= uint32(std::numeric_limits<int32>::max()));
		check(Indexes[TriIndex * 3 + 1] <= uint32(std::numeric_limits<int32>::max()));
		check(Indexes[TriIndex * 3 + 2] <= uint32(std::numeric_limits<int32>::max()));

		return { int32(Indexes[TriIndex * 3 + 0]), int32(Indexes[TriIndex * 3 + 1]), int32(Indexes[TriIndex * 3 + 2]) };
	}

	FORCEINLINE FVector3f GetVertexPosition(const int32 TriIndex, const int32 TriEdgeIndex) const
	{
		return Verts[GetVertexIndex(TriIndex, TriEdgeIndex)].Position;
	}

	FORCEINLINE FVector3f GetVertexPosition(const int32 VertexIndex) const
	{
		return Verts[VertexIndex].Position;
	}

	FORCEINLINE void SetVertexPosition(const int32 VertexIndex, const FVector3f Position) const
	{
		Verts[VertexIndex].Position = Position;
	}

	FORCEINLINE void GetTriVertices(const int32 TriIndex, FVector3f& v0, FVector3f& v1, FVector3f& v2) const
	{
		v0 = Verts[GetVertexIndex(TriIndex, 0)].Position;
		v1 = Verts[GetVertexIndex(TriIndex, 1)].Position;
		v2 = Verts[GetVertexIndex(TriIndex, 2)].Position;
	}

	FORCEINLINE int32 MaxVertexID() const
	{
		return Verts.Num();
	}

	FORCEINLINE bool IsValidVertex(const int32 VertexID) const
	{
		return true;
	}

	FORCEINLINE int32 MaxTriID() const
	{
		return Indexes.Num() / 3;
	}

	FORCEINLINE bool IsValidTri(const int32 TriIndex) const
	{
		return true;
	}

	// Return the triangle index on the other side of the half edge given by TriIndex, TriEdgeIndex or IndexConstants::InvalidID on boundary
	FORCEINLINE int32 GetAdjTriangle(const int32 TriIndex, const int32 TriEdgeIndex) const
	{
		const int32 AdjEdge = AdjEdges[TriIndex * 3 + TriEdgeIndex];

		if (AdjEdge < 0)
			return IndexConstants::InvalidID;

		return AdjEdge / 3;
	}

	// Return the triangle index on the other side of the half edge given by TriIndex, TriEdgeIndex or IndexConstants::InvalidID on boundary
	FORCEINLINE TPair<int32,int32> GetAdjEdge(const int32 TriIndex, const int32 TriEdgeIndex) const
	{
		const int32 AdjEdge = AdjEdges[TriIndex * 3 + TriEdgeIndex];

		if (AdjEdge < 0)
			return { -1, -1 };

		return { AdjEdge / 3, AdjEdge % 3 };
	}

	FORCEINLINE bool EdgeManifoldCheck(const int32 TriIndex, const int32 TriEdgeIndex) const
	{
		const int32 EdgeIndex = TriIndex * 3 + TriEdgeIndex;
		const int32 AdjEdgeIndex = AdjEdges[EdgeIndex];
		
		if (AdjEdgeIndex < 0)
			return true;

		// Manifoldness check
		if( Indexes[EdgeIndex] != Indexes[ Cycle3( AdjEdgeIndex ) ] ||
			Indexes[ Cycle3( EdgeIndex ) ] != Indexes[ AdjEdgeIndex ] )
		{
			return false;
		}

		return true;
	}

	FORCEINLINE bool AllowEdgeFlip(const int32 TriIndex, const int32 TriEdgeIndex, const int32 AdjTriIndex ) const
	{
		return (MaterialIndexes[TriIndex] == MaterialIndexes[AdjTriIndex]);
	}

	FORCEINLINE bool AllowEdgeSplit(const int32 TriIndex, const int32 TriEdgeIndex ) const
	{
		return true;
	}

	FORCEINLINE FVector3f GetTriangleNormal(const int32 TriIndex) const
	{
		const FVector3f& p0 = Verts[Indexes[TriIndex * 3 + 0]].Position;
		const FVector3f& p1 = Verts[Indexes[TriIndex * 3 + 1]].Position;
		const FVector3f& p2 = Verts[Indexes[TriIndex * 3 + 2]].Position;

		const FVector3f Edge01 = p1 - p0;
		const FVector3f Edge12 = p2 - p1;
		const FVector3f Edge20 = p0 - p2;

		return (Edge01 ^ Edge20).GetSafeNormal();
	}

	// Split given triangle edge. The new position is SplitWeight * V0 + (1-SplitWeight) * V1.
	UE::Geometry::FEdgeSplitInfo SplitEdge(const int32 TriIndex, const int32 TriEdgeIndex, const float SplitWeight);

	// Introduce new vertex in triangle, connect to all three vertices and split into three new triangles.
	UE::Geometry::FPokeInfo PokeTriangle(const int32 TriIndex, const FVector3f Barycentrics);

	UE::Geometry::FFlipEdgeInfo FlipEdge(const int32 TriIndex, const int32 TriEdgeIndex);

	bool IsTriangleSoup() const
	{ 
		return bTriangleSoup;
	}

	////////////////////////////////////////////////////////////////////////////////////////////
	// Policy-interface implementation end
	////////////////////////////////////////////////////////////////////////////////////////////
	
	const FLerpVert& GetLerpVert(const int32 Idx) const
	{
		return Verts[Idx];
	}

	int32 GetMaterialIndex(const int32 TriIndex) const
	{
		return MaterialIndexes[TriIndex];
	}
  private:
  
	// Added interpolation of corner vertices of triangle according to barycentric coordinates and return new vertex index.
	inline int32 AddInterpolatedVertex(const FVector3f Barycentrics, const int32 TriIndex)
	{
		ensure( FMath::Abs( Barycentrics.X + Barycentrics.Y + Barycentrics.Z - 1.0f ) < 1e-4f );
		ensure( 0.0f <= Barycentrics.X && Barycentrics.X < 1.0f );
		ensure( 0.0f <= Barycentrics.Y && Barycentrics.Y < 1.0f );
		ensure( 0.0f <= Barycentrics.Z && Barycentrics.Z < 1.0f );

		const FIndex3i Tri = GetTriangle(TriIndex);

		const FLerpVert& Vert0 = Verts[ Tri.A ];
		const FLerpVert& Vert1 = Verts[ Tri.B ];
		const FLerpVert& Vert2 = Verts[ Tri.C ];

		FLerpVert NewVert;
		NewVert  = Vert0 * Barycentrics.X;
		NewVert += Vert1 * Barycentrics.Y;
		NewVert += Vert2 * Barycentrics.Z;

		return Verts.Add( NewVert );
	}

	// Added interpolation of corner vertices of edge according to barycentric coordinates and return new vertex index.
	inline int32 AddInterpolatedVertex(const FVector2f Barycentrics, int32 VertexIndex0, int32 VertexIndex1)
	{
		ensure(FMath::Abs(Barycentrics.X + Barycentrics.Y - 1.0f) < 1e-4f);
		ensure(0.0f <= Barycentrics.X && Barycentrics.X < 1.0f);
		ensure(0.0f <= Barycentrics.Y && Barycentrics.Y < 1.0f);
		
		const FLerpVert& Vert0 = Verts[VertexIndex0];
		const FLerpVert& Vert1 = Verts[VertexIndex1];
		
		FLerpVert NewVert;
		NewVert = Vert0 * Barycentrics.X;
		NewVert += Vert1 * Barycentrics.Y;
		
		return Verts.Add(NewVert);
	}

    FORCEINLINE int32 HalfEdgeCount() const
	{
		return Indexes.Num();
	}

	void LinkEdge( int32 EdgeIndex0, int32 EdgeIndex1 )
	{
		AdjEdges[ EdgeIndex0 ] = EdgeIndex1;
		if( EdgeIndex1 >= 0 )
		{
			AdjEdges[ EdgeIndex1 ] = EdgeIndex0;
	
			check( Verts[ Indexes[ EdgeIndex0 ] ].Position == Verts[ Indexes[ Cycle3( EdgeIndex1 ) ] ].Position );
			check( Verts[ Indexes[ EdgeIndex1 ] ].Position == Verts[ Indexes[ Cycle3( EdgeIndex0 ) ] ].Position );
		}
	}

	bool HasIdenticalUVs( uint32 VID0, uint32 VID1 )
	{
		if (VID0 == VID1) 
		{
			return true;
		}
		const FLerpVert Vert0 = Verts[VID0];
		const FLerpVert Vert1 = Verts[VID1];

		for (uint32 I = 0; I < MAX_STATIC_TEXCOORDS; ++I)
		{
			if (Vert0.UVs[I] != Vert1.UVs[I])
			{
				return false;
			}
		}
		return true;
	}

  private:

	friend class DisplacementPolicyFunctor;

	TArray<FLerpVert>&	 Verts;              // per-vertex 
	TArray<uint32>&		 Indexes;            // triangle as triplets
	TArray<int32>&		 MaterialIndexes;    // per triangle material index

	TArray<int32>        AdjEdges;           // half-edge to neighboring half-edge (or -1 on boundary)
	const EEdgeSplitMode EdgeSplitMode;      // how to we handle edge splits
	const bool           bTriangleSoup;      // if true, don't assume the vertex indices provide mesh connectivity
};


inline UE::Geometry::FEdgeSplitInfo FMinimalMesh::SplitEdge(const int32 TriIndex, const int32 TriEdgeIndex, const float SplitWeight)
{
	UE::Geometry::FEdgeSplitInfo SplitInfo;

	const FIndex3i Triangle = GetTriangle(TriIndex);
	int32 NewIndex = AddInterpolatedVertex(FVector2f(SplitWeight, 1.f - SplitWeight), 
		                                   GetVertexIndex(TriIndex, TriEdgeIndex), 
										   GetVertexIndex(TriIndex, Cycle3(TriEdgeIndex)));
	SplitInfo.NewVertIndex[0] = NewIndex;

	// Split edge
	int32 Edge[2];
	Edge[0] = TriIndex * 3 + TriEdgeIndex;
	Edge[1] = AdjEdges[ Edge[0] ];
	
	const int32 NumNewTris = Edge[1] < 0 ? 1 : 2;

	SplitInfo.OldTriIndex[0] = TriIndex;
	SplitInfo.OldTriIndex[1] = Edge[1] / 3;

	SplitInfo.NewTriIndex[0] = MaxTriID();
	SplitInfo.NewTriIndex[1] = SplitInfo.NewTriIndex[0] + 1;

	bool bIsSeam = false;
	if( Edge[1] < 0 )
	{
		SplitInfo.OldTriIndex[1] = -1;
		SplitInfo.NewTriIndex[1] = -1;
		SplitInfo.NewVertIndex[1] = -1;
	}
	else
	{
		check( Verts[ Indexes[ Edge[0] ] ].Position == Verts[ Indexes[ Cycle3( Edge[1] ) ] ].Position );
		check( Verts[ Indexes[ Edge[1] ] ].Position == Verts[ Indexes[ Cycle3( Edge[0] ) ] ].Position );
		if (EdgeSplitMode == EEdgeSplitMode::PreserveSeams)
		{
			// material seam
			if (MaterialIndexes[SplitInfo.OldTriIndex[0]] != MaterialIndexes[SplitInfo.OldTriIndex[1]])
			{
				bIsSeam = true;
			}
			else if (!HasIdenticalUVs(Indexes[Edge[0]], Indexes[Cycle3(Edge[1])]) ||
				     !HasIdenticalUVs(Indexes[Edge[1]], Indexes[Cycle3(Edge[0])]))
			{
				bIsSeam = true;
			}
		}
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
			// only introduce seem when material indices are different. 
			if (EdgeSplitMode == EEdgeSplitMode::CrackFree || 
				(EdgeSplitMode == EEdgeSplitMode::PreserveSeams && !bIsSeam))
			{	
				SplitInfo.NewVertIndex[j] = SplitInfo.NewVertIndex[0];
			}
			else
			{
				NewIndex = AddInterpolatedVertex(FVector2f(SplitWeight, 1.f - SplitWeight), OldIndex1, OldIndex0);
				SplitInfo.NewVertIndex[j] = NewIndex;
			}     	
		}

		Indexes.AddUninitialized(3);
		AdjEdges.AddUninitialized(3);
		MaterialIndexes.Add( CopyTemp( MaterialIndexes[ SplitInfo.OldTriIndex[j] ] ) );

		// replace v0
		uint32 i = SplitInfo.OldTriIndex[j] * 3;
		Indexes[ i + 0 ] = NewIndex;
		Indexes[ i + 1 ] = OldIndex1;
		Indexes[ i + 2 ] = OldIndex2;

		AdjEdges[ i + 0 ] = SplitInfo.NewTriIndex[j^1] * 3;
		LinkEdge( i + 1, OldAdjEdge1 );
		AdjEdges[ i + 2 ] = SplitInfo.NewTriIndex[j] * 3 + 1;

		// replace v1
		i = SplitInfo.NewTriIndex[j] * 3;
		Indexes[ i + 0 ] = OldIndex0;
		Indexes[ i + 1 ] = NewIndex;
		Indexes[ i + 2 ] = OldIndex2;

		AdjEdges[ i + 0 ] = SplitInfo.OldTriIndex[j^1] * 3;
		AdjEdges[ i + 1 ] = SplitInfo.OldTriIndex[j] * 3 + 2;
		LinkEdge( i + 2, OldAdjEdge2 );
	}

	return SplitInfo;
}

inline UE::Geometry::FPokeInfo FMinimalMesh::PokeTriangle(const int32 TriIndex, const FVector3f Barycentrics)
{
	UE::Geometry::FPokeInfo PokeInfo;

	const FIndex3i Tri = GetTriangle(TriIndex);
	const int32 NewIndex = AddInterpolatedVertex(Barycentrics, TriIndex);

	ensure( FMath::Abs( Barycentrics.X + Barycentrics.Y + Barycentrics.Z - 1.0f ) < 1e-4f );
	ensure( 0.0f <= Barycentrics.X && Barycentrics.X < 1.0f );
	ensure( 0.0f <= Barycentrics.Y && Barycentrics.Y < 1.0f );
	ensure( 0.0f <= Barycentrics.Z && Barycentrics.Z < 1.0f );

	const FLerpVert& Vert0 = Verts[ Tri.A ];
	const FLerpVert& Vert1 = Verts[ Tri.B ];
	const FLerpVert& Vert2 = Verts[ Tri.C ];

	FLerpVert NewVert;
	NewVert  = Vert0 * Barycentrics.X;
	NewVert += Vert1 * Barycentrics.Y;
	NewVert += Vert2 * Barycentrics.Z;

	int32 OldAdjEdges[3];
	OldAdjEdges[0] = AdjEdges[ TriIndex * 3 + 0 ];
	OldAdjEdges[1] = AdjEdges[ TriIndex * 3 + 1 ];
	OldAdjEdges[2] = AdjEdges[ TriIndex * 3 + 2 ];

	PokeInfo.NewTriIndex[0] = TriIndex;
	PokeInfo.NewTriIndex[1] = MaxTriID();
	PokeInfo.NewTriIndex[2] = PokeInfo.NewTriIndex[1] + 1;

	// add 2 triangles
	Indexes.AddUninitialized(6);
	AdjEdges.AddUninitialized(6);
	MaterialIndexes.AddUninitialized(2);

	for( uint32 EdgeIndex = 0; EdgeIndex < 3; EdgeIndex++ )
	{
		const uint32 e0 = EdgeIndex;
		const uint32 e1 = (1 << e0) & 3;
		const uint32 e2 = (1 << e1) & 3;

		uint32 i = PokeInfo.NewTriIndex[ EdgeIndex ] * 3;
		Indexes[ i + 0 ] = Tri[ e0 ];
		Indexes[ i + 1 ] = Tri[ e1 ];
		Indexes[ i + 2 ] = NewIndex;

		LinkEdge( i + 0,	OldAdjEdges[ e0 ] );
		AdjEdges[ i + 1 ] = PokeInfo.NewTriIndex[ e1 ] * 3 + 2;
		AdjEdges[ i + 2 ] = PokeInfo.NewTriIndex[ e2 ] * 3 + 1;

		MaterialIndexes[ PokeInfo.NewTriIndex[ EdgeIndex ] ] = MaterialIndexes[ TriIndex ];
	}
	
	PokeInfo.EdgeIndex[0] = PokeInfo.EdgeIndex[1] = PokeInfo.EdgeIndex[2] = 0;
	PokeInfo.NewVertIndex = NewIndex;

	return PokeInfo;
}

inline UE::Geometry::FFlipEdgeInfo FMinimalMesh::FlipEdge(const int32 TriIndex, const int32 TriEdgeIndex)
{
	const int32 EdgeIndex = TriIndex * 3 + TriEdgeIndex;
	const int32 AdjEdge = AdjEdges[ EdgeIndex ];
	check(AdjEdge >= 0);

	const int32 AdjTriIndex = AdjEdge / 3;

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
	const int32 eA = TriIndex * 3 + ( EdgeIndex + 2 ) % 3;
	const int32 eB = AdjTriIndex * 3 + ( AdjEdge + 1 ) % 3;
	const int32 eC = AdjTriIndex * 3 + ( AdjEdge + 2 ) % 3;
	const int32 eD = TriIndex * 3 + ( EdgeIndex + 1 ) % 3;

	int32 IndexA = Indexes[ eA ];
	int32 IndexB = Indexes[ eB ];
	int32 IndexC = Indexes[ eC ];
	int32 IndexD = Indexes[ eD ];

	int32 AdjEdgeA = AdjEdges[ eA ];
	int32 AdjEdgeB = AdjEdges[ eB ];
	int32 AdjEdgeC = AdjEdges[ eC ];
	int32 AdjEdgeD = AdjEdges[ eD ];

	Indexes[ TriIndex * 3 + 0 ] = IndexC;
	Indexes[ TriIndex * 3 + 1 ] = IndexA;
	Indexes[ TriIndex * 3 + 2 ] = IndexB;

	Indexes[ AdjTriIndex * 3 + 0 ] = IndexA;
	Indexes[ AdjTriIndex * 3 + 1 ] = IndexC;
	Indexes[ AdjTriIndex * 3 + 2 ] = IndexD;

	LinkEdge( TriIndex * 3    , AdjTriIndex * 3 );
	LinkEdge( TriIndex * 3 + 1, AdjEdgeA );
	LinkEdge( TriIndex * 3 + 2, AdjEdgeB );

	LinkEdge( AdjTriIndex * 3 + 1, AdjEdgeC );
	LinkEdge( AdjTriIndex * 3 + 2, AdjEdgeD );

	return { { TriIndex, AdjTriIndex }, { 0, 0 } };
}

} // namespace Nanite