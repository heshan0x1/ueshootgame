// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// IrisLogUtils.cpp —— FIrisLogOnceTracker 的实现
// ----------------------------------------------------------------------------
// 职责：提供"按 hash 去重的一次性日志跟踪器"。上层调用 ShouldLog(Hash) 检查
// 某个 Hash 值是否曾出现过，未出现则返回 true（并把该 Hash 记录下来），下次
// 再遇到同样 Hash 返回 false。典型用途：
//   * 同一个类触发同一条警告只打一次；
//   * 同一个未知 NetSerializer / NetBlob 的兜底警告只打一次；
//   * 某个属性配置异常（无效 Condition / 无效量化精度）只提示一次。
//
// 数据结构：内部用 TSet<uint32> LoggedHashes 存已见过的 Hash（成员声明
// 为 mutable，以便 const 成员函数中插入）。
//
// 哈希冲突说明：
//   - 采用 32-bit 单表 hash，若两个语义不同的 key 碰撞到同一个 uint32，将被
//     当成"同一条消息"只打一次。调用方可通过选择足够强的 hash（例如
//     GetTypeHash(FName) 或包含更多维度的手工组合 hash）降低概率。
//   - Iris 的使用场景都是诊断/开发期日志，即便偶发碰撞也不会造成正确性问
//     题——最坏结果是漏打某条日志。
//
// 线程安全性：TSet 的 Emplace 不是线程安全的。若需要在多线程下共享同一个
// Tracker 实例，调用方需自行加锁；Iris 目前典型用法都是把 Tracker 当作局
// 部/单线程对象（例如挂在只在 Replication 帧上驱动的对象上）。
// ============================================================================

#include "Iris/Core/IrisLogUtils.h"
#include "UObject/NameTypes.h"
#include "Templates/TypeHash.h"

namespace UE::Net
{

/**
 * 判定该 Hash 是否从未出现过。
 *
 * 实现：直接把 Hash 丢进 TSet，利用 Emplace 的 out 参数 bAlreadySet 拿到
 * "本次插入前是否已在集合里"的信息。
 *   - 第一次遇到：bAlreadySet = false  → 返回 true（允许写日志），同时 Hash
 *     已被 Set 记录；
 *   - 之后再遇到：bAlreadySet = true   → 返回 false（抑制重复日志）。
 *
 * 成员函数声明为 const，通过 mutable LoggedHashes 实现语义上的"查询但可修改
 * 内部缓存"。
 *
 * 线程安全：否。并发调用会同时改动 TSet 内部桶，产生数据竞争。
 */
bool FIrisLogOnceTracker::ShouldLog(uint32 Hash) const
{
	bool bAlreadySet = false;
	// TSet::Emplace 带 out 参数版本：若 Hash 已在集合里，bAlreadySet 置 true。
	LoggedHashes.Emplace(Hash, &bAlreadySet);
	// 只有首次插入才应该输出日志。
	return !bAlreadySet;
}

/**
 * FName 版本。
 *
 * 转发到 uint32 版本：使用全局 GetTypeHashHelper(FName) 把 FName 映射到 32-bit
 * hash；由于 FName 内部已经是字符串表的 index+number，其 hash 非常稳定且碰
 * 撞概率低，适合用作"同一类消息只打一次"的键。
 */
bool FIrisLogOnceTracker::ShouldLog(const FName& Name) const
{
	return ShouldLog(::GetTypeHashHelper(Name));
}

}

