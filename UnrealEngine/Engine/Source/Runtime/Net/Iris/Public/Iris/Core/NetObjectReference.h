// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/IsPODType.h"

// ============================================================================
// FNetObjectReference —— Iris 统一的"网络对象引用"值类型
// ----------------------------------------------------------------------------
// 背景：
//   旧复制体系中，"对 UObject 的网络引用"主要靠 FNetworkGUID + FNetGUIDCache
//   实现；每次使用都要通过 PackageMap 去查缓存，且生命周期和导入/导出与
//   Actor/Channel 紧密耦合。Iris 重新设计为一个扁平的值类型 FNetObjectReference，
//   由三部分组成："FNetRefHandle + FNetToken + trait"，称为"三位一体"：
//
//   1) FNetRefHandle（54-bit Id + 10-bit ReplicationSystemId）
//      —— 对动态复制对象的强标识；在 FNetRefHandleManager 中注册后才获得。
//         用于客户端/服务端间"对同一个复制体的指代"。
//
//   2) FNetToken（NetCore 的路径/字符串 token）—— PathToken
//      —— 对静态对象（level actor、CDO、资源路径）的轻量路径导出，
//         用"token"代替字符串传输，避免每次重传路径名。
//         当对象还未在 NetRefHandle 表中登记（如尚未 spawn 到 Iris），
//         仅通过 PathToken 也能找到；RefHandle + PathToken 并存时表示
//         既有运行时句柄，又有"稳定路径名"可导出。
//
//   3) trait（ENetObjectReferenceTraits 位标志）
//      —— 当前仅一个 CanBeExported，但预留扩展位；表示这一引用
//         是否可以安全地通过 Exports 机制告诉远端。
//
//   三者"合体"能覆盖 Iris 中所有 UObject 引用场景：RPC 参数、可复制属性、
//   依赖声明（Dependent/Creation dep）、分布式/远端对象（`UE_WITH_REMOTE_OBJECT_HANDLE`）等。
//
//   该类型是 POD（TIsPODType 特化为 true），因此可安全按值传递、memcpy、
//   存入 Quantized Buffer。
// ============================================================================

// 前向声明：避免把整个 UReplicationSystem.h 拉进头文件，保持 Core 轻依赖。
class UReplicationSystem;
class UObject;

namespace UE::Net::Private
{
	// 友元构造点：Iris 内部的 FObjectReferenceCache 是唯一被允许构造
	// FNetObjectReference 的地方（对外构造只有默认构造函数）。这个约束
	// 保证"RefHandle + PathToken + traits 的组合合法性"只由 Cache 保证。
	class FObjectReferenceCache;
}

namespace UE::Net
{

/**
 * Traits for a NetObjectReference.
 *
 * 引用的位标志特征。当前仅 CanBeExported 一项，日后若需扩展（如
 * "必须已映射才能调用 RPC"、"跨 server 引用"等），在这里追加即可。
 * 位宽预留 uint32，足以扩展到 32 个独立特征。
 */
enum class ENetObjectReferenceTraits : uint32
{
	None = 0U,
	/**
	 * Whether the reference can be exported or not.
	 * @note This flag is only guaranteed to be set correctly for references obtained from ObjectReferenceCache.
	 *
	 * 表示该引用是否可通过 Exports/TokenStore 机制导出给远端。
	 * 只有当 FNetObjectReference 来自 FObjectReferenceCache 时，该标志才
	 * 保证正确。调用方自己构造的默认值 trait 为 None，请勿据此判断。
	 */
	CanBeExported = 1U,
};
ENUM_CLASS_FLAGS(ENetObjectReferenceTraits);

/**
 * A reference to a network addressable object.
 *
 * Iris 网络对象引用值类型。组合三样：
 *   - FNetRefHandle RefHandle  —— 动态句柄（可选）
 *   - FNetToken     PathToken  —— 静态路径 token（可选）
 *   - Traits                    —— 导出能力等特征位
 * 至少其中一个 Handle 有效即判定为有效引用。
 *
 * 使用者：
 *   - FObjectReferenceCache：唯一的构造/解析者；维护 UObject* ↔ Reference 双向映射；
 *   - FObjectNetSerializer 家族：读取 RefHandle/PathToken 序列化到位流；
 *   - Bridge / RPC 层：传入传出参数时以值类型直接拷贝。
 */
class FNetObjectReference
{
public:
	/** 默认构造：两个 handle 都无效，traits 为 None，表示"空引用"。 */
	inline FNetObjectReference() {}

	/**
	 * Returns whether the reference points to a valid object.
	 *
	 * 只要 RefHandle 或 PathToken 中有一个有效就算有效引用——这两者在
	 * 三位一体中是"或"的关系：动态对象可能只有 RefHandle，静态对象可能只有
	 * PathToken，两者兼有则更鲁棒（同时能 by-handle 和 by-path 解析）。
	 */
	inline bool IsValid() const { return RefHandle.IsValid() || PathToken.IsValid(); }
	/**
	 * Returns whether this reference points to the same network addressable objects as some other reference.
	 *
	 * 相等性：两个子 Handle 都相等才算相等。Traits 不参与比较（它是派生信息）。
	 */
	inline bool operator==(const FNetObjectReference& Other) const { return RefHandle == Other.GetRefHandle() && PathToken == Other.PathToken; }
	/** Returns whether this reference doesn't point to the same network addressable objects as some other reference. */
	inline bool operator!=(const FNetObjectReference& Other) const { return !(*this == Other); }
		
	/** Returns the NetRefHandle part of the object reference. */
	FNetRefHandle GetRefHandle() const { return RefHandle; }
	/** Returns the NetToken part of the object reference. */
	FNetToken GetPathToken() const { return PathToken; }
	/**
	 * Returns whether the object reference can be exported.
	 * 仅在引用来自 FObjectReferenceCache 时结果可信。
	 */
	bool CanBeExported() const { return EnumHasAnyFlags(Traits, ENetObjectReferenceTraits::CanBeExported); }

	/**
	 * Returns a human readable string representing the object reference, for debugging purposes.
	 *
	 * 面向调试/日志的字符串化：
	 *   - 两者都有效时拼成 "PathToken : RefHandle"；
	 *   - 只有 PathToken 时返回 PathToken 的字符串；
	 *   - 只有 RefHandle 时返回 RefHandle 的字符串。
	 */
	inline FString ToString() const
	{
		if (PathToken.IsValid())
		{
			if (RefHandle.IsValid())
			{
				FString Result(PathToken.ToString());
				Result.Appendf(TEXT(" : %s"), ToCStr(RefHandle.ToString()));
				return Result;			
			}
			else
			{
				return PathToken.ToString();
			}
		}
		else
		{
			return RefHandle.ToString();
		}
	}

private:
	// 只允许 FObjectReferenceCache 构造非默认形态：保证 trait 与 Handle 的
	// 一致性只由 Cache 维护，外部无法绕过。
	friend class Private::FObjectReferenceCache;

	/** 内部全量构造：由 Cache 在解析/导入过程中使用。 */
	inline FNetObjectReference(FNetRefHandle InHandle, FNetToken InPathToken, ENetObjectReferenceTraits InTraits)
		: RefHandle(InHandle)
		, PathToken(InPathToken)
		, Traits(InTraits)
	{
	}

	/** 仅 Handle 构造：PathToken 置空、Traits 置 None。用于动态对象路径。 */
	inline explicit FNetObjectReference(FNetRefHandle Handle)
	: FNetObjectReference(Handle, FNetToken(), ENetObjectReferenceTraits::None)
	{
	}

	/** 三位一体之一：动态复制对象句柄（64-bit：54 Id + 10 RS Id）。 */
	FNetRefHandle RefHandle;
	/** 三位一体之二：静态对象路径 token，可随 Exports 机制传输。 */
	FNetToken PathToken;
	/** 三位一体之三：特征位（目前仅 CanBeExported）。 */
	ENetObjectReferenceTraits Traits = ENetObjectReferenceTraits::None;
};

/**
 * Representation of an object dependency, e.g. when used in UReplicationBridge::GetInitialDependencies().
 *
 * "对象依赖"信息：
 *   - 使用场景：Bridge 层在创建/复制一个对象前，需要告诉 ReplicationSystem
 *     "这个对象依赖哪些其它对象先被复制/解析"。典型如：
 *       · 一个拥有 SubObject 的 Actor，需要先把其依赖的外部资产/关联对象先解析；
 *       · UObjectReplicationBridge::GetInitialDependencies 回调返回 FNetDependencyInfo 数组，
 *         用以在发送端建立 DependentObjects 链、或在接收端决定"先解引哪些"。
 *   - 当前这个 struct 只是 FNetObjectReference 的轻量封装，预留未来扩展
 *     更多依赖元信息（如依赖强度/顺序/条件）的空间。
 */
struct FNetDependencyInfo
{
	/** 通过引用构造：复制一个 FNetObjectReference 作为依赖指向。 */
	explicit FNetDependencyInfo(const FNetObjectReference& InObjectRef)
	: ObjectRef(InObjectRef)
	{}

	/** 被依赖对象的引用（由调用方保证其"曾经可解析"或"计划解析"）。 */
	FNetObjectReference ObjectRef;
};

/**
 * Return type for various methods that resolves NetObjectReferences.
 *
 * 引用解析的结果位图。用于各类 Resolve/Dequantize API 告诉调用方：
 *   - 是否还存在"尚未能解析"的引用；
 *   - 其中是否包含"必须先解析才能继续"（例如 RPC 执行时依赖对象未就绪）。
 */
enum class ENetObjectReferenceResolveResult : uint32
{
	/** There are no unresolved references.  所有引用都已成功解析。 */
	None = 0U,
	/** There are references that were unresolvable at this time.  存在暂时解析不到的引用（非致命，稍后重试）。 */
	HasUnresolvedReferences = 1 << 0U,
	/**
	 * There are references that were unresolvable at this time and that are required before we can continue further processing, such as calling an RPC.
	 * This could be due to an asset not being loaded yet for example.
	 *
	 * 比 HasUnresolvedReferences 更严格：这些未解析引用属于"MustBeMapped"，
	 * 在解析成功前不能调用 RPC / 应用状态。典型原因：目标资源还在 async loading。
	 * 遇到此结果时 ReplicationReader 会把这批数据放入 PendingBatchData 暂存。
	 */
	HasUnresolvedMustBeMappedReferences = HasUnresolvedReferences << 1U,
};
ENUM_CLASS_FLAGS(ENetObjectReferenceResolveResult);

}

// FNetObjectReference 是 POD：由 FNetRefHandle(POD) + FNetToken(POD) + enum
// 组成，不含虚函数/动态分配，可安全 memcpy、放进量化 buffer。
template <> struct TIsPODType<UE::Net::FNetObjectReference> { enum { Value = true }; };

