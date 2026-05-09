// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// ReplicationCondition.h（Public）
// ---------------------------------------------------------------------------------------------
// 模块定位：Iris / ReplicationSystem / Conditionals 子模块对外的"动态条件"枚举定义。
//
// 设计上下文（参考 Iris_Architecture.md §3.8 与 ReplicationSystem.md §6.3）：
//   * Iris 把"哪些字段应该向哪些连接复制"分成两层：
//       1. Lifetime 条件（COND_OwnerOnly / COND_AutonomousOnly / COND_SimulatedOnly / ...）：
//          属于"协议形状"的一部分，由 FReplicationStateDescriptor::MemberLifetimeConditionDescriptors
//          静态描述，运行时不会改变（除非属性使用 COND_Dynamic 这种特殊条件）。这部分由 trait
//          EReplicationStateMemberTraits::HasLifetimeConditional 标记。
//       2. 动态条件（本枚举）：游戏代码运行时可以通过
//          UReplicationSystem::SetReplicationCondition / SetReplicationConditionConnectionFilter
//          设置/清除的"高层语义开关"。它会影响第一层 Lifetime 条件的求值结果（例如开了
//          ReplicatePhysics 之后，COND_SimulatedOrPhysics 才会对模拟连接放行）。
//
// 与 Lifetime 条件的差异（重要！）：
//   * Lifetime（协议级）：每个属性的 COND_* 在 protocol 编译期就固定，是状态的一部分；
//   * Dynamic（运行期）：本枚举里的开关是"对象级 + 连接级"的高层布尔，每帧 Conditionals.Update
//     会把它们和 RoleAutonomous / OwningConnection 等信息一起组合成最终的 ConditionalsMask，
//     再用这个 mask 去裁剪 ChangeMask。
//
// 用法：
//   * UReplicationSystem::SetReplicationCondition(Handle, ReplicatePhysics, true)
//     ——对所有连接生效；
//   * UReplicationSystem::SetReplicationConditionConnectionFilter(Handle, RoleAutonomous, ConnId, true)
//     ——只允许单一连接被视为 Autonomous，其他连接退化为 Simulated。
// =============================================================================================

#pragma once

#include "HAL/Platform.h"

namespace UE::Net
{

/**
 * 动态复制条件枚举（运行期可变）。
 *
 * 这是一组"对象级"（部分还可"按连接"）的高层语义开关。它们不是属性自身的 lifetime 条件，
 * 而是用来动态影响那些 lifetime 条件求值结果的运行期信号。
 *
 * 相关文件：
 *   * Private/Iris/ReplicationSystem/Conditionals/ReplicationConditionals.{h,cpp}
 *     ——FReplicationConditionals::Get/Apply/SetCondition 实现；
 *   * UReplicationSystem::SetReplicationCondition / SetReplicationConditionConnectionFilter
 *     ——对外 API 入口。
 */
enum class EReplicationCondition : uint32
{
	/**
	 * 角色为 Autonomous 的那条连接（与 ENetRole::ROLE_AutonomousProxy 对应）。
	 *
	 * 语义：在 Iris 模型里，每个对象至多有"一条连接"被认为是 Autonomous（通常是该 Pawn
	 * 的玩家控制器所在的连接），其他连接一律视为 Simulated。
	 *
	 * 用法：必须使用 SetReplicationConditionConnectionFilter（带 ConnectionId），因为该
	 * 条件本身就是按连接二分的（owner connection vs others）。直接调用 SetReplicationCondition
	 * 会被拒绝（见 FReplicationConditionals::SetCondition 的 ensure）。
	 *
	 * 影响：直接驱动最终 ConditionalsMask 中的 COND_AutonomousOnly / COND_SimulatedOnly /
	 * COND_SimulatedOrPhysics / COND_SimulatedOnlyNoReplay 等位的求值（见
	 * FReplicationConditionals::GetLifetimeConditionals）。同时还会触发对应连接 RemoteRole
	 * 字段的脏标记与基线作废（保证客户端能感知 role 切换）。
	 *
	 * @see UReplicationSystem::SetReplicationConditionConnectionFilter
	 */
	RoleAutonomous,

	/**
	 * 物理复制开关。默认关闭。
	 *
	 * 语义：开启后，Lifetime 条件 COND_SimulatedOrPhysics / COND_SimulatedOrPhysicsNoReplay
	 * 会对所有模拟连接放行（即使在 Simulated 路径上也会复制带 physics 条件的字段，例如
	 * FReplicatedMovement 中的物理子集）。
	 *
	 * 注意：开启会触发该对象（含 SubObject）所有连接的基线作废
	 * （BaselineInvalidationTracker.InvalidateBaselineForAllConnections），因为新增的字段
	 * 在客户端的旧基线里没有正确值。关闭则不需要作废，因为只是收紧条件不会引入未知值。
	 *
	 * 用法：UReplicationSystem::SetReplicationCondition(Handle, ReplicatePhysics, true/false)。
	 *
	 * @see UReplicationSystem::SetReplicationCondition
	 */
	ReplicatePhysics,
};

}
