// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// MassEntityTrace.cpp
// Mass 追踪事件的定义与实现。整体仅在 UE_MASS_TRACE_ENABLED 为真时编译；Shipping / Program 下整文件空编。
// 
// 事件定义采用 UE Trace 的 DSL（UE_TRACE_EVENT_BEGIN / _FIELD / _END），每个事件都有一串字段：
// Cycle（时间戳）、ID（实体/查询/archetype 的句柄）以及可变长度的 uint64[] 列表（例如 fragments 数组）。
// =====================================================================================================================

#include "MassEntityTrace.h"

#if UE_MASS_TRACE_ENABLED

#include "MassArchetypeData.h"
#include "MassEntityTypes.h"
#include "MassEntityQuery.h"
#include "MassEntityUtils.h"
#include "MassProcessor.h"
#include "MassDebugger.h"

#include "Trace/Trace.inl"
#include "TraceFilter.h"

// 定义 MassChannel（对应头文件里的 UE_TRACE_CHANNEL_EXTERN 声明）。
// 运行期可通过命令 `trace.enable MassChannel` 开启，默认未开启时所有事件短路。
UE_TRACE_CHANNEL_DEFINE(MassChannel);

// ---------------------------------------------------------------------------------------------------------------------
// 事件字段声明区：定义每个事件的结构。
// UE_TRACE_EVENT_BEGIN(Logger, EventName) ... UE_TRACE_EVENT_END() 生成一个 Logger::EventName 的事件类型。
// ---------------------------------------------------------------------------------------------------------------------

// Phase 区间事件：开始/结束成对，共享 Cycle+PhaseName+PhaseId 字段。
UE_TRACE_EVENT_BEGIN(MassTrace, MassPhaseBegin)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, PhaseName)
	UE_TRACE_EVENT_FIELD(uint64, PhaseId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, MassPhaseEnd)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, PhaseName)
	UE_TRACE_EVENT_FIELD(uint64, PhaseId)
UE_TRACE_EVENT_END()

// Archetype 注册：把组合信息一次性发出（fragment/tag 类型 ID 列表）。
UE_TRACE_EVENT_BEGIN(MassTrace, RegisterMassArchetype)
	UE_TRACE_EVENT_FIELD(uint64, ArchetypeID)
	UE_TRACE_EVENT_FIELD(uint64[], Fragments)          // 可变长度数组：Fragment + Tag 的 UScriptStruct 指针
UE_TRACE_EVENT_END()

// Fragment/Tag 类型注册：字符串名 + 大小 + 分类枚举（FFragmentType）。
UE_TRACE_EVENT_BEGIN(MassTrace, RegisterMassFragment)
	UE_TRACE_EVENT_FIELD(uint64, FragmentId)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, FragmentName)
	UE_TRACE_EVENT_FIELD(uint32, FragmentSize)
	UE_TRACE_EVENT_FIELD(uint8, FragmentType)          // 对应 FFragmentType 枚举
UE_TRACE_EVENT_END()

// Phase 执行时段事件（声明已定义但本文件中未见 emit；可能保留供将来使用或由其他 TU 发）。
UE_TRACE_EVENT_BEGIN(MassTrace, MassPhaseExecutionBegin)
	UE_TRACE_EVENT_FIELD(uint64, PhaseId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, MassPhaseExecutionEnd)
	UE_TRACE_EVENT_FIELD(uint64, PhaseId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

// Chunk 执行事件：一次 chunk 的处理开始/结束，含实体数量和所属 Query。
UE_TRACE_EVENT_BEGIN(MassTrace, MassExecuteChunk)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ChunkId)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
	UE_TRACE_EVENT_FIELD(int32, EntityCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, MassExecuteChunkEnd)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ChunkId)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
UE_TRACE_EVENT_END()

// 批量实体生/灭事件：Entities + ArchetypeIDs 两个可变长度数组等长对应。
UE_TRACE_EVENT_BEGIN(MassTrace, MassBulkAddEntity)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64[], Entities)
	UE_TRACE_EVENT_FIELD(uint64[], ArchetypeIDs)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, MassBulkEntityDestroyed)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64[], Entities)
UE_TRACE_EVENT_END()

// 单实体跨 archetype 迁移：记录目标 archetype ID。
UE_TRACE_EVENT_BEGIN(MassTrace, MassEntityMoved)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, Entity)
	UE_TRACE_EVENT_FIELD(uint64, NewArchetypeID)
UE_TRACE_EVENT_END()

// Query 对象生灭与关系事件。
UE_TRACE_EVENT_BEGIN(MassTrace, QueryCreated)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, QueryDestroyed)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, QueryRegisteredToProcessor)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
	UE_TRACE_EVENT_FIELD(uint64, ProcessorID)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, ProcessorName)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, QueryArchetypeAdded)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
	UE_TRACE_EVENT_FIELD(uint64, ArchetypeID)
UE_TRACE_EVENT_END()

// Query ForEach 区间事件（与 FScopedQueryForEachTrace RAII 对应）。
UE_TRACE_EVENT_BEGIN(MassTrace, QueryForEachStarted)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(MassTrace, QueryForEachComplete)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, QueryID)
	UE_TRACE_EVENT_FIELD(int32, ArchetypeCount)
	UE_TRACE_EVENT_FIELD(int32, ChunkCount)
	UE_TRACE_EVENT_FIELD(int32, EntityCount)
UE_TRACE_EVENT_END()

// ---------------------------------------------------------------------------------------------------------------------
// 内部辅助：Fragment 分类枚举。随 RegisterMassFragment 事件发出，用于 Insights 在 UI 上区分 Fragment / Tag / Shared。
// 注：ChunkFragment 没有独立分类；ConstSharedFragment 也归入 SharedFragment。
// 这是一个可能需要在未来扩展的设计点（见总结的"疑似文档/设计遗漏"）。
// ---------------------------------------------------------------------------------------------------------------------
enum class FFragmentType : uint8
{
	Unknown = 0,
	Fragment,
	Tag,
	SharedFragment
};

// ---------------------------------------------------------------------------------------------------------------------
// Query 相关事件的发射
// ---------------------------------------------------------------------------------------------------------------------

// 发 QueryCreated：用 Query 指针地址当 ID。QueryID 在整个会话内稳定且唯一（Query 未销毁时）。
void FMassTrace::QueryCreated(const FMassEntityQuery* Query)
{
	UE_TRACE_LOG(MassTrace, QueryCreated, MassChannel)
		<< QueryCreated.Cycle(FPlatformTime::Cycles64())
		<< QueryCreated.QueryID(reinterpret_cast<uint64>(Query));
}

// 发 QueryDestroyed。注：若 Query 地址被复用（后续新 Query 分到同地址），Insights 会看到 ID 复用；
// 实际冲突概率在单次 session 内极低，可以接受。
void FMassTrace::QueryDestroyed(const FMassEntityQuery* Query)
{
	UE_TRACE_LOG(MassTrace, QueryDestroyed, MassChannel)
		<< QueryDestroyed.Cycle(FPlatformTime::Cycles64())
		<< QueryDestroyed.QueryID(reinterpret_cast<uint64>(Query));
}

// 建立 Query ↔ Processor 关系。TNotNull 保证 Processor 非空，无需额外 null check。
void FMassTrace::QueryRegisteredToProcessor(const FMassEntityQuery* Query, TNotNull<const UMassProcessor*> Processor)
{
	UE_TRACE_LOG(MassTrace, QueryRegisteredToProcessor, MassChannel)
		<< QueryRegisteredToProcessor.QueryID(reinterpret_cast<uint64>(Query))
		<< QueryRegisteredToProcessor.ProcessorID(reinterpret_cast<uint64>((const UMassProcessor*)(Processor)))
		<< QueryRegisteredToProcessor.ProcessorName(*Processor->GetProcessorName());
}

// Query 匹配到新 archetype 时调用。提前用 UE_TRACE_CHANNELEXPR_IS_ENABLED 检查 channel 是否启用，
// 避免在 Trace 未开启时做 reinterpret_cast 和后续无效负载工作。
void FMassTrace::QueryArchetypeAdded(const FMassEntityQuery* Query, const FMassArchetypeHandle& Archetype)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		uint64 ArchetypeID = reinterpret_cast<uint64>(FMassArchetypeHelper::ArchetypeDataFromHandle(Archetype));

		UE_TRACE_LOG(MassTrace, QueryArchetypeAdded, MassChannel)
			<< QueryArchetypeAdded.QueryID(reinterpret_cast<uint64>(Query))
			<< QueryArchetypeAdded.ArchetypeID(ArchetypeID);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FScopedQueryForEachTrace：ForEachEntityChunk 的 RAII 作用域追踪。
// 构造 → 发 "started"；析构 → 发 "complete"（带累积统计数）。
// ---------------------------------------------------------------------------------------------------------------------
FMassTrace::FScopedQueryForEachTrace::FScopedQueryForEachTrace(const FMassEntityQuery* InQuery)
	: Query(InQuery)
	, ArchetypeCount(0)
	, ChunkCount(0)
	, EntityCount(0)
{
	// 注意：此处未先检查 channel 是否启用就直接 UE_TRACE_LOG。UE_TRACE_LOG 宏内部会检查 channel 状态，
	// 关闭时仅产生极小开销，但构造此 RAII 本身的"对象构造开销"是固定的。
	UE_TRACE_LOG(MassTrace, QueryForEachStarted, MassChannel)
		<< QueryForEachStarted.Cycle(FPlatformTime::Cycles64())
		<< QueryForEachStarted.QueryID(reinterpret_cast<uint64>(Query));
}

FMassTrace::FScopedQueryForEachTrace::~FScopedQueryForEachTrace()
{
	UE_TRACE_LOG(MassTrace, QueryForEachComplete, MassChannel)
		<< QueryForEachComplete.Cycle(FPlatformTime::Cycles64())
		<< QueryForEachComplete.QueryID(reinterpret_cast<uint64>(Query))
		<< QueryForEachComplete.ArchetypeCount(ArchetypeCount)
		<< QueryForEachComplete.ChunkCount(ChunkCount)
		<< QueryForEachComplete.EntityCount(EntityCount);
}

// 计数累加：遍历每 archetype 时调用一次；仅在 channel 启用时访问 Archetype 数据（避免 GetChunkCount 等 overhead）。
void FMassTrace::FScopedQueryForEachTrace::ReportArchetype(const FMassArchetypeData& Archetype)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		++ArchetypeCount;
		ChunkCount += Archetype.GetChunkCount();
		EntityCount += Archetype.GetNumEntities();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Fragment 注册：把 UScriptStruct 发到 Insights。
// ---------------------------------------------------------------------------------------------------------------------
// 输出版本：不做"是否 channel 启用"检查（外层调用点已检查）。
// 分类逻辑：根据 Struct 的继承关系映射到 FFragmentType；注意 ChunkFragment 未独立分类，
// 也没有 ConstSharedFragment 的独立分支。
void FMassTrace::OutputRegisterFragment(const UScriptStruct* Struct)
{
	const uint64 FragmentId = reinterpret_cast<uint64>(Struct);
	// 立即执行的 lambda（IIFE）：可避免"中途写成复杂 if-else 链"的命令式代码。
	const FFragmentType FragmentType = [](const UScriptStruct* Struct)
		{
			if (Struct->IsChildOf<FMassFragment>())
			{
				return FFragmentType::Fragment;
			}
			else if (Struct->IsChildOf<FMassTag>())
			{
				return FFragmentType::Tag;
			}
			else if (Struct->IsChildOf<FMassSharedFragment>())
			{
				return FFragmentType::SharedFragment;
			}
			else
			{
				// 注意：ChunkFragment / ConstSharedFragment 不归此分支 → 被标为 Unknown。
				return FFragmentType::Unknown;
			}
		}(Struct);

	UE_TRACE_LOG(MassTrace, RegisterMassFragment, MassChannel)
		<< RegisterMassFragment.FragmentId(FragmentId)
		<< RegisterMassFragment.FragmentName(*Struct->GetName())
		<< RegisterMassFragment.FragmentSize(Struct->GetStructureSize())
		<< RegisterMassFragment.FragmentType(static_cast<uint8>(FragmentType));
}

// ---------------------------------------------------------------------------------------------------------------------
// Phase 区间事件：利用 EMassProcessingPhase 的名字反解，调用 OutputBegin/EndPhaseRegion。
// ---------------------------------------------------------------------------------------------------------------------
void FMassTrace::OnPhaseBegin(uint64 PhaseId)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		// PhaseId 本质是 EMassProcessingPhase 的整数值；用 StaticEnum 反解得到可读字符串。
		const FString EnumName = StaticEnum<EMassProcessingPhase>()->GetNameStringByValue(PhaseId);
		OutputBeginPhaseRegion(*EnumName);
	}
}

void FMassTrace::OnPhaseEnd(uint64 PhaseId)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const FString EnumName = StaticEnum<EMassProcessingPhase>()->GetNameStringByValue(PhaseId);
		OutputEndPhaseRegion(*EnumName);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Archetype 注册。核心逻辑：把 CompositionDescriptor 里的 Fragments + Tags 两个位集展开为 UScriptStruct* 数组，
// 同时为每个结构体发一次 RegisterMassFragment，再整体发一次 RegisterMassArchetype。
// 注意：本函数仅导出 Fragments 和 Tags；ChunkFragment / SharedFragment / ConstSharedFragment 未被记录。
// ---------------------------------------------------------------------------------------------------------------------
uint64 FMassTrace::OutputRegisterArchetype(uint64 ArchetypeID, const FMassArchetypeCompositionDescriptor& CompositionDescriptor)
{
	// 预留容量：fragment + tag 的类型数上限。避免多次扩容。
	TArray<uint64> FragmentsScratch;
	FragmentsScratch.Reserve(CompositionDescriptor.GetFragments().CountStoredTypes() + CompositionDescriptor.GetTags().CountStoredTypes());

	// 遍历 Fragments 位集：用索引迭代器取 UScriptStruct*，每个都发 RegisterMassFragment，并追加 ID 到 Scratch。
	auto FragmentIterator = CompositionDescriptor.GetFragments().GetIndexIterator();
	while (FragmentIterator)
	{
		const UScriptStruct* FragmentStruct = CompositionDescriptor.GetFragments().GetTypeAtIndex(*FragmentIterator);

		OutputRegisterFragment(FragmentStruct);

		// TODO Should have utility function to go from UScriptStruct to the fragment ID
		// （Epic 自留的 TODO：以 UScriptStruct* 的原始指针作为 FragmentId 太"地址化"，应提供统一 ID 工具。）
		FragmentsScratch.Add(reinterpret_cast<uint64>(FragmentStruct));
		++FragmentIterator;
	}

	// 同样处理 Tags。
	auto TagIterator = CompositionDescriptor.GetTags().GetIndexIterator();
	while (TagIterator)
	{
		const UScriptStruct* FragmentStruct = CompositionDescriptor.GetTags().GetTypeAtIndex(*TagIterator);
		{
			OutputRegisterFragment(FragmentStruct);
		}
		// TODO Should have utility function to go from UScriptStruct to the fragment ID
		FragmentsScratch.Add(reinterpret_cast<uint64>(FragmentStruct));
		++TagIterator;
	}

	// 最终把所有 fragment+tag 的 ID 列表打包到一条 RegisterMassArchetype 事件。
	UE_TRACE_LOG(MassTrace, RegisterMassArchetype, MassChannel)
		<< RegisterMassArchetype.ArchetypeID(ArchetypeID)
		<< RegisterMassArchetype.Fragments(FragmentsScratch.GetData(), FragmentsScratch.Num());

	return ArchetypeID;
}

// RegisterArchetype 两种入口：
//   - FMassArchetypeHandle 版：通过 Debugger 公共 API 取 descriptor 和 trace id
//   - FMassArchetypeData 版：直接取数据
// 两者都先检查 channel 是否开启，未开启则跳过整个注册过程（返回 0）。
uint64 FMassTrace::RegisterArchetype(const FMassArchetypeHandle& ArchetypeHandle)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const FMassArchetypeCompositionDescriptor& CompositionDescriptor = FMassDebugger::GetArchetypeComposition(ArchetypeHandle);
		const uint64 ArchetypeID = FMassDebugger::GetArchetypeTraceID(ArchetypeHandle);

		return OutputRegisterArchetype(ArchetypeID, CompositionDescriptor);
	}
	return 0;
}

uint64 FMassTrace::RegisterArchetype(const FMassArchetypeData& Data)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const FMassArchetypeCompositionDescriptor& CompositionDescriptor = Data.GetCompositionDescriptor();
		const uint64 ArchetypeID = FMassDebugger::GetArchetypeTraceID(Data);

		return OutputRegisterArchetype(ArchetypeID, CompositionDescriptor);
	}
	return 0;
}

// 单类型注册入口：对外暴露的版本带 channel 检查。
void FMassTrace::RegisterFragment(const UScriptStruct* Struct)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		OutputRegisterFragment(Struct);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Entity 生命周期：创建/迁移/销毁。
// ---------------------------------------------------------------------------------------------------------------------
// 单实体创建：内部复用 MassBulkAddEntity 事件（Entities/ArchetypeIDs 数组长度=1）。
// 这样 Insights 端只需识别一种事件即可处理"批量"与"单个"两种情形。
void FMassTrace::EntityCreated(FMassEntityHandle Entity, const FMassArchetypeData& Archetype)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const uint64 Cycle = FPlatformTime::Cycles64();
		const uint64 EntityAsU64 = Entity.AsNumber();               // 将 (Index, SerialNumber) 打包成 uint64
		const uint64 ArchetypeID = FMassDebugger::GetArchetypeTraceID(Archetype);
		UE_TRACE_LOG(MassTrace, MassBulkAddEntity, MassChannel)
			<< MassBulkAddEntity.Cycle(Cycle)
			<< MassBulkAddEntity.Entities(&EntityAsU64, 1)
			<< MassBulkAddEntity.ArchetypeIDs(&ArchetypeID, 1);
	}
}

// 实体跨 archetype 迁移：由于 Mass 以 Descriptor 为 archetype 身份，任何 Add/Remove 组合操作都等同"迁移"。
void FMassTrace::EntityMoved(FMassEntityHandle Entity, const FMassArchetypeData& NewArchetype)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const uint64 Cycle = FPlatformTime::Cycles64();
		const uint64 EntityAsU64 = Entity.AsNumber();
		UE_TRACE_LOG(MassTrace, MassEntityMoved, MassChannel)
			<< MassEntityMoved.Cycle(Cycle)
			<< MassEntityMoved.Entity(EntityAsU64)
			<< MassEntityMoved.NewArchetypeID(FMassDebugger::GetArchetypeTraceID(NewArchetype));
	}
}

// 单实体销毁：同样借用 MassBulkEntityDestroyed 的单元素模式。
void FMassTrace::EntityDestroyed(FMassEntityHandle Entity)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const uint64 Cycle = FPlatformTime::Cycles64();
		const uint64 EntityAsU64 = Entity.AsNumber();
		UE_TRACE_LOG(MassTrace, MassBulkEntityDestroyed, MassChannel)
			<< MassBulkEntityDestroyed.Cycle(Cycle)
			<< MassBulkEntityDestroyed.Entities(&EntityAsU64, 1);
	}
}

// 批量销毁：把 TConstArrayView<FMassEntityHandle> 直接按 uint64 数组发出。
// 关键假设：FMassEntityHandle 在内存中布局等价于 uint64（Index + SerialNumber 各 int32 相邻）。
// 这依赖 M0 层 FMassEntityHandle 的实现细节——若那边修改了布局（例如增加字段），此处会发送错误数据。
void FMassTrace::EntitiesDestroyed(TConstArrayView<FMassEntityHandle> Entities)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const uint64 Cycle = FPlatformTime::Cycles64();

		// reinterpret_cast 把 FMassEntityHandle* 视作 uint64*；前提是两者同尺寸与对齐。
		TConstArrayView<uint64> EntitiesAsU64(
			reinterpret_cast<const uint64*>(Entities.GetData()),
			Entities.Num());
		UE_TRACE_LOG(MassTrace, MassBulkEntityDestroyed, MassChannel)
			<< MassBulkEntityDestroyed.Cycle(Cycle)
			<< MassBulkEntityDestroyed.Entities(EntitiesAsU64.GetData(), EntitiesAsU64.Num());
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Phase 区间底层 emit：带 ID / 按名 / 按 ID 收尾三种变体。
// ---------------------------------------------------------------------------------------------------------------------
// 带 ID 的 Begin：PhaseId 用 Cycles64 时间戳充当"唯一标识"，同时返回给调用方留作配对的 EndPhaseRegion(PhaseId) 使用。
uint64_t FMassTrace::OutputBeginPhaseWithID(const TCHAR* PhaseName)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(MassChannel))
	{
		const uint64 PhaseId = FPlatformTime::Cycles64();
		UE_TRACE_LOG(MassTrace, MassPhaseBegin, MassChannel)
			<< MassPhaseBegin.Cycle(FPlatformTime::Cycles64())  // 注意：这里再次调用 Cycles64，与 PhaseId 可能略有偏差
			<< MassPhaseBegin.PhaseName(PhaseName)
			<< MassPhaseBegin.PhaseId(PhaseId);
		return PhaseId;
	}
	return 0;
}

// 按名字 Begin：PhaseId 填 0 —— 表示"按名字配对"的区间。
void FMassTrace::OutputBeginPhaseRegion(const TCHAR* PhaseName)
{
	UE_TRACE_LOG(MassTrace, MassPhaseBegin, MassChannel)
		<< MassPhaseBegin.Cycle(FPlatformTime::Cycles64())
		<< MassPhaseBegin.PhaseName(PhaseName)
		<< MassPhaseBegin.PhaseId(0);
}

void FMassTrace::OutputEndPhaseRegion(const TCHAR* PhaseName)
{
	UE_TRACE_LOG(MassTrace, MassPhaseEnd, MassChannel)
		<< MassPhaseEnd.Cycle(FPlatformTime::Cycles64())
		<< MassPhaseEnd.PhaseName(PhaseName)
		<< MassPhaseEnd.PhaseId(0);
}

// 按 ID 收尾：与 BeginPhaseWithID 配对。注意：这里没有设置 PhaseName（Insights 需靠 PhaseId 反查）。
void FMassTrace::OutputEndPhaseRegion(uint64 PhaseId)
{
	UE_TRACE_LOG(MassTrace, MassPhaseEnd, MassChannel)
		<< MassPhaseEnd.Cycle(FPlatformTime::Cycles64())
		<< MassPhaseEnd.PhaseId(PhaseId);
}

#endif //UE_MASS_TRACE_ENABLED