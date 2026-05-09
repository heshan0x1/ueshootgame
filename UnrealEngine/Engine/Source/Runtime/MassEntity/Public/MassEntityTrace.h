// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =====================================================================================================================
// 文件角色：Mass 在 UE Insights / UE Trace 中的"追踪事件"定义。
// 作用是把 Mass 运行期的关键事件（phase 区间、archetype / fragment 注册、entity 生死、query 执行）
// 以结构化 trace event 形式输出，在 Insights 里形成 Mass 可视化视图。
// 
// 该模块完全可裁剪：UE_MASS_TRACE_ENABLED 为 0 时，所有 UE_TRACE_MASS_* 宏都为空宏，
// 调用处 0 开销；Shipping / Program 构建下默认关闭。
// =====================================================================================================================

#include "MassProcessingTypes.h"
#include "Misc/NotNull.h"
#include "Trace/Config.h"
#include "Trace/Trace.h"

#ifndef UE_MASS_TRACE_ENABLED
// 默认启用条件：
//   - UE_TRACE_ENABLED：全局 Trace 系统开启
//   - WITH_MASSENTITY_DEBUG：Mass 调试功能开启
//   - !IS_PROGRAM：不是独立 Program 构建（如工具类二进制）
//   - !UE_BUILD_SHIPPING：非 Shipping 构建
// 外部可通过 Target.cs 显式定义此宏覆盖默认值。
#define UE_MASS_TRACE_ENABLED (UE_TRACE_ENABLED && WITH_MASSENTITY_DEBUG && !IS_PROGRAM && !UE_BUILD_SHIPPING)
#endif

#if UE_MASS_TRACE_ENABLED

// 前向声明：减少编译依赖。
struct FMassEntityHandle;
struct FMassEntityManager;
struct FMassEntityQuery;
struct FMassArchetypeData;
struct FMassArchetypeHandle;
struct FMassArchetypeCompositionDescriptor;
class UMassProcessor;	

// MassChannel：Trace 事件输出的逻辑通道。运行期可通过 `trace.enable MassChannel` 动态开/关。
// UE_TRACE_CHANNEL_EXTERN 仅声明；对应 UE_TRACE_CHANNEL_DEFINE 在 MassEntityTrace.cpp。
UE_TRACE_CHANNEL_EXTERN(MassChannel, MASSENTITY_API);

// FMassTrace：所有 Mass 追踪接口的命名空间式宿主类。FNoncopyable 防止被拷贝——它无状态，只提供静态函数。
class FMassTrace : public FNoncopyable
{
public:
	// FScopedQueryForEachTrace：RAII 区间追踪器。
	// 用法：进入 Query::ForEachEntityChunk 前构造，离开作用域自动析构 → 自动发出 QueryForEachStarted / QueryForEachComplete。
	// 运行期通过 ReportArchetype() 累加本次遍历跨越了多少 archetype/chunk/entity。
	struct FScopedQueryForEachTrace
	{
		// 构造：记录当前时间戳并输出"开始"事件。
		FScopedQueryForEachTrace(const FMassEntityQuery* InQuery);
		// 析构：输出"结束"事件，带上累计的 archetype/chunk/entity 数量。
		~FScopedQueryForEachTrace();

		// 每遇到一个 archetype 调用一次：累加 chunk/entity 计数（单次遍历可能跨多个 archetype）。
		void ReportArchetype(const FMassArchetypeData& Archetype);
		const FMassEntityQuery* Query = nullptr;   // 发起遍历的 Query（用作 Trace 事件的逻辑 ID）
		int32 ArchetypeCount = 0;                   // 本次遍历涉及多少 archetype
		int32 ChunkCount = 0;                       // chunk 总数
		int32 EntityCount = 0;                      // 遍历到的 entity 总数
	};

	// Phase 区间追踪：带 PhaseId 的"开始"，返回 PhaseId（= Cycles64 时间戳，确保唯一）。
	MASSENTITY_API static uint64_t OutputBeginPhaseWithID(const TCHAR* PhaseName);
	// 不带 ID 的"开始/结束"——靠 PhaseName 配对。
	MASSENTITY_API static void OutputBeginPhaseRegion(const TCHAR* PhaseName);
	MASSENTITY_API static void OutputEndPhaseRegion(const TCHAR* PhaseName);
	// 按 PhaseId 收尾。注意这里无 PhaseName 字段（与 Begin 不对称）。
	MASSENTITY_API static void OutputEndPhaseRegion(uint64 PhaseId);

	// EMassProcessingPhase 值 ↔ Phase 区间自动映射：OnPhaseBegin/End 根据枚举名反解字符串后输出。
	MASSENTITY_API static void OnPhaseBegin(uint64 PhaseId);
	MASSENTITY_API static void OnPhaseEnd(uint64 PhaseId);
	
	// Archetype 注册：把 CompositionDescriptor 拆成 Fragment/Tag 的 UScriptStruct* 列表送到 Insights，
	// 同时把每个 fragment 自身也注册为 Trace 资源（通过 OutputRegisterFragment）。
	MASSENTITY_API static uint64 OutputRegisterArchetype(uint64 ArchetypeID, const FMassArchetypeCompositionDescriptor& CompositionDescriptor);
	MASSENTITY_API static uint64 RegisterArchetype(const FMassArchetypeHandle& ArchetypeHandle);
	MASSENTITY_API static uint64 RegisterArchetype(const FMassArchetypeData& Data);

	// 单个 Fragment/Tag 类型注册：把 UScriptStruct* 的名字/大小/类型分类送到 Insights，一次性即可。
	MASSENTITY_API static void OutputRegisterFragment(const UScriptStruct* Struct);
	MASSENTITY_API static void RegisterFragment(const UScriptStruct* Struct);
	
	// Entity 生命周期事件：
	// - EntityCreated：单实体创建。内部作为 MassBulkAddEntity 单元素事件发出。
	// - EntityMoved：实体在 archetype 间迁移（组合变更必然触发）。
	// - EntityDestroyed：单实体销毁，作为 MassBulkEntityDestroyed 单元素事件。
	// - EntitiesDestroyed：批量销毁，合并为一条 MassBulkEntityDestroyed 事件，节省带宽。
	MASSENTITY_API static void EntityCreated(FMassEntityHandle Entity, const FMassArchetypeData& Archetype);
	MASSENTITY_API static void EntityMoved(FMassEntityHandle Entity, const FMassArchetypeData& NewArchetype);
	MASSENTITY_API static void EntityDestroyed(FMassEntityHandle Entity);
	MASSENTITY_API static void EntitiesDestroyed(TConstArrayView<FMassEntityHandle> Entities);

	// Query 生命周期与关系事件：
	// - QueryCreated/Destroyed：Query 对象本身的生灭
	// - QueryRegisteredToProcessor：把 Query 绑定到某个 Processor 时发一条，便于 Insights 展示 Query↔Processor 关系
	// - QueryArchetypeAdded：Query 在匹配到新 archetype 时触发
	MASSENTITY_API static void QueryCreated(const FMassEntityQuery* Query);
	MASSENTITY_API static void QueryDestroyed(const FMassEntityQuery* Query);
	MASSENTITY_API static void QueryRegisteredToProcessor(const FMassEntityQuery* Query, TNotNull<const UMassProcessor*> Processor);
	MASSENTITY_API static void QueryArchetypeAdded(const FMassEntityQuery* Query, const FMassArchetypeHandle& Archetype);
};

// ---------------------------------------------------------------------------------------------------------------------
// 对外暴露的埋点宏：调用方写 UE_TRACE_MASS_xxx(...)，UE_MASS_TRACE_ENABLED==0 时会被编译为空宏（见下方 #else 分支）。
// ---------------------------------------------------------------------------------------------------------------------

// Phase 开始/结束：PhaseId 通常就是 EMassProcessingPhase 的枚举值。
#define UE_TRACE_MASS_PHASE_BEGIN(PhaseID) \
	FMassTrace::OnPhaseBegin(PhaseID);

#define UE_TRACE_MASS_PHASE_END(PhaseID) \
	FMassTrace::OnPhaseEnd(PhaseID);

// Archetype 创建后立刻向 Trace 注册其组合信息。
#define UE_TRACE_MASS_ARCHETYPE_CREATED(Archetype) \
	FMassTrace::RegisterArchetype(Archetype);

// 单实体创建事件。
#define UE_TRACE_MASS_ENTITY_CREATED(Entity, Archetype) \
	FMassTrace::EntityCreated(Entity, Archetype);

// 批量创建：逐条发 EntityCreated。
// 注意：此宏展开后没有外层 {}，直接用 for。在 `if(...) UE_TRACE_MASS_ENTITIES_CREATED(...)` 这种
// 没有大括号的场景可能产生语法歧义，调用方最好始终用大括号包裹。
#define UE_TRACE_MASS_ENTITIES_CREATED(EntityHandles, Archetype) \
	for (const FMassEntityHandle& Entity : EntityHandles) \
	{ \
		FMassTrace::EntityCreated(Entity, Archetype); \
	}

#define UE_TRACE_MASS_ENTITY_MOVED(Entity, NewArchetype) \
	FMassTrace::EntityMoved(Entity, NewArchetype);

#define UE_TRACE_MASS_ENTITY_DESTROYED(Entity) \
	FMassTrace::EntityDestroyed(Entity);

// 注意：批量销毁这里是逐条 EntityDestroyed 而非调用 EntitiesDestroyed(Array)。
// 如果需要节省 Trace 带宽，直接调用 FMassTrace::EntitiesDestroyed 更合算。
#define UE_TRACE_MASS_ENTITIES_DESTROYED(EntityHandles) \
	for (const FMassEntityHandle& Entity : EntityHandles) \
	{ \
		FMassTrace::EntityDestroyed(Entity); \
	}

// Query 类宏：需要在 FMassEntityQuery 的成员函数内使用（依赖 `this`）。
#define UE_TRACE_MASS_QUERY_CREATED() \
	FMassTrace::QueryCreated(this);

#define UE_TRACE_MASS_QUERY_DESTROYED() \
	FMassTrace::QueryDestroyed(this);

#define UE_TRACE_MASS_QUERY_REGISTERED_TO_PROCESSOR(Processor) \
	FMassTrace::QueryRegisteredToProcessor(this, Processor);

#define UE_TRACE_MASS_QUERY_ARCHETYPE_ADDED(Archetype) \
	FMassTrace::QueryArchetypeAdded(this, Archetype);

// 在 Query 的 ForEachEntityChunk 入口处放置此宏：
//   构造一个名为 _ScopedQueryForEachTrace 的 RAII 对象，遍历结束自动发结束事件。
//   同时约定了该变量名，下面 REPORT_ARCHETYPE 才能引用到它。
#define UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH() \
	FMassTrace::FScopedQueryForEachTrace _ScopedQueryForEachTrace(this);

// 在遍历内每处理一个 archetype 时调用，累加 chunk/entity 计数。
#define UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH_REPORT_ARCHETYPE(Archetype) \
	_ScopedQueryForEachTrace.ReportArchetype(Archetype);

#else //UE_MASS_TRACE_ENABLED

// ---------------------------------------------------------------------------------------------------------------------
// Trace 未启用时的空宏。这些宏保持语法兼容（仍是"语句"），但展开后没有任何代码，零开销。
// 注意：UE_TRACE_MASS_ARCHETYPE_CREATED 这里的参数名被错误地写成了 PhaseID（原本应为 Archetype），
// 属于纯文档层面的小瑕疵，不影响功能（宏体为空）。
// ---------------------------------------------------------------------------------------------------------------------

#define UE_TRACE_MASS_ARCHETYPE_CREATED(PhaseID)
#define UE_TRACE_MASS_ENTITY_CREATED(Entity, Archetype)
#define UE_TRACE_MASS_ENTITIES_CREATED(EntityHandles, Archetype)
#define UE_TRACE_MASS_ENTITY_MOVED(Entity, NewArchetype)
#define UE_TRACE_MASS_ENTITY_DESTROYED(Entity)
#define UE_TRACE_MASS_ENTITIES_DESTROYED(EntityHandles)
#define UE_TRACE_MASS_PHASE_BEGIN(PhaseID)
#define UE_TRACE_MASS_PHASE_END(PhaseID)
#define UE_TRACE_MASS_QUERY_CREATED()
#define UE_TRACE_MASS_QUERY_DESTROYED()
#define UE_TRACE_MASS_QUERY_REGISTERED_TO_PROCESSOR(Processor)
#define UE_TRACE_MASS_QUERY_ARCHETYPE_ADDED(Archetype)
#define UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH()
#define UE_TRACE_SCOPED_MASS_QUERY_FOR_EACH_REPORT_ARCHETYPE(Archetype)

#endif //UE_MASS_TRACE_ENABLED