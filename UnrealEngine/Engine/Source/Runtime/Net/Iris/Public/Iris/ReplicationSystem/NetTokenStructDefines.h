// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetTokenStructDefines.h —— USTRUCT 通过 NetToken 序列化的辅助宏集合
// -------------------------------------------------------------------------------------------------------------
// 本文件提供一组宏，用于把一个 USTRUCT 自动接入 Iris 的 NetToken 化序列化机制：
//   1) 把"具体值数据"(Struct content) 替换成一个紧凑的 FNetToken（仅一个 32-bit 索引 + 1 bit 权威标志）；
//   2) 把"具体值数据"通过独立的 NetTokenDataStream 单独导出（一次性，并按 ACK 跟踪），后续完全靠 Token 寻址；
//   3) 通过 USTRUCT 的 StructOps 自动绑定 IRIS NetSerializer，使得无需手写 Quantize/Dequantize/Serialize 等。
//
// 典型使用场景：内容稳定但不常更新、或者取值集合有限的 USTRUCT（如 GameplayTagContainer 风格的数据）。
// 详细使用范例见 StructNetTokenDataStore.h 中的 doxygen 注释。
// =============================================================================================================

#pragma once

#include "UObject/Class.h"

namespace UE::Net
{

// -----------------------------------------------------------------------------
// TNetTokenStructOpsTypeTraits
//   - 模板辅助 StructOps：把任意 USTRUCT 标记为"有 NetSerializer + 通过相等性判等 + 支持共享序列化"。
//   - 一旦 USTRUCT 被特化为该 traits（由 UE_NET_DECLARE_NAMED_NETTOKEN_STRUCT_SERIALIZERS 自动完成），
//     UStruct 反射系统就会调用我们提供的 NetSerialize/operator== 实现。
//   - WithNetSharedSerialization=true 表示同一帧内同一份 Token 序列化可在多个连接复用（Iris 会缓存比特流）。
// -----------------------------------------------------------------------------
template<typename T>
struct TNetTokenStructOpsTypeTraits : public TStructOpsTypeTraitsBase2<T>
{ 
	enum
	{ 
		WithNetSerializer = true,
		WithIdenticalViaEquality = true,
		WithNetSharedSerialization = true,
	};
};

};
	
// Optional, declares a default Native NetSerializer StructOpts and IRIS NetSerializer methods that serializes the struct as a NetToken by default.
// -----------------------------------------------------------------------------
// UE_NET_DECLARE_NAMED_NETTOKEN_STRUCT_SERIALIZERS
//   - 在 .h 中声明：把 F##NAME 的 StructOps 特化为 TNetTokenStructOpsTypeTraits（启用 NetSerializer 等）。
//   - 同时通过 UE_NET_DECLARE_SERIALIZER 声明一个 IRIS NetSerializer 类型 F##NAME##NetSerializer。
//   - 实现部分由 UE_NET_IMPLEMENT_NAMED_NETTOKEN_STRUCT_SERIALIZERS 在 .cpp 中提供。
// -----------------------------------------------------------------------------
#define UE_NET_DECLARE_NAMED_NETTOKEN_STRUCT_SERIALIZERS(NAME, API) \
	template<> struct TStructOpsTypeTraits<F##NAME> : public UE::Net::TNetTokenStructOpsTypeTraits<F##NAME> {}; \
	namespace UE::Net \
	{ \
		UE_NET_DECLARE_SERIALIZER(F##NAME##NetSerializer, API);\
	}

// Optional, declares a default native NetSerializer method, GetTokenStoreName method and Equality operators (for equality via identity) for a NetToken Struct type
// -----------------------------------------------------------------------------
// UE_NET_NETTOKEN_GENERATED_BODY
//   - 类内部宏：声明 TokenStoreName / GetTokenStoreName / NetSerialize / operator== / operator!=。
//   - 注意：相等性是"按 GetUniqueKey() 比较"，因此使用方需要实现 GetUniqueKey() 函数返回稳定哈希。
// -----------------------------------------------------------------------------
#define UE_NET_NETTOKEN_GENERATED_BODY(NAME, API) \
	inline static FName TokenStoreName = TEXT( PREPROCESSOR_TO_STRING(F##NAME) );\
	static FName GetTokenStoreName() \
	{ \
		return TokenStoreName; \
	} \
	API bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess); \
	API bool operator==(const F##NAME& Other) const; \
	API bool operator!=(const F##NAME& Other) const; 

// Optional, implements a default NetSerializer method and Native IRIS NetSerializer methods that works for NetToken types by default
// -----------------------------------------------------------------------------
// UE_NET_IMPLEMENT_NAMED_NETTOKEN_STRUCT_SERIALIZERS
//   - 在 .cpp 中实现：
//       * NetSerialize：通过 TStructNetTokenDataStoreHelper 转发到 NetToken 化路径（旧 NetSerialize 兼容路径）。
//       * operator==/!=：按 GetUniqueKey() 比较（要求与 NetToken 身份保持一致）。
//       * F##NAME##NetSerializer：派生自 TStructAsNetTokenNetSerializerImpl 的 IRIS NetSerializer。
//       * UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES：把该 NetSerializer 注册到 FNetSerializerRegistry。
// -----------------------------------------------------------------------------
#define UE_NET_IMPLEMENT_NAMED_NETTOKEN_STRUCT_SERIALIZERS(NAME) \
	bool F##NAME::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) \
	{ \
		bOutSuccess = bOutSuccess && UE::Net::TStructNetTokenDataStoreHelper<F##NAME>::NetSerializeAndExportToken(Ar, Map, *static_cast<F##NAME*>(this)); \
		return true; \
	} \
	bool F##NAME::operator==(const F##NAME& Other) const \
	{ \
		return GetUniqueKey() == Other.GetUniqueKey(); \
	}; \
	bool F##NAME::operator!=(const F##NAME& Other) const \
	{ \
		return GetUniqueKey() != Other.GetUniqueKey(); \
	} \
	namespace UE::Net { \
		struct F##NAME##NetSerializer : public TStructAsNetTokenNetSerializerImpl<F##NAME> { \
			static const uint32 Version = 0; \
			static inline const ConfigType DefaultConfig = ConfigType(); \
		};\
		UE_NET_IMPLEMENT_SERIALIZER(F##NAME##NetSerializer); \
		UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES(NAME, F##NAME##NetSerializer); \
	}