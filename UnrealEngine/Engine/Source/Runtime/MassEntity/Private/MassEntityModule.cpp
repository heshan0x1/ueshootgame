// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "MassEntityTypes.h"
#include "MassDebugger.h"
#include "MassProcessingTypes.h"
#include "Modules/ModuleManager.h"
#include "Relations/MassChildOf.h"

#define LOCTEXT_NAMESPACE "Mass"

// =============================================================================================
// MassEntityModule.cpp —— MassEntity 模块（dll/库）的入口点
// ---------------------------------------------------------------------------------------------
// IModuleInterface 是 UE 模块系统的标准接口。每个独立编译的 module（对应一个 .Build.cs）
// 必须有且仅有一个 IMPLEMENT_MODULE(YourModuleClass, ModuleName) 宏调用。
//
// MassEntity 模块的启动职责：
//   1) 调试支持：开发版下加载测试套件 MassEntityTestSuite。
//   2) 多线程标志日志：让用户在 log 一眼看出当前编译是否启用了 MASS_DO_PARALLEL。
//   3) 内置 Relation 注册：把 MassEntity 自带的 ChildOf 关系类型注册进 Relation Registry。
//      Relation 是 Mass 中用来描述 entity 之间父子/拥有/绑定等"图结构"的机制；
//      ChildOf 是最基础的一种，必须在所有依赖它的代码运行前完成注册。
// 关闭时仅做 Debugger 的关闭，其余资源由 UObject GC 自然回收。
// =============================================================================================

class FMassEntityModule : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

// 标准宏：把 FMassEntityModule 注册为 "MassEntity" 模块的工厂。
// UE 在加载 dll 时通过查找此宏生成的导出符号来实例化模块对象。
IMPLEMENT_MODULE(FMassEntityModule, MassEntity)

void FMassEntityModule::StartupModule()
{
#if WITH_UNREAL_DEVELOPER_TOOLS
	// 开发者构建（含编辑器）下，预加载测试套件模块。
	// 这样单元测试可以在引擎启动后立刻运行，无需手动 require。
	FModuleManager::Get().LoadModule("MassEntityTestSuite");
#endif // WITH_UNREAL_DEVELOPER_TOOLS

#if MASS_DO_PARALLEL
	// MASS_DO_PARALLEL 是编译期开关，决定 processor 能否在 worker 线程上跑。
	// Server / 移动端常常关闭这个开关以节省线程。打日志方便用户确认。
	UE_LOG(LogMass, Log, TEXT("MassEntity running with MULTITHREADING support."));
#else
	UE_LOG(LogMass, Log, TEXT("MassEntity running in game thread."));
#endif // MASS_DO_PARALLEL

	// 注册 Mass 内置的 ChildOf 关系（一种 Tag-based 的"父→子"链接）。
	// 这必须在任何使用 ChildOf 的 processor / observer 启动之前完成 —— 模块初始化是最早合适的时机。
	UE::Mass::Relations::RegisterChildOfRelation();
}

void FMassEntityModule::ShutdownModule()
{
#if WITH_MASSENTITY_DEBUG
	// 调试器持有的全局状态（例如订阅的 console commands、已注册的可视化 hooks）需要显式清理，
	// 否则在编辑器多次重载模块时会出现重复注册或悬空回调。
	FMassDebugger::ShutdownDebugger();
#endif
}

#undef LOCTEXT_NAMESPACE 
