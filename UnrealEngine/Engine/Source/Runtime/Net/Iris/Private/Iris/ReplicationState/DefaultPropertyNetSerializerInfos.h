// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// DefaultPropertyNetSerializerInfos.h —— 引擎内置 Property 类型默认 NetSerializer 注册入口
// ---------------------------------------------------------------------------------------------------------------------
// 在 FIrisCoreModule::StartupModule 调用的 RegisterPropertyNetSerializerSelectorTypes 中：
//   PreFreeze 广播  → 各模块 NetSerializerRegistryDelegates 自动 Register
//   ↓
//   RegisterDefaultPropertyNetSerializerInfos()  ← 本头文件提供的入口，注册引擎核心 Property → NetSerializer 映射
//   ↓
//   FPropertyNetSerializerInfoRegistry::Freeze()
//   ↓
//   PostFreeze 广播
//
// 因此本入口是"引擎层补齐"的部分——把 Int8/Int16/Float/Object/Enum/Bool/Vector/Quat/Guid/... 这种
// 100% 必备的标准类型一次性写死注册。游戏侧通常只需要补充自定义 USTRUCT。
// =====================================================================================================================

#pragma once

namespace UE::Net::Private
{

/**
 * Register supported property types and their default NetSerializers
 */
void RegisterDefaultPropertyNetSerializerInfos();

}
