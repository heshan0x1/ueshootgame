// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptor.cpp —— Descriptor 引用计数 + 描述输出
// -------------------------------------------------------------------------------------
// 本文件实现 FReplicationStateDescriptor 的两个非 inline 接口：
//   1) AddRef / Release —— atomic 引用计数；
//      Release 至 0 时还要负责"析构链式资源"：
//        a) DefaultStateBuffer（若 HasDynamicState 还需先 FreeDynamicState）；
//        b) 非默认 SerializerConfig（若标记了 NeedDestruction）；
//        c) MemberFunctionDescriptors 链上的 Descriptor->Release；
//        d) 最后 Free 整块 Descriptor 内存（由 Builder 单次 malloc 分配）。
//   2) DescribeReplicationDescriptor —— 调试字符串输出，
//      逐成员打印 ExternalOffset / InternalOffset / ChangeMaskBits / Serializer 名字。
// =====================================================================================

#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializer.h"
#include "AutoRTFM.h"
#include "HAL/PlatformAtomics.h"
#include "UObject/UnrealType.h"
#include "UObject/CoreNetTypes.h"
#include "Templates/IsSigned.h"

namespace UE::Net
{

//static_assert(TIsSigned<__underlying_type(ELifetimeCondition)>::Value == TIsSigned<decltype(FReplicationStateMemberLifetimeConditionDescriptor::Condition)>::Value, "FReplicationStateMemberLifetimeConditionDescriptor::Condition is not compatible with ELifetimeCondition");
// 编译期校验 LifetimeCondition 紧凑存储位宽足够：把 ELifetimeCondition 压成 int8 不能截断。
static_assert(static_cast<__underlying_type(ELifetimeCondition)>(ELifetimeCondition::COND_Max - 1) <= TNumericLimits<decltype(FReplicationStateMemberLifetimeConditionDescriptor::Condition)>::Max(), "FReplicationStateMemberLifetimeConditionDescriptor::Condition is not compatible with ELifetimeCondition");

// 【DescribeReplicationDescriptor】把 Descriptor 的关键字段拼到字符串里（调试/日志）。
// 输出格式（行文本）：
//   FReplicationStateDescriptor : <Identifier>
//   MemberCount : N
//   ExternalBufferSize / Alignment / InternalBufferSize / Alignment / ChangeMaskBitCount
//   ..[每成员] PropertyName Type / Serializer / ExternalOffset / InternalOffset / ChangeMaskBits
//
// 当 MemberProperties 非空（Class/Struct 路径）走"PropertyName + CPPType"风格；
// 否则（手写 State 路径）回退到 "Member:<index>"。
void DescribeReplicationDescriptor(FString& OutString, const FReplicationStateDescriptor* Descriptor)
{
	uint32 MemberCount = Descriptor->MemberCount;

	OutString.Append(FString::Printf(TEXT("FReplicationStateDescriptor : %" UINT64_FMT "\n"), Descriptor->DescriptorIdentifier.Value));
	OutString.Append(FString::Printf(TEXT("MemberCount : %u\n"), MemberCount));
	OutString.Append(FString::Printf(TEXT("ExternalBufferSize : %u\n"), Descriptor->ExternalSize));
	OutString.Append(FString::Printf(TEXT("ExternalAlignment : %u\n"), Descriptor->ExternalAlignment));
	OutString.Append(FString::Printf(TEXT("InternalBufferSize : %u\n"), Descriptor->InternalSize));
	OutString.Append(FString::Printf(TEXT("InternalAlignment : %u\n"), Descriptor->InternalAlignment));

	OutString.Append(FString::Printf(TEXT("ChangeMaskBitCount : %u\n"), Descriptor->ChangeMaskBitCount));

	// 逐成员输出。MemberChangeMaskDescriptors 可能为 nullptr（Init State 无 ChangeMask）。
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = Descriptor->MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializer = Descriptor->MemberSerializerDescriptors[MemberIt];

		const uint32 ChangeMaskBitCount = Descriptor->MemberChangeMaskDescriptors ? Descriptor->MemberChangeMaskDescriptors[MemberIt].BitCount : 0u;

		if (Descriptor->MemberProperties)
		{
			// Class/Struct 路径：用 FProperty 的反射名打印
			const FProperty* Property = Descriptor->MemberProperties[MemberIt];
			OutString.Append(FString::Printf(TEXT("..%s Type: %s: Serializer: %s ExternalOffset %u, InternalOffset %u ChangeMaskBits: %u\n"), *Property->GetName(), *Property->GetCPPType(), MemberSerializer.Serializer->Name, MemberDescriptor.ExternalMemberOffset, MemberDescriptor.InternalMemberOffset, ChangeMaskBitCount));
		}
		else
		{
			// 手写 State 路径：无 FProperty，仅打 MemberIndex
			OutString.Append(FString::Printf(TEXT("..Member:%u Serializer: %s: ExternalOffset %u, InternalOffset %u ChangeMaskBits: %u\n"), MemberIt, MemberSerializer.Serializer->Name, MemberDescriptor.ExternalMemberOffset, MemberDescriptor.InternalMemberOffset, ChangeMaskBitCount));
		}
	}
}

// 【AddRef】只对 NeedsRefCount 标记的 Descriptor 实际生效（运行期由 Builder 创建的）；
// 编译期静态 Descriptor（手写状态）的 RefCount 不会被使用，所以早早 short-circuit。
//
// AutoRTFM 块：当 AddRef 处于一个事务中而事务被 abort 时，RefCount 会被自动回滚 --。
void FReplicationStateDescriptor::AddRef() const
{
	if (EnumHasAnyFlags(Traits, EReplicationStateTraits::NeedsRefCount))
	{
		UE_AUTORTFM_OPEN
		{
			++RefCount;
		};
		UE_AUTORTFM_ONABORT(this)
		{
			--RefCount;
		};
	}
}

// 【Release】引用计数减 1；归零时执行链式资源清理。
//
// 处于 AutoRTFM 事务中时，--RefCount 必须在事务 commit 后才执行（避免事务 abort 后误释放）。
//
// 归零清理顺序（必须严格按此顺序）：
//   1) DefaultStateBuffer：如果存在
//      a) HasDynamicState → FreeDynamicState 释放内嵌动态分配（TArray、FString 等）；
//      b) Free 缓冲本身；
//   2) SerializerConfig：遍历每成员，若 Config 标记了 NeedDestruction（例如 enum、struct
//      数组等"非默认 Config"，由 Builder 单独 malloc 出来）则手动调用析构；
//   3) MemberFunctionDescriptors：每个 Function 引用一个嵌套 Descriptor（参数列表 Descriptor），
//      Release 它们以解除引用链；
//   4) 最后 Free 整块 Descriptor 内存（包括所有子数组和 SerializerConfig，因为是单块 malloc）。
void FReplicationStateDescriptor::Release() const
{
	using namespace UE::Net::Private;

	if (EnumHasAnyFlags(Traits, EReplicationStateTraits::NeedsRefCount))
	{
		UE_AUTORTFM_ONCOMMIT(this)
		{
			if (--RefCount == 0)
			{
				// We must destruct default state if we have one
				// 释放默认状态缓冲（含动态成员要先 FreeDynamicState）
				if (uint8* StateBufferToFree = const_cast<uint8*>(DefaultStateBuffer))
				{
					if (EnumHasAnyFlags(Traits, EReplicationStateTraits::HasDynamicState))
					{
						FNetSerializationContext FreeContext;
						FInternalNetSerializationContext InternalContext;
						FreeContext.SetInternalContext(&InternalContext);

						FReplicationStateOperationsInternal::FreeDynamicState(FreeContext, StateBufferToFree, this);
					}
					FMemory::Free(StateBufferToFree);
				}

				// We must destruct configs pointing to generated structs
				// 析构带 NeedDestruction 的 SerializerConfig。注意只调用析构函数，
				// Config 内存本身在整块 Descriptor 内（紧跟 Descriptor 末尾），与下面的整块 Free 一同释放。
				for (uint32 It = 0; It < MemberCount; ++It)
				{
					const FNetSerializerConfig* Config = MemberSerializerDescriptors[It].SerializerConfig;
					if (EnumHasAnyFlags(Config->ConfigTraits, ENetSerializerConfigTraits::NeedDestruction))
					{
						Config->~FNetSerializerConfig();
					}
				}

				// We must destruct member function descriptors
				// 解除对 FunctionDescriptor 的引用（每个 Function 关联的参数列表 Descriptor）
				for (const FReplicationStateMemberFunctionDescriptor& FunctionDescriptor : MakeArrayView(MemberFunctionDescriptors, FunctionCount))
				{
					FunctionDescriptor.Descriptor->Release();
				}

				// Allocated via FMemory::Malloc in ReplicationStateDescriptorBuilder
				// 整块释放——Builder 是 1 次 Malloc 把 Descriptor + 所有子数组 + SerializerConfig
				// 都打包到一段连续内存里，所以这里 1 次 Free 全部回收。
				FMemory::Free(const_cast<FReplicationStateDescriptor*>(this));
			}
		};
	}
}

}
