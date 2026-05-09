// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"


// 前向声明：避免此头文件耦合 ReplicationSystem.h，保持 Core 层"最少依赖"。
class UReplicationSystem;

namespace UE::Net
{

/**
 * 关键错误（Critical Error）检测多播委托签名。
 *
 * 参数：UReplicationSystem*  —— 触发关键错误的 ReplicationSystem 实例。
 *       在 PIE 多实例场景下，上层需要通过该参数区分是哪一个 RS 出了问题。
 *
 * 在何处广播（何时会被调用）：
 *   - Iris 在底层检测到"会导致后续复制无法正确进行"的不可恢复错误时，
 *     如：Protocol 不一致、BitStream 反量化失败且破坏了连接状态、
 *     NetRefHandle 表结构被破坏、FNetBlob 解码严重错误等。
 *   - 典型触发点位于 ReplicationReader/Writer、Serialization Context 的
 *     错误分支中。上层游戏代码可以据此选择"断线重连 / 回到登录 / 写日志
 *     给遥测系统"等策略。
 *
 * 使用者：
 *   - 游戏上层模块（Matchmaking、Analytics、ClientShell）通常会在模块
 *     初始化时 AddLambda/AddUObject 订阅，收集崩溃/踢线遥测。
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FIrisCriticalErrorDetected, UReplicationSystem*);

/**
 * Iris 对外暴露的"全局委托入口"聚合类。
 *
 * 设计意图：
 *   - 不希望在头文件公开具体的静态变量定义（避免符号重复、初始化顺序等问题），
 *     统一通过 static 成员函数返回进程内单例引用的方式访问委托；
 *   - 保持扩展性：未来若再加其他全局钩子（如 `OnReplicationSystemCreated`），
 *     只需在这里继续添加 `GetXxxDelegate()`。
 *
 * 所属分层：Core 基础设施层，对 ReplicationSystem 只有前向声明依赖，不会
 *          在头文件中引入更高层头，保证 Core 保持可被任何模块引用的地位。
 */
class FIrisDelegates
{
public:
	/**
	 * 取得 Iris 的"关键错误"全局多播委托的单例引用。
	 *
	 * - 返回引用语义：调用方可以 `AddLambda/AddUObject/AddSP`，也可以
	 *   `RemoveAll` 清理自己注册的回调；
	 * - 线程约束：Iris 框架当前只在游戏/网络线程上广播该委托，订阅回调
	 *   应尽快返回、避免阻塞；
	 * - 返回的单例生命周期跟随模块/进程，不会随 `UReplicationSystem` 销毁而销毁。
	 */
	IRISCORE_API static FIrisCriticalErrorDetected& GetCriticalErrorDetectedDelegate();
};

	
	

} // end namespace UE::Net

