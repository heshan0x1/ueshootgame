// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a，Mass <-> UE Subsystem 桥接层基座）
// -----------------------------------------------------------------------------
// 本文件定义了 Mass 框架与 UE 标准子系统体系（UWorldSubsystem / UTickableWorldSubsystem）
// 之间的两个"标准底座"基类：
//   * UMassSubsystemBase            —— 所有 Mass 相关、非 Tickable 的 WorldSubsystem 基类
//   * UMassTickableSubsystemBase    —— 所有 Mass 相关、需要 Tick 的 WorldSubsystem 基类
//
// 它们解决以下共性问题：
//   1) 是否在某些 World 上"压根不创建"该 Mass 子系统（如 -game inactive、某些 PIE 工具
//      化场景），通过控制台变量 mass.RuntimeSubsystemsEnabled 一次性开关。
//   2) 子系统在游戏开始后被"延迟创建"（GameplayFeatureActions 动态加载）时，需要补上
//      PostInitialize / OnWorldBeginPlay 这两个本应在 World 早期阶段触发的回调，
//      由 HandleLateCreation() 统一处理。
//   3) 把每个具体的 Mass 子系统类（class）登记到 FTypeManager，用 FSubsystemTypeTraits
//      声明该子系统可以怎么访问（GameThreadOnly / ThreadSafeWrite 等），让 Processor 的
//      依赖求解器（DependencySolver, Wave M9）能据此推断子系统访问的并发安全性。
//
// 设计要点（中文备注）：
//   * 这两个基类本身是 abstract 的：UCLASS(Abstract)，只能被继承，不能直接实例化。
//   * 业务方"自定义 Mass 子系统"应当 **始终** 继承 UMassSubsystemBase 或
//     UMassTickableSubsystemBase，而不是直接继承 UWorldSubsystem，否则会丢掉上述
//     三项功能。
//   * UMassEntitySubsystem 自身是 UMassSubsystemBase 的子类，但它在 Initialize 中
//     做了"特例处理"——见 .cpp 文件中 IsChildOf 的判断。
// =============================================================================

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassTypeManager.h"
#include "Subsystems/SubsystemCollection.h"
#include "MassEntityConcepts.h"
#include "MassSubsystemBase.generated.h"

// MASSENTITY_API 的简写，下方所有需要导出的成员都使用 UE_API 标注；文件末尾会 #undef
#define UE_API MASSENTITY_API

struct FMassEntityManager;

namespace UE::Mass
{
	namespace Subsystems
	{
		/**
		 * FInitializationState
		 * --------------------
		 * 中文说明：
		 *   一个 1 字节大小的 bitfield，用作"簿记 / bookkeeping"，记录子系统的三个生命周期
		 *   钩子是否已经被调用过。它的存在是为了支持两类异常情况：
		 *     1) HandleLateCreation()：当子系统是在 World 已经 BeginPlay 之后才被创建出来
		 *        （如 GameplayFeatureActions 在游戏中开关 feature），此时需要补打
		 *        PostInitialize 和 OnWorldBeginPlay 两次回调，但又必须避免重复触发。
		 *     2) ensure 校验：基类的 Initialize/PostInitialize 等被多次调用时给出诊断
		 *        信息（理论上不应发生，除非业务方手动调用了不该调的接口）。
		 */
		struct FInitializationState
		{
			/** Initialize 是否已经被调用过（1 bit） */
			uint8 bInitializeCalled : 1 = false;
			/** PostInitialize 是否已经被调用过（1 bit） */
			uint8 bPostInitializeCalled : 1 = false;
			/** OnWorldBeginPlay 是否已经被调用过（1 bit） */
			uint8 bOnWorldBeginPlayCalled : 1 = false;
		};

		/**
		 * 把一个具体的子系统类（SubsystemClass）登记到 FTypeManager，用 Traits 描述其访问特征。
		 * 中文说明：
		 *   * 这个重载从 FSubsystemCollectionBase 间接拿到 UMassEntitySubsystem，再拿到默认的
		 *     FMassEntityManager；适合在派生类的 Initialize(Collection) 里使用。
		 *   * 之所以单独抽成自由函数，是为了让模板 OverrideSubsystemTraits<T> 不会因把
		 *     EntityManager 的 include 拉进 .h 而引发循环 include。
		 *
		 * @param Collection       USubsystem 集合（来自 Initialize 参数）
		 * @param SubsystemClass   需要登记的子系统类（通常是 GetClass()）
		 * @param Traits           该子系统的访问特征（GameThreadOnly / ThreadSafe 等）
		 */
		MASSENTITY_API void RegisterSubsystemType(FSubsystemCollectionBase& Collection, TSubclassOf<USubsystem> SubsystemClass, FSubsystemTypeTraits&& Traits);

		/**
		 * 同上，但直接传入 EntityManager 引用。
		 * 中文说明：
		 *   * UMassEntitySubsystem 自身做注册时使用此重载（见 MassEntitySubsystem.cpp）：
		 *     因为 UMassEntitySubsystem 才是 EntityManager 的"宿主"，它不能再走第一个重载
		 *     去通过 Collection 反过来找自己。
		 */
		MASSENTITY_API void RegisterSubsystemType(TSharedRef<FMassEntityManager> EntityManager, TSubclassOf<USubsystem> SubsystemClass, FSubsystemTypeTraits&& Traits);
	}
}

/** 
 * The sole responsibility of this world subsystem class is to serve functionality common to all 
 * Mass-related UWorldSubsystem-based subsystems, like whether the subsystems should get created at all. 
 *
 * 中文说明（UMassSubsystemBase —— Mass 与 UE 子系统体系的"标准底座"）：
 *   * 这是 Mass 框架中所有"非 Tickable"自定义 WorldSubsystem 的公共基类。约定：
 *     业务方的自定义 Mass 子系统应继承本类，而不是直接继承 UWorldSubsystem。
 *   * 提供 4 项核心能力：
 *       1. ShouldCreateSubsystem()：根据 mass.RuntimeSubsystemsEnabled 在某些 World 上
 *          直接跳过创建（如纯编辑器、Inactive World）。
 *       2. 在 Initialize 中自动把自己（GetClass()）登记到 FTypeManager（默认 traits）。
 *          子类如果有不同的并发访问特征，可在自己的 Initialize 中调用
 *          OverrideSubsystemTraits<T>() 覆盖。
 *       3. HandleLateCreation()：让"在 World BeginPlay 之后才创建出来的"子系统也能
 *          正确地走完 PostInitialize / OnWorldBeginPlay 流程。
 *       4. InitializationState：簿记初始化阶段，配合 ensure 防御重复调用。
 *
 * 反射宏说明：
 *   * UCLASS(Abstract)        —— 不能直接实例化，必须被子类化。
 *   * config = Mass            —— 该类的 ini 配置文件名为 Mass（Engine/Config/Mass.ini）。
 *   * defaultconfig            —— ini 仅写入 DefaultMass.ini（不写 Saved/Game ini）。
 *   * MinimalAPI               —— 仅导出 UClass* 反射符号，普通方法须用 UE_API 显式导出。
 */
UCLASS(Abstract, config = Mass, defaultconfig, MinimalAPI)
class UMassSubsystemBase : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 静态查询：当前进程/世界是否允许创建运行期 Mass 子系统。
	 * 中文说明：
	 *   * 实际只看一个 CVar：mass.RuntimeSubsystemsEnabled（默认 true）。
	 *   * 关掉它可以在不重新编译的情况下，禁掉所有继承自本基类的 Mass 子系统的自动创建——
	 *     用于排障、性能对比、或在某些工具/服务器场景里不需要 Mass 时省开销。
	 *   * 注意：必须在 World 加载之前设置才生效。
	 *   * Outer 当前未使用，保留以兼容 ShouldCreateSubsystem 的签名以便将来按 World 类型细分。
	 */
	static UE_API bool AreRuntimeMassSubsystemsAllowed(UObject* Outer);

	/** 中文：返回当前的初始化阶段位图，主要供调试 / late-creation 路径使用。 */
	UE::Mass::Subsystems::FInitializationState GetInitializationState() const { return InitializationState; }

protected:
	//~USubsystem interface
	/**
	 * 中文：UE 子系统创建过滤入口。
	 *   逻辑 = AreRuntimeMassSubsystemsAllowed() && Super::ShouldCreateSubsystem()
	 *   即：先看 Mass 的全局开关，再看默认的 WorldSubsystem 过滤（World 类型等）。
	 */
	UE_API virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 中文：子系统初始化第一阶段——
	 *   * 标记 bInitializeCalled=true（并 ensure 没被重复调）。
	 *   * 把自身类登记到 FTypeManager（除非自己就是 UMassEntitySubsystem，那是特例）。
	 *   * 子类 override 时务必先调 Super::Initialize()。
	 */
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * 中文：子系统初始化第二阶段——
	 *   * 在 USubsystemCollection 中所有被依赖的子系统都已 Initialize 之后调用，适合做
	 *     跨子系统访问的初始化逻辑。
	 *   * 标记 bPostInitializeCalled=true。
	 */
	UE_API virtual void PostInitialize() override;

	/**
	 * 中文：World 析构 / 切换时的清理。
	 *   * 把 InitializationState 整体重置（这样如果同一个对象被复用，可以再次走完整初始化）。
	 *   * 子类 override 时建议在末尾调用 Super::Deinitialize()。
	 */
	UE_API virtual void Deinitialize() override;

	/**
	 * 中文：World BeginPlay 时机。
	 *   * 标记 bOnWorldBeginPlayCalled=true。
	 *   * 子类如有"游戏开始时才需要做"的初始化逻辑（如生成持久化实体），放在这里。
	 */
	UE_API virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~End of USubsystem interface

	/**
	 * Needs to be called in Initialize for subsystems we want to behave properly when dynamically added after UWorld::BeginPlay
	 * (for example via GameplayFeatureActions). This is required for subsystems relying on their PostInitialize and/or OnWorldBeginPlay called.
	 *
	 * 中文说明（HandleLateCreation 延迟创建补救）：
	 *   * 触发场景：World 已经 BeginPlay 了，结果 GameplayFeature 在运行中被启用，
	 *     带来一组新的子系统。UE 默认只会调用 Initialize，但 PostInitialize 与
	 *     OnWorldBeginPlay 已经"过点"了，不会被自动补打。
	 *   * 本函数检查 World 的当前阶段：若已 IsInitialized() 但本子系统还没 PostInitialize 过，
	 *     则马上调 PostInitialize；同理 OnWorldBeginPlay。
	 *   * 调用约定：在子类自己的 Initialize() 里调用一次（已是 Super::Initialize 之后即可）。
	 *   * 注意：UMassEntitySubsystem 的 .cpp 中已经调用，因此凡是它的"依赖型子系统"被
	 *     Initialize() 时通过 InitializeDependency<UMassEntitySubsystem>() 触发，会自动获益。
	 */
	UE_API void HandleLateCreation();

	/**
	 * Registers given subsystem class as part of Mass type information. Needs to be called as part of Initialize override.
	 * Note that calling the function is only required if the registered traits differ from the parent class'.
	 *
	 * 中文说明（OverrideSubsystemTraits<T>）：
	 *   * 模板参数 T 必须满足 UE::Mass::CSubsystem concept（即继承 USubsystem）。
	 *   * 典型使用：在子类的 Initialize() 里声明自己的并发访问特征，例如把自己声明为
	 *     "GameThreadOnly"（默认）或 "ThreadSafeWrite"，从而让 Mass 的 Processor 调度
	 *     器（Wave M9 DependencySolver）正确做并发依赖求解。
	 *   * 调用时机：必须在 Initialize 中。Super::Initialize 已经用"父类的 traits"登记过一次，
	 *     这里覆盖；因此只有当子类的 traits 与父类不同才需要调用。
	 */
	template <UE::Mass::CSubsystem T>
	void OverrideSubsystemTraits(FSubsystemCollectionBase& Collection)
	{
		UE::Mass::Subsystems::RegisterSubsystemType(Collection, T::StaticClass(), UE::Mass::FSubsystemTypeTraits::Make<T>());
	}

	/**
	 * Tracks which initialization function had already been called. Requires the child classes to call Super implementation
	 * for their Initialize, PostInitialize, Deinitialize and OnWorldBeginPlayCalled overrides
	 *
	 * 中文：3 个 1-bit 标志的合集，详见 FInitializationState。
	 *   ensure 校验依赖它，HandleLateCreation 也通过它判断"哪些钩子还没补"。
	 */
	UE::Mass::Subsystems::FInitializationState InitializationState;
};

/**
 * The sole responsibility of this tickable world subsystem class is to serve functionality common to all
 * Mass-related UTickableWorldSubsystem-based subsystems, like whether the subsystems should get created at all.
 *
 * 中文说明（UMassTickableSubsystemBase）：
 *   * 与 UMassSubsystemBase 几乎一一对应，区别仅在于父类是 UTickableWorldSubsystem——
 *     即子类支持自动 Tick。
 *   * 凡是需要每帧执行（而非依赖 Mass Processor 调度器）的 Mass 子系统应继承本类。
 *   * 大多数 Mass 业务逻辑应通过 Processor 而非每帧 Tick 完成；这条路径主要给"非 ECS"
 *     的辅助系统（如 logger、debug 渲染、外部联动）使用。
 */
UCLASS(Abstract, config = Mass, defaultconfig, MinimalAPI)
class UMassTickableSubsystemBase : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 中文：返回初始化阶段位图。 */
	UE::Mass::Subsystems::FInitializationState GetInitializationState() const { return InitializationState; }

protected:
	//~USubsystem interface
	/** 中文：同 UMassSubsystemBase::ShouldCreateSubsystem，受 mass.RuntimeSubsystemsEnabled 控制。 */
	UE_API virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	/** 中文：标记 bInitializeCalled，并把 GetClass() 登记到 FTypeManager 使用 UMassTickableSubsystemBase 的默认 traits。 */
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** 中文：标记 bPostInitializeCalled。 */
	UE_API virtual void PostInitialize() override;
	/** 中文：重置 InitializationState。 */
	UE_API virtual void Deinitialize() override;
	/** 中文：标记 bOnWorldBeginPlayCalled。 */
	UE_API virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~End of USubsystem interface

	/**
	 * Registers given subsystem class as part of Mass type information. Needs to be called as part of Initialize override.
	 * Note that calling the function is only required if the registered traits differ from the parent class'.
	 *
	 * 中文：含义同 UMassSubsystemBase::OverrideSubsystemTraits。
	 */
	template <UE::Mass::CSubsystem T>
	void OverrideSubsystemTraits(FSubsystemCollectionBase& Collection)
	{
		UE::Mass::Subsystems::RegisterSubsystemType(Collection, T::StaticClass(), UE::Mass::FSubsystemTypeTraits::Make<T>());
	}

	/**
	 * Needs to be called in Initialize for subsystems we want to behave properly when dynamically added after UWorld::BeginPlay
	 * (for example via GameplayFeatureActions). This is required for subsystems relying on their PostInitialize and/or OnWorldBeginPlay called.
	 *
	 * 中文：含义同 UMassSubsystemBase::HandleLateCreation。
	 */
	UE_API void HandleLateCreation();

private:
	/** 
	 * Tracks which initialization function had already been called. Requires the child classes to call Super implementation
	 * for their Initialize, PostInitialize, Deinitialize and OnWorldBeginPlayCalled overrides
	 *
	 * 中文：与 UMassSubsystemBase 同名字段一致，用作生命周期簿记。
	 *       这里设为 private（而非 protected），意味着 UMassTickableSubsystemBase 的子类
	 *       只能通过 GetInitializationState() 读取，不能直接修改——较 UMassSubsystemBase 更严格。
	 */
	UE::Mass::Subsystems::FInitializationState InitializationState;
};

#undef UE_API