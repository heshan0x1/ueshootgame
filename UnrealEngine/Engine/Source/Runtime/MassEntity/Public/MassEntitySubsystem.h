// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a，Mass 框架的"标准入口"子系统）
// -----------------------------------------------------------------------------
// UMassEntitySubsystem 是 UE 业务代码访问 Mass 框架的"标准入口"。它的作用：
//   * 每个 UWorld 拥有 1 份本子系统，子系统内部持有 1 份 FMassEntityManager。
//   * 业务代码调用：
//         FMassEntityManager& EM = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
//     即可拿到该 World 的"默认 EntityManager"，再通过 EM 创建实体、查询、运行 Processor 等。
//   * 子系统将 Initialize / PostInitialize / Deinitialize 三个生命周期钩子委托给
//     FMassEntityManager 的同名方法。
//
// 多 EntityManager 场景：
//   * 在某些高级用例（独立的预测/回滚池、UI mock 实例、独立服务器房间）下，业务方可以
//     自己 new FMassEntityManager 并独立维护，不走本子系统。但这种情况下需要业务自己
//     处理生命周期、Processor 注册、线程安全等问题。
// =============================================================================

#pragma once

#include "MassSubsystemBase.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.generated.h"

#define UE_API MASSENTITY_API


/** 
 * The sole responsibility of this world subsystem class is to host the default instance of FMassEntityManager
 * for a given UWorld. All the gameplay-related use cases of Mass (found in MassGameplay and related plugins) 
 * use this by default. 
 *
 * 中文说明：
 *   * 继承自 UMassSubsystemBase（而非直接 UWorldSubsystem），从而获得：
 *       - mass.RuntimeSubsystemsEnabled 全局开关
 *       - HandleLateCreation 延迟创建支持
 *       - InitializationState 簿记
 *   * 在 Initialize 中显式调用 RegisterSubsystemType 自我登记到 FTypeManager（因为
 *     UMassSubsystemBase 的标准登记路径里专门跳过了 UMassEntitySubsystem 自己——见
 *     MassSubsystemBase.cpp 的"自循环依赖"避免）。
 *   * 反射宏：
 *       UCLASS(MinimalAPI) —— 仅导出 UClass 反射符号；非反射方法须显式 UE_API 标注。
 */
UCLASS(MinimalAPI)
class UMassEntitySubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

	//~USubsystem interface
	/** 中文：构造默认 EntityManager 之外的初始化（实际创建在构造函数）；初始化 Storage 模式（线程安全/单线程）并完成 TypeManager 自登记。 */
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** 中文：在所有依赖的子系统都 Initialize 之后，让 EntityManager 完成 PostInitialize（如 Processor 集合的准备）。 */
	UE_API virtual void PostInitialize() override;
	/** 中文：先 Deinitialize EntityManager 再 Reset 共享指针，最后转发到 Super::Deinitialize。 */
	UE_API virtual void Deinitialize() override;
	//~End of USubsystem interface

public:
	/**
	 * 中文：构造函数——立即创建一个 FMassEntityManager 实例，把当前 UMassEntitySubsystem* 作为 Owner 传入。
	 *      EntityManager 是 TSharedPtr，因此可以被外部通过 AsShared() 得到 TSharedRef，传给 Processor、
	 *      Observer 等长期持有引用的对象。
	 */
	UE_API UMassEntitySubsystem();

	//~UObject interface
	/** 中文：内存统计接口。把 EntityManager 自身的内存占用累加到 CumulativeResourceSize，用于 ListUObjectMemUsage 等内存分析工具。 */
	UE_API virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~End of UObject interface

	/**
	 * 中文：只读访问默认 EntityManager。check 保证不为空——一旦构造完成就不应为 nullptr，
	 *       直到 Deinitialize 才会 Reset。
	 */
	const FMassEntityManager& GetEntityManager() const { check(EntityManager); return *EntityManager.Get(); }

	/**
	 * 中文：可写访问默认 EntityManager。Mass 业务代码通过这个入口创建/销毁实体、添加 Fragment、
	 *       发起查询等。check 含义同上。
	 */
	FMassEntityManager& GetMutableEntityManager() { check(EntityManager); return *EntityManager.Get(); }

protected:
	/**
	 * 中文：本子系统拥有的默认 EntityManager。
	 *   * 用 TSharedPtr 而非裸指针/UPROPERTY 是因为：
	 *     1) 它不是 UObject（FMassEntityManager 是 plain C++ struct），无法走 UE GC；
	 *     2) Processor / Observer / Query 等都需要持有它的弱/强引用，必须用智能指针管理。
	 *   * 在构造函数中 MakeShareable(new FMassEntityManager(this)) 创建；
	 *     在 Deinitialize() 中 Reset()。
	 */
	TSharedPtr<FMassEntityManager> EntityManager;
};

#undef UE_API
