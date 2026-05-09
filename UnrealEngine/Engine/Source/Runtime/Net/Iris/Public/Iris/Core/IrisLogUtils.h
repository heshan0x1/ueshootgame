// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Containers/Set.h"

// 前向声明：避免为此头文件引入 FName 的完整定义。
class FName;

namespace UE::Net
{

/**
 * Utility class to determine whether some message coupled to a hash hasn't been logged yet. 
 * Example usage would be for exmaple if you only want to log something once per class. 
 *
 * ----------------------------------------------------------------------------
 * 中文说明：
 *   用于实现"对相同内容只输出一次日志"的去重追踪器。
 *   在 Iris 内部非常常见的使用场景：
 *     - 某个 UClass 注册复制协议时，如果发现字段不支持，按类名输出一条
 *       警告即可，避免每次实例化都刷屏；
 *     - 某个 NetSerializer 遇到无法解析的 FName/资源引用时，按资源名去重；
 *     - RepNotify / 反射失败回退走"最后手段"路径时的兜底告警等。
 *
 *   实现策略：调用方把感兴趣的标识（对象、字符串、类名、FName 等）折算
 *             成一个 uint32 hash 传入 ShouldLog()，内部用 TSet<uint32>
 *             记录"已经遇到过的 hash"。首次出现返回 true（允许输出），
 *             此后返回 false（抑制）。
 *
 *   哈希碰撞：由于使用 uint32，本质上存在碰撞风险，但日志去重并非安全敏感
 *             功能，偶发漏打一条是可以接受的。
 *
 *   线程安全：LoggedHashes 标记为 mutable，以便 ShouldLog 对外表现为 const；
 *             但内部 TSet 并非线程安全，调用方需自行保证在同一线程/加锁调用。
 * ----------------------------------------------------------------------------
 */
class FIrisLogOnceTracker
{
public:
	/**
	 * Returns true if the Hash has not been encountered in a call to ShouldLog before.
	 *
	 * - 首次出现的 Hash：记录并返回 true，表示可以输出日志；
	 * - 重复出现的 Hash：返回 false，表示应当抑制（去重）。
	 * @param Hash 调用方自行构造的 uint32 摘要；如何构造完全由业务决定
	 *             （可以是 FName hash、PointerHash、字符串 CityHash 等）。
	 */
	bool ShouldLog(uint32 Hash) const;

	/**
	 * Returns true if the hash of the Name has not been encountered in a call to ShouldLog before.
	 *
	 * 便利重载：内部会把 FName 折算成 uint32（通常取其 ComparisonIndex
	 * 或等价摘要）后再走上面的 uint32 版本。
	 */
	bool ShouldLog(const FName& Name) const;

private:
	/**
	 * 已经出现过的 hash 集合。使用 mutable 是为了让 ShouldLog() 保持 const
	 * 语义——对外看起来是"查询"，但内部需要修改集合以实现记忆。
	 */
	mutable TSet<uint32> LoggedHashes;
};

}
