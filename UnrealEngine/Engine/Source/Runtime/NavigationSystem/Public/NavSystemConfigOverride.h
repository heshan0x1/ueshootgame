// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavSystemConfigOverride.h
// -----------------------------------------------------------------------------
// 允许在某个关卡里"局部覆盖"全局的 NavigationSystem 配置。
// 用法：把 ANavSystemConfigOverride 放进关卡，设置里面的 NavigationSystemConfig
// 与 OverridePolicy。Actor 在 BeginPlay / 编辑器 PostRegisterAllComponents 时
// 触发 ApplyConfig：
//
//   OverridePolicy = Override → 销毁已有 NavSystem，重建一个新实例（可能换类）
//                 = Append   → 合并非冲突配置（例如追加 SupportedAgents）
//                 = Skip     → 若已有 NavSystem 则本 Override 被忽略
//
// 典型场景：主关卡使用 Static NavMesh，某张大地图在自己关卡里 Override 为 Dynamic。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "AI/NavigationSystemBase.h"
#include "NavSystemConfigOverride.generated.h"


class UNavigationSystemConfig;

// 覆盖策略：控制 Override Actor 与世界里已有的 NavigationSystem 如何合并。
UENUM()
enum class ENavSystemOverridePolicy : uint8
{
	Override, // the pre-existing nav system instance will be destroyed.
	           // 销毁已有 NavSystem，按本 Actor 的 Config 全新创建一套。
	Append, // config information will be added to pre-existing nav system instance
	         // 把非冲突字段追加到已有 NavSystem（常用于在子关卡追加 Agent）。
	Skip	// if there's already a NavigationSystem in the world then the overriding config will be ignored
	         // 若 World 已有 NavSystem 则完全忽略本 Override。
};


// 关卡 NavigationSystem 配置覆盖 Actor。
UCLASS(hidecategories = (Input, Rendering, Actor, LOD, Cooking), MinimalAPI)
class ANavSystemConfigOverride : public AActor
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
private:
	// 编辑器图标（关卡视口里看到的那个 Billboard）
	UPROPERTY()
	TObjectPtr<class UBillboardComponent> SpriteComponent;
#endif // WITH_EDITORONLY_DATA

protected:
	// 要使用的 NavigationSystem 配置（Instanced，可直接在属性面板编辑子属性）。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation, Instanced,  meta = (NoResetToDefault))
	TObjectPtr<UNavigationSystemConfig> NavigationSystemConfig;

	/** If there's already a NavigationSystem instance in the world how should this nav override behave */
	// 与已有 NavSystem 的冲突处理策略
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation)
	ENavSystemOverridePolicy OverridePolicy;

	// 网络复制：是否把本 Override 下发到客户端（客户端 NavSystem 配置通常和服务器不同）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Navigation, AdvancedDisplay)
	uint8 bLoadOnClient : 1;

public:
	NAVIGATIONSYSTEM_API ANavSystemConfigOverride(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin UObject Interface
	NAVIGATIONSYSTEM_API virtual void PostInitProperties() override;
#if WITH_EDITOR
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin AActor Interface
	// 运行时入口：BeginPlay 时触发 ApplyConfig —— 实际走 Override/Append 逻辑。
	NAVIGATIONSYSTEM_API virtual void BeginPlay() override;
#if WITH_EDITOR
	// 编辑器里拖进关卡的瞬间也尝试应用；反向组件反注册时不处理已有 NavSystem。
	NAVIGATIONSYSTEM_API virtual void PostRegisterAllComponents() override;
	NAVIGATIONSYSTEM_API virtual void PostUnregisterAllComponents() override;
#endif
	//~ End AActor Interface

#if WITH_EDITOR
	/** made an explicit function since rebuilding navigation system can be expensive */
	// 编辑器按钮：手动应用 Config（因为重建 NavSystem 代价大，不默认自动跑）。
	UFUNCTION(Category = Navigation, meta = (CallInEditor = "true"))
	NAVIGATIONSYSTEM_API void ApplyChanges();
	//virtual void CheckForErrors() override;
#endif

protected:
	/** Creates a new navigation system and plugs it into the world. If there's a
	 *	nav system instance already in place it gets destroyed. */
	// Override 策略实现：销毁旧 NavSystem → 按 Config.NavigationSystemClass 构建新实例。
	NAVIGATIONSYSTEM_API virtual void OverrideNavSystem();

	/** Appends non-conflicting information (like supported agents) to a pre-existing 
	 *	nav system instance */
	// Append 策略实现：把 Config 里的非冲突字段（如 SupportedAgents）合并入已有 NavSystem。
	NAVIGATIONSYSTEM_API virtual void AppendToNavSystem(UNavigationSystemBase& PrevNavSys);

#if WITH_EDITOR
	/** Called only in the editor mode*/
	// 编辑器特殊路径：为 RunMode 对应的 World 正确初始化 NewNavSys。
	NAVIGATIONSYSTEM_API void InitializeForWorld(UNavigationSystemBase* NewNavSys, UWorld* World, const FNavigationSystemRunMode RunMode);
#endif // WITH_EDITOR

	// 调度入口：读取 OverridePolicy 决定走 OverrideNavSystem / AppendToNavSystem / 跳过。
	NAVIGATIONSYSTEM_API void ApplyConfig();
};
