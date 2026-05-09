// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// SoftObjectNetSerializers.h —— 软引用 NetSerializer 公开声明
// -----------------------------------------------------------------------------
// 软引用（SoftObject*）的语义是"通过路径字符串延迟加载对象"，资产可能尚未加载。
// 网络复制时只需传递路径，对端通过路径在合适时机懒加载（LoadAsset / LoadObject）。
//
// 本文件声明三个软引用 Serializer：
//   • FSoftObjectNetSerializer       —— FSoftObjectPtr：包装一个软引用 + 弱引用缓存
//                                        。若对象已加载且非"网络稳定命名"（如运行
//                                        时生成的 Actor），降级为对象引用走
//                                        FObjectNetSerializer；否则按路径走字符串
//                                        序列化。CollectNetReferences 会把对象引用
//                                        加入 NetReferenceCollector 以便 Iris 的
//                                        引用解析机制处理对端解析。
//   • FSoftObjectPathNetSerializer   —— FSoftObjectPath：纯路径，无对象缓存。
//                                        永远按路径字符串序列化。
//   • FSoftClassPathNetSerializer    —— FSoftClassPath：FSoftObjectPath 的子类，
//                                        语义同上但限制为 UClass 路径。
//
// 与 PackageMap / Iris 引用系统的关系（详见 Docs/Modules/Serialization.md §1.4）：
//   - 路径形式不需要 PackageMap，对端按 FString 反序列化路径并 SetPath；
//   - 对象形式（仅 FSoftObjectNetSerializer）通过 FQuantizedObjectReference +
//     FObjectNetSerializer 走 FObjectReferenceCache，可能借助
//     UIrisObjectReferencePackageMap 兼容旧 NetSerialize 协议。
// =============================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "SoftObjectNetSerializers.generated.h"

/**
 * FSoftObjectNetSerializer 配置（无字段）。所有运行期决策均通过
 * FNetSerializationContext / InternalNetSerializationContext 完成。
 */
USTRUCT()
struct FSoftObjectNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FSoftObjectPathNetSerializer 配置（无字段） */
USTRUCT()
struct FSoftObjectPathNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/** FSoftClassPathNetSerializer 配置（无字段） */
USTRUCT()
struct FSoftClassPathNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// 三个 Serializer 的全局符号声明，定义见 Private/Iris/Serialization/SoftObjectNetSerializers.cpp。
UE_NET_DECLARE_SERIALIZER(FSoftObjectNetSerializer, IRISCORE_API)
UE_NET_DECLARE_SERIALIZER(FSoftObjectPathNetSerializer, IRISCORE_API)
UE_NET_DECLARE_SERIALIZER(FSoftClassPathNetSerializer, IRISCORE_API)

}
