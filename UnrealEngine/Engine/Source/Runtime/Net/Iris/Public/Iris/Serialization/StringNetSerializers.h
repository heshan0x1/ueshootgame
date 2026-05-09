// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StringNetSerializers.h
// -----------------------------------------------------------------------------
// Iris 字符串相关 NetSerializer 公开声明（参见 Docs/Modules/Serialization.md §1.5）。
//
// 本文件声明三类基于字符串/名字（FName/FString）的网络序列化器：
//   • FNameNetSerializer            —— 直接把 FName 序列化进位流。对硬编码 EName
//                                       走整型短路径（仅写若干位），其余按字符串
//                                       走"长度 + 自定义 UTF 编码"路径，量化态用
//                                       FQuantizedType 持有动态分配的字节缓冲。
//   • FNameAsNetTokenNetSerializer  —— 借助 ReplicationSystem 的 NetToken 子系统
//                                       （`FNameTokenStore`）把 FName 替换成
//                                       FNetToken（轻量整数 token），再通过 Token
//                                       Store 对端协商导出/解析；显著节省带宽，
//                                       同名只发一次（按 token 复用）。
//   • FStringNetSerializer          —— 通用 FString 序列化；判断纯 ANSI 走快速
//                                       拷贝路径，否则走 TStringCodec<TCHAR> 编码
//                                       (≤3 字节/codepoint，比 UTF-8 更紧凑)。
//
// 与底层关系：
//   - 真正的位流写入由 NetBitStreamWriter / NetBitStreamUtil 完成；
//   - Token 协商由 FNetTokenStore + FNameTokenStore（Public/Iris/ReplicationSystem
//     /NameTokenStore.h）+ FNetExportContext（导出/挂起导出）协作；
//   - 错误码（数组越界/字符串损坏等）使用 `GNetError_*` FName，详见
//     StringNetSerializerUtils.cpp 中的 `GNetError_CorruptString` 定义。
// =============================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "StringNetSerializers.generated.h"

/**
 * FNameNetSerializer 的运行期配置。
 *
 * FName 直接序列化方式无需运行期开关，故 Config 为空（仅作为 FNetSerializerConfig
 * 占位/类型标识，由 USTRUCT 反射系统注册以便在 PropertyNetSerializerInfoRegistry
 * 中以默认实例形式被检索）。
 */
USTRUCT()
struct FNameNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/**
 * FNameAsNetTokenNetSerializer 的配置。同样无字段，
 * 实际需要的 token 服务在运行期通过 `FNetSerializationContext::GetNetTokenStore()`
 * 获取，并按已知类型 `FNameTokenStore` 注册/读写 token。
 */
USTRUCT()
struct FNameAsNetTokenNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/**
 * FStringNetSerializer 的配置。FString 序列化的策略（编码/打包）已在量化阶段
 * 自动决定，因此 Config 不携带额外字段。
 */
USTRUCT()
struct FStringNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// 通过 UE_NET_DECLARE_SERIALIZER 宏在 IRISCORE_API 下导出三个 Serializer 的全局符号，
// 让外部编译单元可以通过 UE_NET_GET_SERIALIZER(Name) 拿到 `FNetSerializer*` 单例。
// 实际定义/实现位于 Private/Iris/Serialization/StringNetSerializers.cpp。
UE_NET_DECLARE_SERIALIZER(FNameNetSerializer, IRISCORE_API)
UE_NET_DECLARE_SERIALIZER(FNameAsNetTokenNetSerializer, IRISCORE_API)
UE_NET_DECLARE_SERIALIZER(FStringNetSerializer, IRISCORE_API)

/**
 * 返回 FNameNetSerializer / FNameAsNetTokenNetSerializer 共享的"安全量化态字节大小"。
 *
 * Iris 在编译期需要知道每个 Serializer 量化态（QuantizedType）的最大尺寸，以便复制
 * 状态描述符（FReplicationStateDescriptor）布局并预分配存储。两种 Name 序列化器
 * 量化态共用 24 字节作为上限，其内部的 static_assert 会在 .cpp 中校验
 * 对应 FQuantizedType 实际大小不超过该值。
 *
 * 为何是 constexpr：用作模板/数组维度参数，并供宏在 UE_NET_GET_SERIALIZER_*
 * 模板的 constexpr 上下文中使用。
 */
constexpr SIZE_T GetNameNetSerializerSafeQuantizedSize() { return 24; }
}

