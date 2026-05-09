// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationFragmentUtil.cpp —— Fragment 自动构建工具的实现
// ---------------------------------------------------------------------------------------------------------------------
// 关键流程（参见 .h 文件头部说明）：
//   1) 解析 DefaultStateSource：CDO / Template / Archetype 三选一；
//   2) 让 FReplicationStateDescriptorBuilder 反射烘焙 Class 的所有 Descriptor；
//   3) 对每个 Descriptor 决定 Fragment 实现：
//        - 优先调用 Descriptor 自带的 CreateAndRegisterReplicationFragmentFunction（FastArray / 用户自定义）；
//        - 否则使用通用 FPropertyReplicationFragment::CreateAndRegisterFragment；
//   4) 若没有任何 fragment 被注册，把 Context 标记为 "FragmentlessNetObject"。
// =====================================================================================================================

#include "Iris/ReplicationSystem/ReplicationFragmentUtil.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/PropertyReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationFragmentInternal.h"
#include "Iris/ReplicationSystem/FastArrayReplicationFragment.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisLog.h"

namespace UE::Net
{

// 简版包装：参数转发到全功能版。
uint32 FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(UObject* Object, FFragmentRegistrationContext& Context, EFragmentRegistrationFlags RegistrationFlags, TArray<FReplicationFragment*>* OutCreatedFragments)
{
	const FCreateFragmentParams CreateParams
	{
		.ObjectInstance = Object,
		.RegistrationFlags = RegistrationFlags,
	};

	return CreateAndRegisterFragmentsForObject(CreateParams, Context, OutCreatedFragments);
}

uint32 FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(const FCreateFragmentParams& Params, FFragmentRegistrationContext& Context, TArray<FReplicationFragment*>* OutCreatedFragments)
{
	using namespace UE::Net::Private;

	IRIS_PROFILER_SCOPE(FReplicationFragmentUtil_CreateAndRegisterFragmentsForObject);

	// 准备 Builder 的参数：复用 Context 中保存的 Descriptor 注册表（去重缓存）+ ReplicationSystem 句柄。
	FReplicationStateDescriptorBuilder::FParameters BuilderParameters;
	BuilderParameters.DescriptorRegistry = FFragmentRegistrationContextPrivateAccessor::GetReplicationStateRegistry(Context);
	BuilderParameters.ReplicationSystem = FFragmentRegistrationContextPrivateAccessor::GetReplicationSystem(Context);

	UClass* ObjectClass = Params.ObjectInstance->GetClass();

	// 是否本次注册的"主对象"——主对象走 Template/Archetype/CDO 选择；子对象不走。
	const bool bIsMainObject = FFragmentRegistrationContextPrivateAccessor::IsMainObject(Context, Params.ObjectInstance);

	// Find the state source needed by some fragments
	// ── 解析 DefaultStateSource ──
	{
		const UObject* DefaultStateSource = nullptr;

		// Some objects want to always build the init state from the CDO
		// 显式标志强制使用 CDO（典型于动态生成对象、不能用 archetype 的场景）。
		if (EnumHasAnyFlags(Params.RegistrationFlags, EFragmentRegistrationFlags::InitializeDefaultStateFromClassDefaults))
		{
			DefaultStateSource = ObjectClass->GetDefaultObject();
		}
		// For the main object look if a template was provided
		// 主对象优先使用 caller 传入的 Template（蓝图等场景需要这个）。
		else if (bIsMainObject)
		{
			DefaultStateSource = FFragmentRegistrationContextPrivateAccessor::GetTemplate(Context);
		}
	
		// Default to use the archetype otherwise
		// 缺省回退到 Archetype（普通 UObject 的标准行为）。
		if (DefaultStateSource == nullptr)
		{
			DefaultStateSource = Params.ObjectInstance->GetArchetype();
		}

		// 如果连 Archetype 都不可用（极端异常），退到 CDO 并打错误日志。
		if (!IsValid(DefaultStateSource))
		{
			UE_LOG(LogIris, Error, TEXT("FPropertyReplicationFragment::CreateAndRegisterFragmentsForObject: Invalid object archetype for object %s, default state will use the CDO"), *GetFullNameSafe(Params.ObjectInstance));
			DefaultStateSource = ObjectClass->GetDefaultObject();
		}

		// 仅主对象需要写回 Context 让 Bridge 可读取（子对象的默认状态来源由更上层管控）。
		if (bIsMainObject)
		{
			// Store the state source so the bridge can retrieve it
			FFragmentRegistrationContextPrivateAccessor::SetDefaultStateSource(Context, DefaultStateSource);
		}

		BuilderParameters.DefaultStateSource = DefaultStateSource;
	}

	// Pass-on that we allow FastAarrays with extra replicated properties for this object.
	// NOTE: this is not perfect as it allows this for all FastArray properties of the object but as there 
	// is further validation in the actual ReplicationStateFragment implementation for FastArrays it is good enough.
	// FastArray 兼容："允许 FastArray 携带额外属性"的 flag 在此对象级别开启；
	// 这是粗粒度开关（影响该对象所有 FastArray 字段），但 FastArray fragment 内部还会再做一次验证。
	if (EnumHasAnyFlags(Params.RegistrationFlags, EFragmentRegistrationFlags::AllowFastArraysWithAdditionalProperties))
	{
		BuilderParameters.AllowFastArrayWithExtraReplicatedProperties = 1U;
	}

	// ── 反射烘焙 ──：让 Builder 给整个 UClass 创建 Descriptor 数组（每个 ReplicationState 一份）。
	FReplicationStateDescriptorBuilder::FResult Result;
	FReplicationStateDescriptorBuilder::CreateDescriptorsForClass(Result, ObjectClass, BuilderParameters);

	// 仅 RPC 模式：跳过没有 UFunction 的 Descriptor。
	const bool bRegisterFunctionsOnly = EnumHasAnyFlags(Params.RegistrationFlags, EFragmentRegistrationFlags::RegisterRPCsOnly);

	uint32 NumCreatedReplicationFragments = 0U;
	// create fragments and let the instance protocol own them.
	// 为每个 Descriptor 实例化对应的 Fragment。
	for (TRefCountPtr<const FReplicationStateDescriptor>& Desc : Result)
	{
		if (bRegisterFunctionsOnly && (Desc->FunctionCount == 0))
		{
			continue;
		}

		FReplicationFragment* Fragment = nullptr;
		// If descriptor provides CreateAndRegisterReplicationFragment function we use that to instantiate fragment
		// FastArray / 用户自定义路径：Descriptor 烘焙时已写入函数指针。
		if (Desc->CreateAndRegisterReplicationFragmentFunction)
		{
			Fragment = Desc->CreateAndRegisterReplicationFragmentFunction(Params.ObjectInstance, Desc.GetReference(), Context);
		}
		else
		{
			// 通用路径：基于 UProperty 的 FPropertyReplicationFragment。
			Fragment = FPropertyReplicationFragment::CreateAndRegisterFragment(Params.ObjectInstance, Desc.GetReference(), Context);
		}
		if (Fragment && OutCreatedFragments)
		{
			OutCreatedFragments->Add(Fragment);
			++NumCreatedReplicationFragments;
		}
	}

	// If we did not find any fragments to create, tell the context it's known.
	// 没有 fragment 被注册，标记为"无 Fragment 网络对象"，避免上层抛错。
	if (Context.NumFragments() <= 0)
	{
		Context.SetIsFragmentlessNetObject(true);
	}

	return NumCreatedReplicationFragments;
}

}
