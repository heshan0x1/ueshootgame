// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// NetSerializers.cpp
// ---------------------------------------------------------------------------------------------
// "聚合 cpp"：本身没有真正的 NetSerializer 注册（FBoolNetSerializer / FStructNetSerializer
// / FNopNetSerializer 各自的实现分散在 BoolNetSerializer.cpp / StructNetSerializer.cpp /
// NopNetSerializer.cpp 中）。本文件只承担两件事：
//
//   1. 接入 UHT 反射胶水：通过 UE_INLINE_GENERATED_CPP_BY_NAME(NetSerializers) 把
//      NetSerializers.generated.cpp 的内容内联进来，完成 USTRUCT Config 的反射注册。
//   2. 实现 FStructNetSerializerConfig 的非内联成员：默认构造（设置 NeedDestruction）
//      与默认析构（默认实现，但放在 cpp 里以打断对 ReplicationStateDescriptor 的头文件依赖）。
// =============================================================================================

#include "Iris/Serialization/NetSerializers.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetSerializers)

// ------------------------------------------------------------------------------------------
// FStructNetSerializerConfig 默认构造
// ------------------------------------------------------------------------------------------
// 把 ConfigTraits 设为 NeedDestruction：告诉 NetSerializer 框架在销毁 Config 时一定要
// 跑我们的析构函数（默认 trivial Config 不会跑）。这是因为 StateDescriptor 是 RefCounted，
// 不跑析构会泄漏引用计数。
// ------------------------------------------------------------------------------------------
FStructNetSerializerConfig::FStructNetSerializerConfig()
: FNetSerializerConfig()
{
	ConfigTraits = ENetSerializerConfigTraits::NeedDestruction;
}

// ------------------------------------------------------------------------------------------
// FStructNetSerializerConfig 默认析构
// ------------------------------------------------------------------------------------------
// 显式 = default，但放在 cpp 里——这样头文件无需 #include
// ReplicationStateDescriptor 的完整定义，可大幅降低公开 API 头文件的传染依赖。
// ------------------------------------------------------------------------------------------
FStructNetSerializerConfig::~FStructNetSerializerConfig() = default;
