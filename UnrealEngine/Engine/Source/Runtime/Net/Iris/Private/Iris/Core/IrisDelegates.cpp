// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// IrisDelegates.cpp —— Iris 全局委托静态入口实现
// ----------------------------------------------------------------------------
// 职责：提供 FIrisDelegates 这一纯静态"委托聚合面板"的具体实现；目前只暴露一
// 路多播：GetCriticalErrorDetectedDelegate。
//
// 背景：Iris 运行过程中若发生"无法恢复的关键错误"——例如：
//   * 接收到的复制数据被判定为损坏（BitStream Overflow / 非法枚举值 …）；
//   * Protocol Identifier 与发送端不一致（版本错位）；
//   * FObjectReferenceCache 指出必须断开连接的严重引用错误。
// Iris 内部会调用该委托广播事件，让上层（Game 层/分析系统/崩溃上报）做出
// 统计、上报、断开连接、强制重连等响应。
//
// 设计要点：
//   1) 采用 Meyer's Singleton（函数内 static 局部变量）实现懒汉单例——首次
//      调用时线程安全地构造，C++11 起编译器保证 static 局部变量初始化仅执行
//      一次（即使多线程并发首次访问）。
//   2) 生命周期：进程结束时由 CRT 析构；Iris 模块卸载不会销毁它，避免挂接了
//      回调的上层模块收到悬挂指针。
//   3) 目前仅此一路多播；如未来新增（如 CriticalWarning / ProtocolMismatch），
//      按同样的"静态函数返回静态对象引用"模式继续扩展即可。
// ============================================================================

#include "Iris/Core/IrisDelegates.h"


namespace UE::Net
{

/**
 * 获取"关键错误检测"全局多播委托的引用。
 *
 * 触发时机：Iris 运行期任意位置检测到不可恢复错误 → 广播事件，传入当前
 * UReplicationSystem* 便于订阅者区分 PIE 多实例中的哪一套出了问题。
 *
 * 线程安全：
 *   - 单例本身的创建是线程安全的（C++11 magic static）；
 *   - 但 FSimpleMulticastDelegate / FIrisCriticalErrorDetected 的 Add / Broadcast
 *     并发调用并不是线程安全的，调用方需自行保证在主线程或加锁使用，
 *     这是 UE Delegate 的一般约束。
 *
 * 典型订阅方：项目层的网络监控/埋点组件，或 UE 崩溃上报前钩子。
 */
FIrisCriticalErrorDetected& FIrisDelegates::GetCriticalErrorDetectedDelegate()
{
	// 静态局部变量：进程生命周期唯一实例，首次进入本函数时完成构造（线程安全）。
	static FIrisCriticalErrorDetected CriticalErrorDelegate;
	return CriticalErrorDelegate;
}

} // end namespace UE::Net

