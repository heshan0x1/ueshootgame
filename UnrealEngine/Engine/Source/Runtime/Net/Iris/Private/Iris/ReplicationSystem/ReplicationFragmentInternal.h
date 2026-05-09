// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationFragmentInternal.h —— FFragmentRegistrationContext 的"私有访问者"
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Private/Iris/ReplicationSystem/  ← 仅 Iris 内部使用。
// 角色：FFragmentRegistrationContext（在公开头里）把若干字段声明为 private，但 Iris 自身的某些子系统（例如
//      ReplicationFragmentUtil、ObjectReplicationBridge、内部协议构建器）确实需要读写它们。本文件用一个
//      "PrivateAccessor"模式提供受控访问：
//        - 通过 friend 关系打开窗口（在 ReplicationFragment.h 中已声明 friend Private::FFragmentRegistrationContextPrivateAccessor）；
//        - 这里以纯静态方法暴露 Get/Set 接口，外界无法实例化 Accessor 也无法绕过编译期可见性。
//
// 暴露的能力：
//   - GetReplicationFragments —— 读已注册的 Fragments 列表（const）。
//   - GetReplicationStateRegistry / GetReplicationSystem —— 拿到 Iris 内部的描述符注册表与 ReplicationSystem 指针。
//   - IsMainObject —— 判断给定 UObject 是不是该 Context 的"主对象"（决定是否走 DefaultStateSource 路径）。
//   - SetDefaultStateSource / GetDefaultStateSource —— 内部记录"最终默认状态来源"（archetype / template / CDO 之一）。
//   - SetTemplate / GetTemplate —— 由 Bridge 传入 / 读取的可选模板对象。
//
// 这种"PrivateAccessor"是 UE 代码风格之一：保留对外 API 干净的同时，把 Iris 内部的紧耦合需求集中在一处可审查的位置。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/ReplicationFragment.h"

namespace UE::Net::Private
{
	// FFragmentRegistrationContextPrivateAccessor
	// 仅 Iris 内部代码可调用；通过 friend 声明跨过 Context 的 private 限制。
	struct FFragmentRegistrationContextPrivateAccessor
	{
		// 读取已注册的 fragment 列表，供 Bridge 后续构造 InstanceProtocol。
		static const FReplicationFragments& GetReplicationFragments(const FFragmentRegistrationContext& Context)
		{ 
			return Context.Fragments;
		}
		
		// 拿到 Iris 内部的"描述符注册表"——在 fragment 创建过程中把同一类对象的 Descriptor 复用。
		static const Private::FReplicationStateDescriptorRegistry* GetReplicationStateRegistry(const FFragmentRegistrationContext& Context)
		{ 
			return Context.ReplicationStateRegistry;
		}
		
		// 同上但非 const：少数路径需要"按需创建"新的 Descriptor 并写回注册表。
		static Private::FReplicationStateDescriptorRegistry* GetReplicationStateRegistry(FFragmentRegistrationContext& Context)
		{ 
			return Context.ReplicationStateRegistry;
		}
		
		// 取 ReplicationSystem 指针，供 fragment 在注册期访问 config / pushmodel 接口等。
		static UReplicationSystem* GetReplicationSystem(const FFragmentRegistrationContext& Context)
		{ 
			return Context.ReplicationSystem;
		}

		// 判断给定 UObject 是否是此次注册的"主对象"——主对象走 Template/archetype 默认状态路径，
		// 子对象 / 组件通常已知 archetype，不走这条特殊处理。
		static bool IsMainObject(const FFragmentRegistrationContext& Context, UObject* ObjectInstance)
		{
			return Context.MainObjectInstance == ObjectInstance;
		}

		// Bridge 在确定"最终默认状态来源对象"之后把指针写回 Context，下游构造时直接用此对象生成 default state buffer。
		static void SetDefaultStateSource(FFragmentRegistrationContext& Context, const UObject* DefaultStateSource)
		{
			Context.MainObjectDefaultStateSource = DefaultStateSource;
		}

		// 读回 SetDefaultStateSource 写入的对象（archetype / template / CDO 之一）。
		static const UObject* GetDefaultStateSource(const FFragmentRegistrationContext& Context)
		{
			return Context.MainObjectDefaultStateSource;
		}

		// 由 Bridge 在创建 Context 时设置：调用方显式提供的"模板"对象（替代 archetype 的来源）。
		static void SetTemplate(FFragmentRegistrationContext& Context, const UObject* Template)
		{
			Context.Template = Template;
		}

		// 读回 SetTemplate 写入的模板对象；若未设置则为 nullptr，下游会回退到 archetype。
		static const UObject* GetTemplate(const FFragmentRegistrationContext& Context)
		{
			return Context.Template;
		}
	};
}
