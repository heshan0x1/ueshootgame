// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteRayTracingScene.h"

//VOXELTODO: Investigate if Embree gives deterministic results that we want in builder code

namespace Nanite
{

LLM_DECLARE_TAG(Embree);

// userPtr is provided during callback registration, it points to the associated FEmbreeScene
static bool EmbreeMemoryMonitorRTScene(void* userPtr, ssize_t bytes, bool post)
{
	LLM_SCOPE_BYTAG(Embree);
	LLM_IF_ENABLED(FLowLevelMemTracker::Get().OnLowLevelChangeInMemoryUse(ELLMTracker::Default, static_cast<int64>(bytes)));

	return true;
}

uint32 FRayTracingScene::AddCluster( RTCDevice Device, RTCScene Scene, const FCluster& Cluster )
{
	RTCGeometry Geom = rtcNewGeometry( Device, RTC_GEOMETRY_TYPE_TRIANGLE );

	rtcSetSharedGeometryBuffer( Geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, Cluster.Verts.Array.GetData(),	0, Cluster.Verts.GetVertSize() * sizeof( float ),	Cluster.Verts.Num() );
	rtcSetSharedGeometryBuffer( Geom, RTC_BUFFER_TYPE_INDEX,  0, RTC_FORMAT_UINT3,  Cluster.Indexes.GetData(),		0, 3 * sizeof( uint32 ),							Cluster.NumTris );
	rtcCommitGeometry( Geom );

	uint32 geomID =
	rtcAttachGeometry( Scene, Geom );
	rtcReleaseGeometry( Geom );
	return geomID;
}

uint32 FRayTracingScene::AddInstance( RTCDevice Device, RTCScene Scene, RTCScene InstancedScene, const FMatrix44f& Transform )
{
	RTCGeometry Geom = rtcNewGeometry( Device, RTC_GEOMETRY_TYPE_INSTANCE );

	rtcSetGeometryInstancedScene( Geom, InstancedScene );
	rtcSetGeometryTransform( Geom, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, (const float*)Transform.M );
	rtcCommitGeometry( Geom );

	uint32 geomID =
	rtcAttachGeometry( Scene, Geom );
	rtcReleaseGeometry( Geom );
	return geomID;
}

static void EmbreeErrorFunc( void* userPtr, RTCError error, const char* str )
{
	checkf( 0, TEXT("Embree error %d: %hs"), error, str );
}

FRayTracingScene::FRayTracingScene()
{
	Device = rtcNewDevice( NULL );
	LLM_IF_ENABLED(rtcSetDeviceMemoryMonitorFunction(Device, EmbreeMemoryMonitorRTScene, NULL));

	Scene = rtcNewScene( Device );

	rtcSetDeviceErrorFunction( Device, EmbreeErrorFunc, NULL );
}

FRayTracingScene::~FRayTracingScene()
{
	rtcReleaseScene( Scene );
	rtcReleaseDevice( Device );
}

void FRayTracingScene::AddCluster( const FCluster& Cluster )
{
	AddCluster( Device, Scene, Cluster );
}

void FRayTracingScene::AddInstance( RTCScene InstancedScene, const FMatrix44f& Transform, uint32 InstanceIndex, uint32 ClusterOffset )
{
	uint32 geomID = AddInstance( Device, Scene, InstancedScene, Transform );

	InstanceMap.Add( geomID, FIntVector2( InstanceIndex, ClusterOffset ) );
}

} // namespace Nanite
