// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationProtocolManager.cpp —— 协议工厂/缓存/校验/失效级联 实现
// -----------------------------------------------------------------------------
// 核心流程：
//  1) CalculateProtocolIdentifier
//     依据 Fragments 的 (DescriptorIdentifier.Value, DefaultStateHash) 列表做 CityHash32。
//     —— 这是跨连接/跨端识别"形态"的唯一标识，Server 与 Client 必须计算结果一致。
//  2) CreateInstanceProtocol（每实例）
//     单次连续分配（FReplicationInstanceProtocol + FragmentData[] + Fragments[]）。
//     • 收集 PushModel owner 唯一集（>1 → IsMultiObjectInstance，目前不支持 PushModel）；
//     • 累积 traits（NeedsPoll / NeedsPreSendUpdate / HasObjectReference / HasPushBased*）；
//     • DeleteWithInstanceProtocol 标记的 Fragment 在 DestroyInstanceProtocol 时随实例销毁。
//  3) CreateReplicationProtocol（每形态共享）
//     • 可选校验 ProtocolId（bValidateProtocolId）—— 若失配则警告+ensure；
//     • 必须确保不重复创建（GetReplicationProtocol 返回 nullptr）；
//     • 为每个 PushModel owner 构造 RepIndex→FragmentIndex 表（按 Descriptor 的 RepIndex 反查）；
//     • 单次连续分配整个 Protocol（含 Descriptor 数组 + PushModel 表 + 表 entry 数据）；
//     • 计算 InternalSize/Alignment、ChangeMaskBitCount、合并 traits（Conditional/DynamicState/
//       ConnectionSpecific/HasObjectReference/SupportsDeltaCompression/HasPushBased*）；
//     • HasConditionalChangeMask 时在 InternalBuffer 末尾追加 conditional changemask 区域；
//     • 加入主缓存 (RegisteredProtocols, ProtocolToInfoMap) 与 (DescriptorToProtocolMap)。
//  4) ValidateReplicationProtocol
//     比对 Protocol->ReplicationStateDescriptors 与传入 Fragments 的 Descriptor 指针/Identifier，
//     用于 Server/Client 接到陌生 Protocol 时的 mismatch 检测。
//  5) InvalidateDescriptor / DestroyReplicationProtocol
//     • InvalidateDescriptor —— Descriptor 失效时（如 class GC/Hot-reload）级联销毁所有引用它的 Protocol；
//     • DestroyReplicationProtocol 把 Protocol 移到 PendingDestroyProtocols，等 RefCount=0 真正释放
//       —— 因为可能还有 in-flight RecordInfo/NetObject 持有引用。
// =============================================================================

#include "Iris/ReplicationSystem/ReplicationProtocolManager.h"
#include "CoreTypes.h"
#include "Iris/IrisConfigInternal.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/MemoryLayoutUtil.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Hash/CityHash.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformString.h"
#include "Containers/StringFwd.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "Iris/Stats/NetStats.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

#ifndef UE_NET_ENABLE_PROTOCOLMANAGER_LOG
// Default is to enable protocol logs in non-shipping
// 非 Shipping 默认开启协议日志（创建/销毁/失配等）。
#	define UE_NET_ENABLE_PROTOCOLMANAGER_LOG !(UE_BUILD_SHIPPING)
#endif

#if UE_NET_ENABLE_PROTOCOLMANAGER_LOG
#	define UE_LOG_PROTOCOLMANAGER(Log, Format, ...)  UE_LOG(LogIris, Log, Format, ##__VA_ARGS__)
#else
#	define UE_LOG_PROTOCOLMANAGER(...)
#endif

#define UE_LOG_PROTOCOLMANAGER_WARNING(Format, ...)  UE_LOG(LogIris, Warning, Format, ##__VA_ARGS__)

namespace UE::Net::Private
{

// CVar：开启后，每次创建协议都打印完整 Fragments 列表（hash + 名字），方便调试形态不一致问题。
static bool bIrisLogReplicationProtocols = false;
static FAutoConsoleVariableRef CVarIrisLogReplicationProtocols(
	TEXT("net.Iris.LogReplicationProtocols"),
	bIrisLogReplicationProtocols,
	TEXT("If true, log all created replication protocols."
	));

// 计算 Fragment traits 的"累积值"和"共有值"。
//  - Accumulated：任一 fragment 含某 trait → 含；用于决定"是否需要走某路径"；
//  - Shared：全部 fragment 都含 → 含；用于决定"能否一刀切走优化路径"（如 FullPushBasedDirtiness）。
static void GetInstanceTraits(const FReplicationFragments& Fragments, EReplicationFragmentTraits& OutAccumulatedTraits, EReplicationFragmentTraits& OutSharedTraits, const EReplicationFragmentTraits InObjectTraits)
{
	EReplicationFragmentTraits AccumulatedTraits = InObjectTraits;
	// 初始 SharedTraits = 全 1（无 fragment 时 = None），用 AND 收敛
	EReplicationFragmentTraits SharedTraits = Fragments.Num() > 0 ? ~EReplicationFragmentTraits::None : EReplicationFragmentTraits::None;
	for (const FReplicationFragmentInfo& Info : Fragments)
	{
		const EReplicationFragmentTraits FragmentTraits = Info.Fragment->GetTraits();
		AccumulatedTraits |= FragmentTraits; // 任一含 → 含
		SharedTraits &= FragmentTraits;      // 全部含 → 含
	}

	OutAccumulatedTraits = AccumulatedTraits;
	OutSharedTraits = SharedTraits;
}

// ---------------------------------------------------------------------------
// CreateInstanceProtocol —— 每实例的 Fragment 表；单次连续分配 + 在位构造。
// 内存布局（一次 MallocZeroed）：
//   [ FReplicationInstanceProtocol | FragmentData[] | Fragments[] ]
// ---------------------------------------------------------------------------
FReplicationInstanceProtocol* FReplicationProtocolManager::CreateInstanceProtocol(const FReplicationFragments& Fragments, UE::Net::EReplicationFragmentTraits ObjectTraits)
{
	const uint32 FragmentCount = Fragments.Num();
	if (!ensure(FragmentCount < 65536))
	{
		return nullptr;
	}

	// We want to keep this as a single allocation so we first build a layout and allocate enough space for the InstanceProtocol and its data
	// 构建内存布局：先用 FMemoryLayoutUtil 累计偏移/对齐，最后单次分配。
	struct FReplicationInstanceProtocolLayoutData
	{
		FMemoryLayoutUtil::FOffsetAndSize ReplicationInstanceProtocolSizeAndOffset;
		FMemoryLayoutUtil::FOffsetAndSize FragmentDataSizeAndOffset;
		FMemoryLayoutUtil::FOffsetAndSize FragmentsSizeAndOffset;
	};
	FReplicationInstanceProtocolLayoutData LayoutData;

	FMemoryLayoutUtil::FLayout Layout;
	FMemoryLayoutUtil::AddToLayout<FReplicationInstanceProtocol>(Layout, LayoutData.ReplicationInstanceProtocolSizeAndOffset, 1);
	FMemoryLayoutUtil::AddToLayout<FReplicationInstanceProtocol::FFragmentData>(Layout, LayoutData.FragmentDataSizeAndOffset, FragmentCount);
	FMemoryLayoutUtil::AddToLayout<FReplicationFragment*>(Layout, LayoutData.FragmentsSizeAndOffset, FragmentCount);

	// Allocate memory for the instance protocol
	uint8* Buffer = static_cast<uint8*>(FMemory::MallocZeroed(Layout.CurrentOffset, static_cast<uint32>(Layout.MaxAlignment)));

	// Init FReplicationInstanceProtocol
	// placement new 在分配区头部构造 FReplicationInstanceProtocol，FragmentData/Fragments 指向后续区域
	FReplicationInstanceProtocol* InstanceProtocol = new (Buffer) FReplicationInstanceProtocol;
	InstanceProtocol->FragmentData = reinterpret_cast<FReplicationInstanceProtocol::FFragmentData*>(Buffer + LayoutData.FragmentDataSizeAndOffset.Offset);
	InstanceProtocol->Fragments = reinterpret_cast<FReplicationFragment* const *>(Buffer + LayoutData.FragmentsSizeAndOffset.Offset);
	InstanceProtocol->FragmentCount = static_cast<uint16>(FragmentCount);

	// Setup owner collector
	// PushModel 多 owner 检测：最多收集 2 个不同 Owner，>1 则不再支持 PushModel
	constexpr uint32 MaxFragmentOwnerCount = 2U;
	UObject* FragmentOwnersForPushBasedFragments[MaxFragmentOwnerCount] = {};
	FReplicationStateOwnerCollector UniquePushBasedOwners(FragmentOwnersForPushBasedFragments, MaxFragmentOwnerCount);

	// Fill in fragment data and fragment pointer
	uint32 FragmentIt = 0U;

	for (const FReplicationFragmentInfo& Info : Fragments)
	{
		// 关键：ExternalSrcBuffer 指向 UObject 的属性首址 —— 这是 Iris 与 UE Property 系统的连接点
		InstanceProtocol->FragmentData[FragmentIt].ExternalSrcBuffer = reinterpret_cast<uint8*>(Info.SrcReplicationStateBuffer);
		InstanceProtocol->FragmentData[FragmentIt].Traits = Info.Fragment->GetTraits();
		const_cast<FReplicationFragment**>(InstanceProtocol->Fragments)[FragmentIt] = Info.Fragment;

		// We collect unique owners with properties that are pushbased
		// PushModel 标脏需要按 owner 维度做 RepIndex 反查；此处只统计含成员且启用 PushBased 的 fragment
		if (Info.Descriptor->MemberCount > 0 && EnumHasAnyFlags(Info.Fragment->GetTraits(), EReplicationFragmentTraits::HasPushBasedDirtiness))
		{
			Info.Fragment->CollectOwner(&UniquePushBasedOwners);		
		}
		++FragmentIt;
	}

	// Get accumulated and shared traits
	EReplicationFragmentTraits AccumulatedTraits;
	EReplicationFragmentTraits SharedTraits;
	GetInstanceTraits(Fragments, AccumulatedTraits, SharedTraits, ObjectTraits);

	// Init instance traits
	// 把 fragment-level traits 翻译成 instance-level traits（位含义不同，独立两套）。
	EReplicationInstanceProtocolTraits InstanceTraits = EReplicationInstanceProtocolTraits::None;
	if (EnumHasAnyFlags(AccumulatedTraits, EReplicationFragmentTraits::NeedsPoll))
	{
		InstanceTraits |= EReplicationInstanceProtocolTraits::NeedsPoll;
	}
	if (EnumHasAnyFlags(AccumulatedTraits, EReplicationFragmentTraits::NeedsLegacyCallbacks))
	{
		InstanceTraits |= EReplicationInstanceProtocolTraits::NeedsLegacyCallbacks;
	}
	if (EnumHasAnyFlags(AccumulatedTraits, EReplicationFragmentTraits::NeedsPreSendUpdate))
	{
		InstanceTraits |= EReplicationInstanceProtocolTraits::NeedsPreSendUpdate;
	}
	if (EnumHasAnyFlags(AccumulatedTraits, EReplicationFragmentTraits::HasObjectReference))
	{
		InstanceTraits |= EReplicationInstanceProtocolTraits::HasObjectReference;
	}
	// Currently we do not support push based dirtiness for protocols with multiple pushbased fragments with different owners.
	// 多 owner 实例：暂不支持 PushBased（标脏路径无法在多个 owner 间复用 RepIndex 表）
	if (UniquePushBasedOwners.GetOwnerCount() > 1U)
	{
		InstanceTraits |= EReplicationInstanceProtocolTraits::IsMultiObjectInstance;
		UE_LOG(LogIris, Warning, TEXT("Currently we do not support pushbased dirtiness for multiowner instances"));
	}
	else if (EnumHasAnyFlags(AccumulatedTraits, EReplicationFragmentTraits::HasPushBasedDirtiness))
	{
		InstanceTraits |= EReplicationInstanceProtocolTraits::HasPushBasedDirtiness;
		// FullPushBased：所有 fragment 都共有该 trait → 可彻底跳过 Poll 阶段
		if (EnumHasAnyFlags(SharedTraits, EReplicationFragmentTraits::HasFullPushBasedDirtiness))
		{
			InstanceTraits |= EReplicationInstanceProtocolTraits::HasFullPushBasedDirtiness;
		}
	}

	InstanceProtocol->InstanceTraits = InstanceTraits;

	return InstanceProtocol;
}

// 销毁实例协议：先析构带 DeleteWithInstanceProtocol 的 fragment（fragment 自身可能含动态资源），
// 再 Free 整块单次分配的内存。
void FReplicationProtocolManager::DestroyInstanceProtocol(FReplicationInstanceProtocol* InstanceProtocol)
{
	FReplicationFragment* const * Fragments = InstanceProtocol->Fragments;

	// Destroy owned fragments
	// $IRIS, Cache the flag to avoid touching all Fragments
	const uint32 FragmentCount = InstanceProtocol->FragmentCount;
	for (uint32 StateIt = 0; StateIt < FragmentCount; ++StateIt)
	{
		if (EnumHasAnyFlags(Fragments[StateIt]->GetTraits(), EReplicationFragmentTraits::DeleteWithInstanceProtocol))
		{
			delete Fragments[StateIt];
		}
	}
	FMemory::Free(InstanceProtocol);
}

// ---------------------------------------------------------------------------
// CalculateProtocolIdentifier —— 协议唯一 ID（CityHash32）
// 包含 DescriptorIdentifier.Value（结构形态）+ DefaultStateHash（默认值），
// 默认值变化也算不同形态以避免静默不一致。
// ---------------------------------------------------------------------------
FReplicationProtocolIdentifier FReplicationProtocolManager::CalculateProtocolIdentifier(const FReplicationFragments& Fragments)
{
	TArray<uint64, TInlineAllocator<32>> IdBuffer;
	for (const FReplicationFragmentInfo& Info : Fragments)
	{
		IdBuffer.Add(Info.Descriptor->DescriptorIdentifier.Value);
		// We currently include the default state hash when building the protocol id in order to verify the integrity of default states
		IdBuffer.Add(Info.Descriptor->DescriptorIdentifier.DefaultStateHash);
	}

	FReplicationProtocolIdentifier ProtocolIdentifier;
	ProtocolIdentifier = CityHash32(reinterpret_cast<const char*>(IdBuffer.GetData()), sizeof(uint64) * IdBuffer.Num());

	return ProtocolIdentifier;
}

// ---------------------------------------------------------------------------
// ValidateReplicationProtocol —— Server/Client 形态匹配校验
// 对每个 fragment 同时比对 Descriptor 指针（应来自同一 Registry）和 DescriptorIdentifier
// （内容标识，跨进程也可比对）。任一失配返回 false 并写日志。
// ---------------------------------------------------------------------------
bool FReplicationProtocolManager::ValidateReplicationProtocol(const FReplicationProtocol* Protocol, const FReplicationFragments& Fragments, bool bLogFragmentErrors)
{
	bool bResult = true;

	const uint32 FragmentCount = Fragments.Num();
	if (ensureMsgf(Protocol->ReplicationStateCount == FragmentCount, TEXT("Protocol %s:ReplicationStateCount %u != FragmentCount:%u"), ToCStr(Protocol->DebugName), Protocol->ReplicationStateCount, FragmentCount))
	{
		// Validate individual fragments
		const FReplicationFragmentInfo* FragmentsData = Fragments.GetData();
		for (uint32 It = 0; It < FragmentCount; ++It)
		{
			const FReplicationFragmentInfo& Info = FragmentsData[It];
			const bool bIsSameDescriptor = Info.Descriptor == Protocol->ReplicationStateDescriptors[It];
	
			if (!bIsSameDescriptor)
			{
				UE_LOG_PROTOCOLMANAGER_WARNING(
					TEXT("FReplicationProtocolManager::ValidateReplicationProtocol for %s Descriptor Pointer mismatch %p != %p for index %u/%u named %s != %s identifier 0x%" UINT64_x_FMT " != 0x%" UINT64_x_FMT),
					ToCStr(Protocol->DebugName), Info.Descriptor, Protocol->ReplicationStateDescriptors[It], It, FragmentCount, ToCStr(Info.Descriptor->DebugName), ToCStr(Protocol->ReplicationStateDescriptors[It]->DebugName), 
					Info.Descriptor->DescriptorIdentifier.Value, Protocol->ReplicationStateDescriptors[It]->DescriptorIdentifier.Value);

				ensure(bIsSameDescriptor);
				
				bResult = false;
			}
			else if (Info.Descriptor->DescriptorIdentifier != Protocol->ReplicationStateDescriptors[It]->DescriptorIdentifier)
			{
				
				UE_LOG_PROTOCOLMANAGER_WARNING(
					TEXT("FReplicationProtocolManager::ValidateReplicationProtocol for %s DescriptorIdentfier mismatch for index %u/%u named %s != %s identifier 0x%" UINT64_x_FMT " != 0x%" UINT64_x_FMT),
					ToCStr(Protocol->DebugName), It, FragmentCount, ToCStr(Info.Descriptor->DebugName), ToCStr(Protocol->ReplicationStateDescriptors[It]->DebugName), Info.Descriptor->DescriptorIdentifier.Value, Protocol->ReplicationStateDescriptors[It]->DescriptorIdentifier.Value);

				ensure(Info.Descriptor->DescriptorIdentifier == Protocol->ReplicationStateDescriptors[It]->DescriptorIdentifier);
				
				bResult = false;
			}
		}
	}

	return bResult;
}

// 主缓存查找：(ProtocolId, TemplateKey) → Protocol
// MultiMap 因为同 ProtocolId 可能撞到不同 TemplateKey（不同 UClass 但 fragment 集合 hash 相同）。
const FReplicationProtocol* FReplicationProtocolManager::GetReplicationProtocol(FReplicationProtocolIdentifier ProtocolId, FObjectKey TemplateKey)
{
	for (auto It = RegisteredProtocols.CreateConstKeyIterator(ProtocolId); It; ++It)
	{
		const FRegisteredProtocolInfo& Info = It.Value();
		if (Info.TemplateKey == TemplateKey)
		{
			return Info.Protocol;
		}
	}
	return nullptr;
}

// 调试：把 Fragments 列表序列化成可读字符串（含 hash），用于失配时的诊断输出。
void FReplicationProtocolManager::FragmentListToString(FStringBuilderBase& StringBuilder, const FReplicationFragments& Fragments)
{
	StringBuilder << TEXT("Fragments:\n");
	const FReplicationFragmentInfo* FragmentsData = Fragments.GetData();
	const uint32 FragmentCount = Fragments.Num();
	for (uint32 It = 0; It < FragmentCount; ++It)
	{
		const FReplicationFragmentInfo& Info = FragmentsData[It];
		StringBuilder.Appendf(TEXT("index %u/%u named %s identifier (0x%" UINT64_x_FMT ", 0x%" UINT64_x_FMT ") Pointer: %p\n"),
			It, FragmentCount, ToCStr(Info.Descriptor->DebugName), Info.Descriptor->DescriptorIdentifier.Value, Info.Descriptor->DescriptorIdentifier.DefaultStateHash, Info.Descriptor);
	}
}

// ---------------------------------------------------------------------------
// CreateReplicationProtocol —— 创建/注册一份共享 FReplicationProtocol
// 大流程：
//   1. 校验 ProtocolId（可选）+ TemplateKey 有效性 + 不可重复创建；
//   2. 构造 PushModel owner 的 RepIndex→FragmentIndex 表（按 Descriptor 反查）；
//   3. 用 FMemoryLayoutUtil 单次分配整块（Protocol + Descriptor[] + PushModelTable + TableData）；
//   4. 累计 InternalSize/Alignment/ChangeMaskBitCount + LifetimeConditional 统计；
//   5. 合并 traits（CombinedStateTraits→ProtocolTraits）；
//   6. 必要时为 ConditionalChangeMask 在 InternalSize 末尾追加位图区域；
//   7. 把 Protocol 加入 RegisteredProtocols + ProtocolToInfoMap + DescriptorToProtocolMap；
//   8. 调试日志。
// ---------------------------------------------------------------------------
const FReplicationProtocol* FReplicationProtocolManager::CreateReplicationProtocol(const FReplicationProtocolIdentifier ProtocolId, const FReplicationFragments& Fragments, const TCHAR* DebugName, const FCreateReplicationProtocolParameters& Params)
{
	if (Params.bValidateProtocolId)
	{
		// 校验：传入 ProtocolId 必须等于按 Fragments 重算得到的 hash
		const FReplicationProtocolIdentifier NewProtocolId = CalculateProtocolIdentifier(Fragments);
		if (NewProtocolId != ProtocolId)
		{
			UE_LOG(LogIris, Warning, TEXT("FReplicationProtocolManager::CreateReplicationProtocol Id mismatch when creating protocol named %s with in ProtocolId:0x%x Calculated ProtocolId:0x%x"), DebugName, ProtocolId, NewProtocolId);
 #if UE_NET_ENABLE_PROTOCOLMANAGER_LOG
 			if (UE_LOG_ACTIVE(LogIris, Warning))
 			{
 				TStringBuilder<4096> StringBuilder;
 				FragmentListToString(StringBuilder, Fragments);

 				UE_LOG(LogIris, Warning, TEXT("%s"), StringBuilder.ToString());
 			}
 #endif
			ensureMsgf(NewProtocolId == ProtocolId, TEXT("FReplicationProtocolManager::CreateReplicationProtocol Id mismatch when creating protocol named %s with in ProtocolId:0x%x Calculated ProtocolId:0x%x"), DebugName, ProtocolId, NewProtocolId);
			return nullptr;
		}
	}

	// Template key should be valid unless the protocol doesn't use one.
	// TemplateKey（通常是 UClass*）是缓存复用的第二维度，必须有效（除非协议明确说明无 TemplateKey）。
	if (Params.bHasTemplateKey && !IsValid(Params.TemplateKey))
	{
		UE_LOG(LogIris, Error, TEXT("Cannot create replication protocol %s due to invalid template"), DebugName);
		check(Params.bHasTemplateKey == false || IsValid(Params.TemplateKey));
		return nullptr;
	}

	// Protocol should not exist already
	check(GetReplicationProtocol(ProtocolId, Params.TemplateKey) == nullptr);

	// Create the protocol
	const uint32 FragmentCount = Fragments.Num();
	if (!ensure(FragmentCount <= 65536))
	{
		return nullptr;
	}

	// Build table to map from RepIndex to fragment
	// 第 1 步：为 PushModel 构造每个 owner 的 RepIndex→FragmentIndex 表
	//        当 MARK_PROPERTY_DIRTY 走 RepIndex 标脏时，能 O(1) 反查到对应 Fragment。
	TArray<TArray<FReplicationProtocol::FRepIndexToFragmentIndex, TInlineAllocator<128>>> OwnerRepIndicesToFragment;
	{
		// $TODO: Remove when we change FReplicationFragment::GetOwner()
		auto GetFragmentOwner = [](const FReplicationFragment* Fragment)
		{
			constexpr uint32 MaxFragmentOwnerCount = 2U;
			UObject* FragmentOwnersForPushBasedFragments[MaxFragmentOwnerCount];
			FReplicationStateOwnerCollector OwnerCollector(FragmentOwnersForPushBasedFragments, MaxFragmentOwnerCount);
			Fragment->CollectOwner(&OwnerCollector);

			ensure(OwnerCollector.GetOwnerCount() > 0U);
			return OwnerCollector.GetOwnerCount() > 0U ? OwnerCollector.GetOwners()[0U] : nullptr;
		};
		
		// Fill in fragment data and fragment pointer
		TArray<const UObject*, TInlineAllocator<16>> Owners;

		for (const FReplicationFragmentInfo& Info : Fragments)
		{
			const uint16 FragmentIndex = static_cast<uint16>(&Info - Fragments.GetData());
			if (Info.Descriptor->MemberCount > 0U && EnumHasAnyFlags(Info.Fragment->GetTraits(), EReplicationFragmentTraits::HasPushBasedDirtiness))
			{
				const UObject* Owner = GetFragmentOwner(Info.Fragment);
				const int32 OwnerId = Owners.AddUnique(Owner);

				// Resize if necessary
				OwnerRepIndicesToFragment.SetNum(Owners.Num());
				OwnerRepIndicesToFragment[OwnerId].SetNum(FPlatformMath::Max(Info.Descriptor->RepIndexCount, (uint16)OwnerRepIndicesToFragment[OwnerId].Num()));

				// 遍历 Descriptor 内的 MemberRepIndexToMemberIndexDescriptor 表，
				// 把每个有效 RepIndex 映射到当前 FragmentIndex
				for (const FReplicationStateMemberRepIndexToMemberIndexDescriptor& MemberIndex : MakeArrayView(Info.Descriptor->MemberRepIndexToMemberIndexDescriptors, Info.Descriptor->RepIndexCount))
				{
					const int32 RepIndex = static_cast<int32>(&MemberIndex - Info.Descriptor->MemberRepIndexToMemberIndexDescriptors);
					if (MemberIndex.MemberIndex != FReplicationStateMemberRepIndexToMemberIndexDescriptor::InvalidEntry)
					{
						//UE_LOG(LogIris, VeryVerbose, TEXT("Mapping Property: %s:%s repindex %d memberindex %d to fragment %d"), *GetNameSafe(Owner), Info.Descriptor->MemberDebugDescriptors[MemberIndex.MemberIndex].DebugName->Name, RepIndex, MemberIndex.MemberIndex, FragmentIndex)					
						OwnerRepIndicesToFragment[OwnerId][RepIndex].FragmentIndex = (uint16)FragmentIndex;
					}
				}
			}
		}
	}

	// We want to keep this as a single allocation so we first build a layout and allocate enough space for the InstanceProtocol and its data
	// 第 2 步：构造 Protocol 内存布局：
	//   [ FReplicationProtocol | const FReplicationStateDescriptor*[FragmentCount] |
	//     FRepIndexToFragmentIndexTable[OwnerCount] | FRepIndexToFragmentIndex[Sum-of-RepIndexCount] ]
	struct FReplicationProtocolLayoutData
	{
		FMemoryLayoutUtil::FOffsetAndSize ReplicationProtocolSizeAndOffset;
		FMemoryLayoutUtil::FOffsetAndSize ReplicationStateDescriptorsSizeAndOffset;
		FMemoryLayoutUtil::FOffsetAndSize PushModelOwnerTableSizeAndOffset;
		FMemoryLayoutUtil::FOffsetAndSize PushModelOwnerRepIndexToFragmentIndexDataSizeAndOffset;
	};
	FReplicationProtocolLayoutData LayoutData;

	FMemoryLayoutUtil::FLayout Layout;
	FMemoryLayoutUtil::AddToLayout<FReplicationProtocol>(Layout, LayoutData.ReplicationProtocolSizeAndOffset, 1);
	FMemoryLayoutUtil::AddToLayout<const FReplicationStateDescriptor*>(Layout, LayoutData.ReplicationStateDescriptorsSizeAndOffset, FragmentCount);

	// Fill in RepIndex -> Fragmeent lookup tables
	const uint16 OwnerCount = (uint16)OwnerRepIndicesToFragment.Num();

	uint32 TotalRepIndexToFragmentCount = 0U;
	for (const TArray<FReplicationProtocol::FRepIndexToFragmentIndex, TInlineAllocator<128>>& TableEntry : OwnerRepIndicesToFragment)
	{
		TotalRepIndexToFragmentCount += TableEntry.Num();
	}

	// Add lookup table to layout
	FMemoryLayoutUtil::AddToLayout<const FReplicationProtocol::FRepIndexToFragmentIndexTable>(Layout, LayoutData.PushModelOwnerTableSizeAndOffset, OwnerCount);
	// Actual data for all table entries.
	FMemoryLayoutUtil::AddToLayout<const FReplicationProtocol::FRepIndexToFragmentIndex>(Layout, LayoutData.PushModelOwnerRepIndexToFragmentIndexDataSizeAndOffset, TotalRepIndexToFragmentCount);

	// Allocate memory for the protocol, the replication protocol must be refcounted by all NetObjects
	// We could also choose to explicitly control the lifetime 
	// 单次分配整块（清零）。Protocol 由所有引用它的 NetObject 通过 RefCount 管理生命周期。
	uint8* Buffer = static_cast<uint8*>(FMemory::MallocZeroed(Layout.CurrentOffset, static_cast<uint32>(Layout.MaxAlignment)));

	// Init FReplicationInstanceProtocol
	FReplicationProtocol* Protocol = new (Buffer) FReplicationProtocol;
	Protocol->ReplicationStateDescriptors = FragmentCount ? reinterpret_cast<const FReplicationStateDescriptor**>(Buffer + LayoutData.ReplicationStateDescriptorsSizeAndOffset.Offset) : nullptr;
	Protocol->ReplicationStateCount = FragmentCount;

	// Fill in the data for the the RepIndex -> Fragment lookup tables.
	// 把上面构造的 OwnerRepIndicesToFragment 内容平铺到分配区
	{
		// Setup table pointer and count
		FReplicationProtocol::FRepIndexToFragmentIndexTable* RepIndexToFragmentIndexTables = OwnerCount ? reinterpret_cast<FReplicationProtocol::FRepIndexToFragmentIndexTable*>(Buffer + LayoutData.PushModelOwnerTableSizeAndOffset.Offset) : nullptr;
		Protocol->PushModelOwnerRepIndexToFragmentIndexTable = RepIndexToFragmentIndexTables;
		Protocol->PushModelOwnerCount = OwnerCount;

		// Setup table data.
		FReplicationProtocol::FRepIndexToFragmentIndex* PushModelRepIndexToFragmentIndexData = OwnerCount ? reinterpret_cast<FReplicationProtocol::FRepIndexToFragmentIndex*>(Buffer + LayoutData.PushModelOwnerRepIndexToFragmentIndexDataSizeAndOffset.Offset) : nullptr;
		for (uint32 OwnerIt = 0U; OwnerIt < OwnerCount; ++OwnerIt)
		{
			const uint32 RepIndexToFragmentCount = OwnerRepIndicesToFragment[OwnerIt].Num();

			// Copy data
			FPlatformMemory::Memcpy(PushModelRepIndexToFragmentIndexData, OwnerRepIndicesToFragment[OwnerIt].GetData(), RepIndexToFragmentCount * sizeof(FReplicationProtocol::FRepIndexToFragmentIndex)); 

			// Setup table entry
			RepIndexToFragmentIndexTables[OwnerIt].RepIndexToFragmentIndex = PushModelRepIndexToFragmentIndexData;
			RepIndexToFragmentIndexTables[OwnerIt].NumEntries = OwnerRepIndicesToFragment[OwnerIt].Num();

			PushModelRepIndexToFragmentIndexData += RepIndexToFragmentCount;
		}
	}

	// Cached data
	// 第 3 步：累计 InternalSize/Alignment、ChangeMaskBitCount，并合并 ProtocolTraits
	uint32 MaxExternalSize = 0;
	uint32 MaxExternalAlign = 0;
	uint32 InternalAlign = 0;
	uint32 InternalSize = 0;
	uint32 ChangeMaskBitCount = 0;
	EReplicationProtocolTraits ProtocolTraits = EReplicationProtocolTraits::None;
	EReplicationStateTraits CombinedStateTraits = EReplicationStateTraits::None;

	// Get accumulated and shared traits for fragments
	EReplicationFragmentTraits AccumulatedTraits;
	EReplicationFragmentTraits SharedTraits;
	GetInstanceTraits(Fragments, AccumulatedTraits, SharedTraits, EReplicationFragmentTraits::None);

	// Fill in data for the protocol
	// LifetimeConditional 信息（用于 Conditionals 模块在按连接生成 conditional changemask 时定位起点）
	uint32 FirstLifetimeConditionalsStateIndex = ~0U;
	uint32 LifetimeConditionalsStateCount = 0;
	uint32 FirstLifetimeConditionalsChangeMaskOffset = ~0U;
	uint32 FragmentIt = 0;
	for (const FReplicationFragmentInfo& Info : Fragments)
	{
		CA_ASSUME(Protocol->ReplicationStateDescriptors != nullptr);

		const FReplicationStateDescriptor* Descriptor = Info.Descriptor;
		Protocol->ReplicationStateDescriptors[FragmentIt] = Descriptor;
		// 协议持有 Descriptor 引用计数（Descriptor 也是 RefCount 管理的不可变对象）
		Descriptor->AddRef();

		// We track all protocols using a descriptor in order to be able to detect when we have to invalidate a protocol due to the descriptor being unloaded
		// 反向跟踪：用于 InvalidateDescriptor 级联失效
		DescriptorToProtocolMap.AddUnique(Descriptor, Protocol);

		MaxExternalSize = FMath::Max<uint32>(MaxExternalSize, Descriptor->ExternalSize);
		MaxExternalAlign = FMath::Max<uint32>(MaxExternalAlign, Descriptor->ExternalAlignment);
		InternalAlign = FMath::Max<uint32>(InternalAlign, Descriptor->InternalAlignment);
		InternalSize = Align(InternalSize, Descriptor->InternalAlignment);
		InternalSize += Descriptor->InternalSize;
		ChangeMaskBitCount += Descriptor->ChangeMaskBitCount;

		// Traits
		CombinedStateTraits |= Descriptor->Traits;

		// LifetimeConditional：触发 HasConditionalChangeMask（必须在内部 buffer 中放置 condition 位图）
		if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
		{
			ProtocolTraits |= EReplicationProtocolTraits::HasLifetimeConditionals | EReplicationProtocolTraits::HasConditionalChangeMask;

			++LifetimeConditionalsStateCount;
			if (LifetimeConditionalsStateCount == 1U)
			{
				FirstLifetimeConditionalsStateIndex = FragmentIt;
				// 注意：FirstLifetimeConditionalsChangeMaskOffset 是 bit 偏移
				FirstLifetimeConditionalsChangeMaskOffset = ChangeMaskBitCount - Descriptor->ChangeMaskBitCount;
				checkSlow(FirstLifetimeConditionalsChangeMaskOffset <= 65535);
			}
		}

		++FragmentIt;
	}

	// 把 state-level traits 提升到 protocol-level traits
	if (EnumHasAnyFlags(CombinedStateTraits, EReplicationStateTraits::HasDynamicState))
	{
		ProtocolTraits |= EReplicationProtocolTraits::HasDynamicState;
	}

	if (EnumHasAnyFlags(CombinedStateTraits, EReplicationStateTraits::HasConnectionSpecificSerialization))
	{
		ProtocolTraits |= EReplicationProtocolTraits::HasConnectionSpecificSerialization;
	}

	if (EnumHasAnyFlags(CombinedStateTraits, EReplicationStateTraits::HasObjectReference))
	{
		ProtocolTraits |= EReplicationProtocolTraits::HasObjectReference;
	}

	if (EnumHasAnyFlags(CombinedStateTraits, EReplicationStateTraits::SupportsDeltaCompression))
	{
		// 空协议（无任何成员）不开启 DeltaCompression（没意义）
		if (InternalSize > 0)
		{
			ProtocolTraits |= EReplicationProtocolTraits::SupportsDeltaCompression;
		}
	}

	// Allocate conditional change mask if required.
	// 在 InternalBuffer 末尾追加 conditional changemask 区域：
	//  - 4 字节对齐（FNetBitArrayView 的 word 对齐）；
	//  - InternalChangeMasksOffset 记录字节偏移，运行时按 ChangeMaskBitCount 解读。
	if (EnumHasAnyFlags(ProtocolTraits, EReplicationProtocolTraits::HasConditionalChangeMask))
	{
		InternalAlign = FPlatformMath::Max(InternalAlign, 4U);
		InternalSize = Align(InternalSize, 4U);

		Protocol->InternalChangeMasksOffset = InternalSize;

		Protocol->FirstLifetimeConditionalsStateIndex = static_cast<uint16>(FirstLifetimeConditionalsStateIndex);
		Protocol->LifetimeConditionalsStateCount = static_cast<uint16>(LifetimeConditionalsStateCount);
		Protocol->FirstLifetimeConditionalsChangeMaskOffset = FirstLifetimeConditionalsChangeMaskOffset;

		InternalSize += FNetBitArrayView::CalculateRequiredWordCount(ChangeMaskBitCount)*sizeof(FNetBitArrayView::StorageWordType);
	}

	// Setup Pushbased traits
	// 协议级 PushModel：累积位 = 部分启用；共有位 = 全部启用 → FullPushBased
	if (EnumHasAnyFlags(AccumulatedTraits, EReplicationFragmentTraits::HasPushBasedDirtiness))
	{
		ProtocolTraits |= EReplicationProtocolTraits::HasPushBasedDirtiness;
		if (EnumHasAnyFlags(SharedTraits, EReplicationFragmentTraits::HasPushBasedDirtiness))
		{
			ProtocolTraits |= EReplicationProtocolTraits::HasFullPushBasedDirtiness;	
		}
	}

	Protocol->InternalTotalAlignment = FPlatformMath::Max(InternalAlign, 1U);
	Protocol->InternalTotalSize = InternalSize;
	Protocol->MaxExternalStateSize = MaxExternalSize;
	Protocol->MaxExternalStateAlignment = FPlatformMath::Max(MaxExternalAlign, 1U);
	Protocol->ChangeMaskBitCount = ChangeMaskBitCount;

	Protocol->ProtocolIdentifier = ProtocolId;
	Protocol->ProtocolTraits = ProtocolTraits;
	Protocol->DebugName = CreatePersistentNetDebugName(DebugName);
	Protocol->TypeStatsIndex = Params.TypeStatsIndex >= 0 ? Params.TypeStatsIndex : FNetTypeStats::DefaultTypeStatsIndex;
	Protocol->RefCount = 0;

	// Register protocol
	FRegisteredProtocolInfo Info;
	Info.TemplateKey = Params.TemplateKey;
	Info.Protocol = Protocol;

	RegisteredProtocols.AddUnique(ProtocolId, Info);
	ProtocolToInfoMap.Add(Protocol, Info);

#if UE_NET_ENABLE_PROTOCOLMANAGER_LOG
	if (bIrisLogReplicationProtocols)
	{
		TStringBuilder<4096> StringBuilder;
		FragmentListToString(StringBuilder, Fragments);
		LOG_SCOPE_VERBOSITY_OVERRIDE(LogIris, ELogVerbosity::Log);
		UE_LOG_PROTOCOLMANAGER(Log, TEXT("FReplicationProtocolManager::CreateReplicationProtocol Created new protocol %s with ProtocolId:0x%x"), ToCStr(Protocol->DebugName), ProtocolId);
		UE_LOG_PROTOCOLMANAGER(Log, TEXT("%s"), StringBuilder.ToString());
	}
	else
	{
		UE_LOG_PROTOCOLMANAGER(Verbose, TEXT("FReplicationProtocolManager::CreateReplicationProtocol Created new protocol %s with ProtocolId:0x%x"), ToCStr(Protocol->DebugName), ProtocolId);	
	}
#endif

	return Protocol;
}

// 销毁入口：从主缓存移除并加入待销毁队列；触发一次 Prune（若立即可销毁则当帧释放）。
void FReplicationProtocolManager::DestroyReplicationProtocol(const FReplicationProtocol* ReplicationProtocol)
{
	FRegisteredProtocolInfo Info;
	if (ProtocolToInfoMap.RemoveAndCopyValue(ReplicationProtocol, Info))
	{
		InternalDeferDestroyReplicationProtocol(Info.Protocol);
		RegisteredProtocols.RemoveSingle(ReplicationProtocol->ProtocolIdentifier, Info);		
	}
	
	PruneProtocolsPendingDestroy();
}

// 真正释放：解除对所有 Descriptor 的引用 + 从反查表移除 + Free 整块。
void FReplicationProtocolManager::InternalDestroyReplicationProtocol(const FReplicationProtocol* Protocol)
{
	if (Protocol)
	{
		UE_LOG_PROTOCOLMANAGER(Verbose, TEXT("FReplicationProtocolManager::InternalDestroyReplicationProtocol Destroyed protocol %s with ProtocolId:0x%" UINT64_x_FMT), ToCStr(Protocol->DebugName), Protocol->ProtocolIdentifier);

		// Remove tracked descriptors
		for (const FReplicationStateDescriptor* Descriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
		{
			DescriptorToProtocolMap.RemoveSingle(Descriptor, Protocol);
			Descriptor->Release();
		}
		FMemory::Free(const_cast<FReplicationProtocol*>(Protocol));
	}
}

// 延迟销毁：把 Protocol 加入待销毁队列。等到 RefCount 归零才真正释放。
void FReplicationProtocolManager::InternalDeferDestroyReplicationProtocol(const FReplicationProtocol* Protocol)
{
	PendingDestroyProtocols.Add(Protocol);
}

// 扫描待销毁队列，对 RefCount=0 的执行真正释放。
// 此函数在每次 DestroyReplicationProtocol 后调用，因此分摊到每次销毁操作。
void FReplicationProtocolManager::PruneProtocolsPendingDestroy()
{
	for (auto It = PendingDestroyProtocols.CreateIterator(); It; ++It)
	{
		const FReplicationProtocol* Protocol = *It;
		if (Protocol->RefCount == 0)
		{
			InternalDestroyReplicationProtocol(Protocol);
			It.RemoveCurrent();
		}
	}
}

// Descriptor 失效（class 卸载/Hot-reload）→ 找出引用它的所有 Protocol 并级联销毁。
// 这是保证"形态"一致性的兜底机制。
void FReplicationProtocolManager::InvalidateDescriptor(const FReplicationStateDescriptor* InvalidatedReplicationStateDescriptor)
{
	// Destroy all protocols that referenced the descriptor (or ensure that they are still valid)
	TArray<const FReplicationProtocol*, TInlineAllocator<32>> InvalidProtocols;

	// Find protocols using the descriptor being invalidated
	for (auto It = DescriptorToProtocolMap.CreateConstKeyIterator(InvalidatedReplicationStateDescriptor); It; ++It)
	{
		InvalidProtocols.Add(It.Value());
	}

	// Destroy them
	for (const FReplicationProtocol* Protocol : MakeArrayView(InvalidProtocols))
	{
		DestroyReplicationProtocol(Protocol);
	}
}

// 析构：清空主缓存与待销毁队列，确保所有 Protocol 释放（不管 RefCount —— 因为 Manager 也走了）。
FReplicationProtocolManager::~FReplicationProtocolManager()
{
	// Cleanup protocols
	for (auto& It : ProtocolToInfoMap)
	{
		InternalDestroyReplicationProtocol(It.Value.Protocol);
	}	

	for (const FReplicationProtocol* Protocol : PendingDestroyProtocols)
	{
		InternalDestroyReplicationProtocol(Protocol);
	}
}

}
