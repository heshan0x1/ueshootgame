// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityElementTypes.generated.h"

// =============================================================================
// MassEntity 数据元素（Element）基类定义
// =============================================================================
// Mass 是 UE 的 ECS（Entity-Component-System）框架。实体（Entity）本身只是一个
// 轻量句柄（FMassEntityHandle），它真正"长什么样"由其携带的一组 Element 决定：
//
//   * FMassFragment            —— 普通数据组件，每个实体独立存一份（最常见）
//   * FMassTag                 —— 零字节"标签"，只用来标记"这个实体属于某类"，
//                                  参与 Archetype 组合但不占内存
//   * FMassChunkFragment       —— 按"Archetype Chunk（一块内存区）"共享一份的数据；
//                                  同一个 chunk 内所有实体共用。适合放 LOD、区域
//                                  平均值之类的批处理聚合状态
//   * FMassSharedFragment      —— 多实体共享的可写数据，按值的哈希去重，多个实体
//                                  若参数相同会指向同一份。常用于"配置/模板"
//   * FMassConstSharedFragment —— 共享且只读版本，更利于多线程无锁并行读取
//
// 这些基类本身不含数据，靠继承做标记（"空基类标记模式"），真正的识别在
// UE::Mass::IsA<T>() 与 UE::Mass::CXxx concepts 中实现。
// =============================================================================

/**
 * 所有"轻量 Fragment（数据组件）"的基类。
 * 子类应当：
 *   1) 使用 USTRUCT()，因为 Mass 需要 UScriptStruct 反射元信息来做类型索引；
 *   2) 尽量保持 trivially copyable（POD 化），以便在 chunk 中按字节搬运、
 *      做 memcpy 式迁移；若确需非平凡拷贝，请特化 TMassFragmentTraits 并将
 *      AuthorAcceptsItsNotTriviallyCopyable 设为 true；
 *   3) 不要放 UObject* 裸指针到很长寿命的 fragment 中，建议 TWeakObjectPtr。
 */
// This is the base class for all lightweight fragments
USTRUCT()
struct FMassFragment
{
	GENERATED_BODY()
};

// these are the messages we'll print out when static checks whether a given type is a fragment fails
// 以下三个宏是"类型断言"失败时输出给开发者的提示信息。当模板/concept 检查发现
// 用户传入的类型不是合法的 FMassFragment 派生类（或违反 trivially copyable 约束）
// 时，静态断言会打印它们，帮助用户定位问题。
// _MASS_INVALID_FRAGMENT_CORE_MESSAGE：核心解决建议（通用正文），下划线前缀表示"内部"。
#define _MASS_INVALID_FRAGMENT_CORE_MESSAGE "Make sure to inherit from FMassFragment or one of its child-types and ensure that the struct is trivially copyable, or opt out by specializing TMassFragmentTraits for this type and setting AuthorAcceptsItsNotTriviallyCopyable = true"
// MASS_INVALID_FRAGMENT_MSG：不带类型名的简短版本，用于 static_assert。
#define MASS_INVALID_FRAGMENT_MSG  "Given struct doesn't represent a valid fragment type." _MASS_INVALID_FRAGMENT_CORE_MESSAGE
// MASS_INVALID_FRAGMENT_MSG_F：带 %s 占位符的格式化版本，可在运行期 UE_LOG 中打印
// 具体类型名，便于日志排查。注意：虽以 _F 结尾，它本身仍只是字符串字面量，
// 由调用方自行传给 printf-like API。
#define MASS_INVALID_FRAGMENT_MSG_F  "Type %s is not a valid fragment type." _MASS_INVALID_FRAGMENT_CORE_MESSAGE


// This is the base class for types that will only be tested for presence/absence, i.e. Tags.
// Subclasses should never contain any member properties.
/**
 * Tag 基类：仅用来"贴标签"，不承载任何数据。
 * Tag 参与 Archetype 的组合键：给实体加/减 Tag 会把实体在不同 Archetype 之间迁移，
 * 因此频繁切换 Tag 有性能成本。常见用途：IsDead、NeedsPathRecompute、IsInCombat 等
 * 离散的布尔状态。
 *
 * 约束：子类不允许声明任何 UPROPERTY 成员，否则违反 Tag 语义且 Mass 也不会搬运它们。
 */
USTRUCT()
struct FMassTag
{
	GENERATED_BODY()
};

/**
 * ChunkFragment：按 Archetype Chunk 存储的数据，一个 chunk 内所有实体共享同一份。
 * 典型用途：把"整个 chunk 的 LOD 等级"或"chunk 级缓存统计"写到这里，
 * Processor 只需遍历 chunk 一次更新，再让 chunk 内所有实体"集体"消费，
 * 对缓存友好。
 */
USTRUCT()
struct FMassChunkFragment
{
	GENERATED_BODY()
};

/**
 * SharedFragment：多个实体/多个 Archetype 可以共享同一份可写数据。
 * Mass 通过值的哈希做去重——若两个实体请求的 SharedFragment 内容相同，会指向同一实例。
 * 由于可写，跨线程并发修改时需要由调用方保证同步（见 TMassSharedFragmentTraits）。
 */
USTRUCT()
struct FMassSharedFragment
{
	GENERATED_BODY()
};

/**
 * ConstSharedFragment：SharedFragment 的只读版本。因为只读，Mass 可以放心让
 * 多个 worker 线程同时读同一份数据而无需加锁。适合存放"从数据表加载的不变配置"。
 */
USTRUCT()
struct FMassConstSharedFragment
{
	GENERATED_BODY()
};

// -----------------------------------------------------------------------------
// UE::Mass::IsA<T>(UStruct*)
// -----------------------------------------------------------------------------
// 运行时版"类型归类"：给一个 UStruct 元信息，判断它是否隶属于某个 Mass 元素类别。
// 设计思路：
//   * 主模板返回 false：未特化的任意类型默认都"不是"。
//   * 针对上面五个基类各做一次显式特化：通过 UScriptStruct::IsChildOf 在反射系统中
//     查找继承关系。
// 相比直接用 StaticStruct->IsChildOf 的好处是：上层代码可以写成
//     if (UE::Mass::IsA<FMassTag>(SomeStruct)) ...
// 从而保留模板泛型，方便在模板代码里按类别分派。
namespace UE::Mass
{
	/** 主模板：所有未显式特化的 T 一律返回 false。 */
	template<typename T>
	bool IsA(const UStruct* /*Struct*/)
	{
		return false;
	}

	/** 判定 Struct 是否派生自 FMassFragment。 */
	template<>
	inline bool IsA<FMassFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassFragment::StaticStruct());
	}

	/** 判定 Struct 是否派生自 FMassTag。 */
	template<>
	inline bool IsA<FMassTag>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassTag::StaticStruct());
	}

	/** 判定 Struct 是否派生自 FMassChunkFragment。 */
	template<>
	inline bool IsA<FMassChunkFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassChunkFragment::StaticStruct());
	}

	/** 判定 Struct 是否派生自 FMassSharedFragment。 */
	template<>
	inline bool IsA<FMassSharedFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassSharedFragment::StaticStruct());
	}

	/** 判定 Struct 是否派生自 FMassConstSharedFragment。 */
	template<>
	inline bool IsA<FMassConstSharedFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassConstSharedFragment::StaticStruct());
	}
}
