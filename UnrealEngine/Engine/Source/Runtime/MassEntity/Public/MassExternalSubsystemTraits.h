// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// 5.6 之前版本的"隐式包含"兼容：一些下游代码习惯在包含本头文件后直接使用
// UWorld / UWorldSubsystem，这里保留老的传递包含以免破坏现有编译；新代码不应
// 依赖这些传递 include。
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6

/**
 * Traits describing how a given piece of code can be used by Mass. We require author or user of a given subsystem to 
 * define its traits. To do it add the following in an accessible location. 
 *
 * template<>
 * struct TMassExternalSubsystemTraits<UMyCustomManager>
 * {
 *		enum { GameThreadOnly = false; }
 * }
 *
 * this will let Mass know it can access UMyCustomManager on any thread.
 *
 * This information is being used to calculate processor and query dependencies as well as appropriate distribution of
 * calculations across threads.
 */
/**
 * TMassExternalSubsystemTraits —— 外部子系统的"线程契约"Traits。
 *
 * 背景：Mass 的 Processor 可以声明自己要访问哪些 UWorldSubsystem / UGameInstanceSubsystem
 * 等外部对象。调度器需要知道这些对象是否可以在非游戏线程上安全访问，才能决定
 * 哪些 Processor 能并行、哪些必须排到 GameThread 上。
 *
 * 用法：在你的 Subsystem 头文件（或任意公共位置）里特化本模板，例如：
 *   template<> struct TMassExternalSubsystemTraits<UMyCustomManager>
 *   { enum { GameThreadOnly = false, ThreadSafeWrite = true }; };
 *
 * 若完全不特化，则走下面的默认值——保守地当作"只能 GameThread 访问"。
 */
template <typename T>
struct TMassExternalSubsystemTraits final
{
	enum
	{
		// Unless configured otherwise each subsystem will be treated as "game-thread only".
		// GameThreadOnly=true 表示"只能在游戏线程访问"。是保守的默认值，确保用户
		// 忘记特化时不会意外产生多线程数据竞争。
		GameThreadOnly = true,

		// If set to true all RW and RO operations will be viewed as RO when calculating processor dependencies
		// ThreadSafeWrite=true 时，调度器在做 Processor 依赖图时会把对该 Subsystem 的
		// 读写操作都视同"只读"——即多个 Processor 可以并行写入（前提是该 Subsystem
		// 内部自己实现了线程安全的写入，如 TSAN 无锁结构/原子累加等）。
		// 默认取反于 GameThreadOnly：GameThread-only 的对象不是 ThreadSafeWrite，
		// 多线程可访问的对象默认也不是（作者需要明确声明"我写入是线程安全的"才打开）。
		ThreadSafeWrite = !GameThreadOnly,
	};
};

/** 
 * Shared Fragment traits.
 * @see TMassExternalSubsystemTraits
 */
/**
 * TMassSharedFragmentTraits —— SharedFragment 的"线程契约"Traits。
 *
 * 与 TMassExternalSubsystemTraits 的思路一致，但默认值不同：因为 SharedFragment
 * 本身就是"数据"而不是"服务对象"，默认允许多线程访问（GameThreadOnly=false），
 * 但默认仍把写入视为不安全（ThreadSafeWrite=false），调度器会自动串行化写者。
 */
template <typename T>
struct TMassSharedFragmentTraits final
{
	enum
	{
		/** 是否只能在 GameThread 访问。默认 false——SharedFragment 可在 worker 线程读。 */
		GameThreadOnly = false,
		/** 写入是否线程安全。默认 false——需要 Mass 为不同写者做串行化。 */
		ThreadSafeWrite = false,
	};
};

/** 
 * Fragment traits.
 * @see TMassExternalSubsystemTraits
 */
/**
 * TMassFragmentTraits —— 普通 Fragment 的元信息 Traits。
 * 这里的 Traits 关注点不是线程，而是"拷贝语义"：
 * Mass 以内存块的方式管理 Fragment（memcpy、批量位移、chunk 间迁移等），
 * 所以默认要求 Fragment 是 trivially copyable。不是的话必须显式承担风险。
 */
template <typename T>
struct TMassFragmentTraits final
{
	enum
	{
		// Fragment types are best kept trivially copyable for performance reasons.
		// To enforce that we test this trait when checking if a given type is a valid fragment type.
		// This test can be skipped by specifically opting out, which also documents that 
		// making the given type non-trivially-copyable was a deliberate decision. 
		// AuthorAcceptsItsNotTriviallyCopyable：
		//   作者是否明确同意"我这个 Fragment 不是平凡可拷贝的，我对风险负责"。
		//   默认 false，Mass 的 concept 检查会在编译期拒绝非平凡类型。
		//   如果 Fragment 内含 TSharedPtr/TArray 等必须走拷贝构造的成员，
		//   需要为该类型特化 TMassFragmentTraits 并把本值设为 true——这既解除编译
		//   限制，也作为"显式声明"留在代码里，供 Code Review 时审视是否必要。
		AuthorAcceptsItsNotTriviallyCopyable = false
	};
};