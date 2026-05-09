// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "MassSettings.generated.h"

#define UE_API MASSENTITY_API

// =============================================================================================
// MassSettings.h —— Mass 框架在 Project Settings 中的"插件汇总点"（Aggregator）
// ---------------------------------------------------------------------------------------------
// 设计目标（"配置即代码"思想的体现）：
//   Mass 由多个独立的子模块组成（MassEntity、MassAI、MassGameplay、MassCrowd、...），
//   每个子模块都可能有自己的可配置参数。如果每个子模块都单独注册一个 Project Settings 页面，
//   会让 UI 散乱、用户不易找到。Mass 的解决方案是：
//
//      ┌─────────────────────────────────────────────────────────────────┐
//      │  Project Settings → Plugins → Mass    (UMassSettings 单一入口)  │
//      ├─────────────────────────────────────────────────────────────────┤
//      │   ├─ Mass Entity   (UMassEntitySettings    : UMassModuleSettings)│
//      │   ├─ Mass AI       (UMassAISettings        : UMassModuleSettings)│
//      │   ├─ Mass Gameplay (UMassGameplaySettings  : UMassModuleSettings)│
//      │   └─ ... 其他派生类                                              │
//      └─────────────────────────────────────────────────────────────────┘
//
//   - UMassSettings    : UDeveloperSettings —— 真正在 Project Settings 中"显形"的那一个
//   - UMassModuleSettings : UObject       —— 抽象基类；子模块只要派生它就会被自动收纳
//
// 自动注册机制（"派生即注册"）：
//   UMassModuleSettings::PostInitProperties() 会在 CDO 构造完成后自动调用，
//   通过 UMassSettings::RegisterModuleSettings() 把 CDO 写入 ModuleSettings TMap。
//   对插件作者而言：只需声明 `class UMyMassSettings : public UMassModuleSettings {...}`，
//   不必再手动调注册函数，子页面就会出现在 Mass 的折叠面板下。
// =============================================================================================


/** 
 * A common parrent for Mass's per-module settings. Classes extending this class will automatically get registered 
 * with- and show under Mass settings in Project Settings.
 *
 * 中文：Mass 各子模块设置的公共抽象基类。任何派生自本类的非抽象 UCLASS（CDO）都会在
 *      构造时自动注册到 UMassSettings 的 ModuleSettings 容器中，并以子页面形式
 *      显示在 "Project Settings → Plugins → Mass" 面板下。
 *
 * UCLASS 元参数说明：
 *   - MinimalAPI       : 仅导出 UClass*/ /*运行时反射符号，节省 dll 导出体积
 *   - Abstract         : 本类不能直接实例化（CDO 不会注册到 UMassSettings，详见 .cpp）
 *   - config = Mass    : 该类的 config UPROPERTY 持久化到 DefaultMass.ini（Engine/Project 各一份）
 *   - defaultconfig    : 仅写入 DefaultMass.ini（不写入 per-user 的 Saved/Config 路径）
 *   - collapseCategories: 编辑器中所有 UPROPERTY 折叠在一起，不再按 Category 分组
 */
UCLASS(MinimalAPI, Abstract, config = Mass, defaultconfig, collapseCategories)
class UMassModuleSettings : public UObject
{
	GENERATED_BODY()
protected:
	/**
	 * UObject 标准生命周期回调：CDO 的属性反序列化（从 ini 读入 config 字段）完成后被调用。
	 * 此处用作"自注册"钩子：
	 *   if (是 CDO 且 类不是 Abstract)
	 *       将自己交给 UMassSettings::RegisterModuleSettings() 收编。
	 * 派生类如需添加自身初始化逻辑，应记得调用 Super::PostInitProperties() 以保证注册不被跳过。
	 */
	UE_API virtual void PostInitProperties() override;
};


/**
 * Mass 在 Project Settings 中的统一入口（开发者设置）。
 * 它本身几乎不持有"业务字段"，仅作为所有 UMassModuleSettings 派生类的展示容器。
 *
 * UCLASS 元参数说明：
 *   - DisplayName = "Mass"            : Project Settings 树中显示的名字（不带前缀 U）
 *   - AutoExpandCategories = "Mass"   : 默认展开 Category="Mass" 下的全部属性
 *   - config = Mass / defaultconfig   : 同 UMassModuleSettings，配置文件落到 DefaultMass.ini
 *
 * UDeveloperSettings 自动机制：UE 反射系统会扫描所有 UDeveloperSettings 派生类，把它们
 *   注入到 Project Settings UI 的对应分类下（默认是 "Plugins" / "Engine" 等）。所以
 *   开发者不必手动调用任何"注册到 UI"的 API。
 */
UCLASS(MinimalAPI, config = Mass, defaultconfig, DisplayName = "Mass", AutoExpandCategories = "Mass")
class UMassSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * 把一个 UMassModuleSettings 子类的 CDO 加入 ModuleSettings 容器。
	 * 由 UMassModuleSettings::PostInitProperties() 自动调用，外部一般不需要手动调用。
	 *
	 * @param SettingsCDO  待注册的 CDO（必须带 RF_ClassDefaultObject 标志）
	 */
	UE_API void RegisterModuleSettings(UMassModuleSettings& SettingsCDO);

public:
	/**
	 * 子模块设置的索引表：Key 是 类名（或 DisplayName meta），Value 是子设置 CDO。
	 *
	 * UPROPERTY 元参数说明：
	 *   - VisibleAnywhere : 编辑器中只读显示；用户不能改 Map 本身的结构
	 *   - NoClear         : 禁止清空 / 解除引用（防止误操作丢失子页面）
	 *   - EditFixedSize   : 数组/Map 的大小固定，不能添加 / 删除元素（由代码自动维护）
	 *   - meta=(EditInline): 内联展开 Value（即直接在该面板里编辑子 Settings 的字段，
	 *                       而不是显示一个 ObjectPicker）—— 这正是"子页面在 UI 中展开"的关键。
	 */
	UPROPERTY(VisibleAnywhere, Category = "Mass", NoClear, EditFixedSize, meta = (EditInline))
	TMap<FName, TObjectPtr<UMassModuleSettings>> ModuleSettings;
};

#undef UE_API
