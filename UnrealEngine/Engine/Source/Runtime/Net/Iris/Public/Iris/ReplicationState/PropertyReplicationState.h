// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Templates/RefCounting.h"

namespace UE::Net
{
	struct FReplicationStateDescriptor;
}

namespace UE::Net
{

 /** 
 * ReplicationState created at runtime using a descriptor built from existing reflection data
 * StateBuffer contains storage for all properties described by the descriptor,
 * When polling data from the source object properties we will compare the value with what we have stored in the statebuffer
 * and mark the member as dirty.if the value differs
 *
 * Polling is quite expensive but we only do it once per update of the replication system
 *
 * ============================================================================================
 * 中文说明：FPropertyReplicationState —— Iris 复制状态运行时"操作壳"
 * ============================================================================================
 * 这是 Iris 在运行时操作"外部表示（external representation）"复制状态缓冲区的核心类。
 * 它依附于一个引用计数的 FReplicationStateDescriptor（不可变、可跨连接共享），并管理
 * 一块按 Descriptor 指定布局分配的 StateBuffer——内含：
 *   1) [可选] FReplicationStateHeader（含 NetHandle 绑定 + dirty 标记位）；
 *   2) 全部 Member 的外部表示数据（与 UObject native layout 对齐，但只包含 Replicated 字段）；
 *   3) MemberChangeMask 位图（标记哪些成员发生了变化）；
 *   4) [可选] MemberConditionalChangeMask（自定义条件位图，与 Lifetime/Custom Condition 联动）；
 *   5) [可选] MemberPollMask（PushModel 标脏位图，仅 push-based 成员使用）。
 *
 * 两种内存所有权（EStateOwnership 概念）：
 *   - Owns（bOwnState=1）：构造时 Malloc 自有 StateBuffer，析构时释放，用于内部临时快照、
 *     RepNotify 比较前的"上一帧"快照、或独立 polling state 等场景；
 *   - ExternalBuffer（bOwnState=0）：通过 InjectState 绑定到外部已构造的 StateBuffer，
 *     该缓冲区由 NetRefHandleManager / ReplicationFragment 管理生命周期，本类只是"壳"。
 *
 * 三种 Poll 路径（运行时 Server 端最关键的入口）：
 *   - PollPropertyReplicationState：传统 poll，遍历全部成员从 SrcInstance 拷贝 + 比较 + 标 dirty。
 *   - PollPushBasedPropertyReplicationState：仅 poll PushModel 中显式标脏的成员（性能更优）。
 *   - PollObjectReferences：仅 poll 含 ObjectReference 的成员，用于 unresolved reference 重试。
 *
 * RepNotify 三阶段：
 *   1) 旧值快照：StoreCurrentPropertyReplicationStateForRepNotifies()——在收到新状态前，
 *      把当前 Object 上的旧值拷进 PreviousState 的 StateBuffer；
 *   2) 应用新值：PushPropertyReplicationState()——把 ReceivedState 写回 Object 属性；
 *   3) 触发回调：CallRepNotifies(Object, PreviousState, ...)——比较 PrevState 与 Current，
 *      为每个 dirty 成员调用对应的 UFUNCTION RepNotify（KeepPreviousState 路径还会把旧值
 *      作为参数传入）。
 *
 * 与 FReplicationStateHeader / FDirtyNetObjectTracker 的联动：
 *   每次 MarkDirty/MarkArrayDirty 都会更新 ChangeMask，并在 IsBound() 时通知 NetObject Header
 *   为 dirty——上层 Tick 中 FDirtyNetObjectTracker 会据此把 Object 加入复制候选列表。
 */
class FPropertyReplicationState
{
public:
	/** Construct a new state, StateBuffer will be allocated and constructed according to data in the descriptor */
	// 中文：拥有所有权的构造——按 Descriptor->ExternalSize/Alignment 分配并构造 StateBuffer，
	//       析构时会反向调用 DestructPropertyReplicationState 并 Free 内存。bOwnState=1。
	IRISCORE_API explicit FPropertyReplicationState(const FReplicationStateDescriptor* Descriptor);

	/** Construct a new state, using already constructed state in InStateBuffer */
	// 中文：注入式构造（外部所有权）——直接绑定到已经被外部系统构造完毕的 StateBuffer，
	//       析构时不释放内存。bOwnState=0。这是在 Fragment / NetRefHandleManager 中最常用的构造形式。
	IRISCORE_API FPropertyReplicationState(const FReplicationStateDescriptor* Descriptor, uint8* InStateBuffer);

	// 中文：析构。仅当 bOwnState 为 1 时才析构 + Free StateBuffer。
	IRISCORE_API ~FPropertyReplicationState();

	/** Copy constructor, will not copy internal data */
	// 中文：拷贝构造。先按 Other 的 Descriptor 自分配 + 构造 StateBuffer（变成 Owns 状态），
	//       然后通过 operator= 把 Other 的状态值拷贝过来。注意不会复制"绑定"关系。
	IRISCORE_API FPropertyReplicationState(const FPropertyReplicationState& Other);

	/** 
	 * Assignment operator, it is debatable if this should be provided or not.
	 * if we are copying to a bound state we will perform a Set which will do a full property compare to properly update dirtiness
	 * if we are copying to a loose state we will just to a copy of all properties contained in the state, changemask will also be copied
	*/
	// 中文：拷贝赋值。两条路径——
	//   1) 目标是 bound（StateBuffer Header.IsBound() == true）：调用 Set()，对每个成员进行
	//      值比较以正确更新 dirty 位（保证 Tracker 通知）；
	//   2) 目标是 loose：直接 CopyPropertyReplicationState（包括 ChangeMask）整体拷贝。
	IRISCORE_API FPropertyReplicationState& operator=(const FPropertyReplicationState& Other);

	/** Set from Other this will update dirtiness by comparing all properties */
	// 中文：逐成员调用 SetPropertyValue —> PollPropertyValue，做"比较 + 拷贝 + 必要时标 dirty"。
	IRISCORE_API void Set(const FPropertyReplicationState& Other);
	
	/** Either StateBuffer is initialized and owned by this instance, or we have been injected with a state from the network system
		A default constructed state is not valid
	*/
	// 中文：StateBuffer 与 Descriptor 都非空才视为有效。默认构造（无参）是无效状态。
	IRISCORE_API bool IsValid() const;

	//
	// Note: There is little to none error checking in the methods below, they are mostly intended to be used from internal code and tests
	// Expected usage for this type of ReplicationState is through the polling system
	//
	// 中文：以下方法基本没有错误检查，主要供内部代码 / 单元测试使用，正常的运行时
	//       使用路径是通过 polling 系统（PollPropertyReplicationState 等）。

	/** Set the value at the provided Index, 
		The UProperty is looked up from the descriptor using the index, if the value differs the statemask is updated
		Mostly intended for test code
		Normal use of this class is through the poll layer
		TODO: It would be nice if we could provide some validation on property types
	*/
	// 中文：通过 MemberIndex 设置成员值。内部调用 PollPropertyValue：
	//       会比较新旧值，不同则标 dirty，并把 SrcValue 拷入 StateBuffer。
	IRISCORE_API void SetPropertyValue(uint32 Index, const void* SrcValue);

	/**
	 * Retrieve the value at the provided Index by writing it to DstValue. 
	 * The property is looked up from the descriptor using the index.
	 * Mostly intended for test code. Normal use of this class is through the poll layer.
	 */
	// 中文：通过 MemberIndex 读取成员值（按 Property 的 Copy 语义写入 DstValue）。
	IRISCORE_API void GetPropertyValue(uint32 Index, void* DstValue) const;

	/** Is the property at the given index dirty, for properties with multiple bits in the statemask this will return true if any of those bits are set */
	// 中文：判断指定成员是否 dirty。对于占用多 bit 的成员（如 TArray 的 element changemask），
	//       任意一位被置位即视为 dirty。InitState 则改读 Header 上的 InitStateDirty 位。
	IRISCORE_API bool IsDirty(uint32 Index) const;

	/** Explicitly mark the property at the given Index as dirty, for properties with multiple bits in the statemask this will mark all bits dirty */
	// 中文：显式标记成员为 dirty。会一次性把该成员对应的全部 ChangeMask 位都置 1，
	//       并在 Header.IsBound() 时通知 NetObjectTracker。
	IRISCORE_API void MarkDirty(uint32 Index);

	/** $IRIS TODO: Move Poll/Push/CallRepNotifies out of PropertyReplicationState as loose functions */

	/**
	 * Poll src data from properties in SrcData, where SrcData is a UClass/UStruct containing properties.
	 * Compare and update representation in DstStateBuffer and update ChangeMask. This is currently used to track and update dirtiness for the properties.
	 * @return True if any state is dirty, false if not.
	 * @todo This requires some thought as the property offset we get in all are expressed from the base, but we do generate descriptors for all parts of the inheritance chain.
	 * Do we carry around the base pointer or should we recalculate the offsets when we build the descriptor
	*/
	// 中文：【传统 Poll 路径】遍历 Descriptor 中所有成员，逐个从 SrcData（UObject/UStruct 实例的
	//       原生内存）按 Property->GetOffset_ForGC() + Property->GetElementSize()*ArrayIndex 取地址，
	//       调用 PollPropertyValue 做"比较 + 拷贝 + 必要时标 dirty"。
	//       开销最大（全量扫描），但兼容所有 UPROPERTY，不要求 PushModel 标脏。
	//       @return 调用后整个 state 是否 dirty（Header 标记或任何 ChangeMask 位被置）。
	IRISCORE_API bool PollPropertyReplicationState(const void* RESTRICT SrcData);

	// 中文：Poll 行为参数（细粒度调优）：
	//   - bEnableVerboseProfiling：是否启用逐成员的 verbose profiler 作用域；
	//   - bForceRefreshObjectReferences：强制重新 poll 含 ObjectReference 的成员，
	//     用于 unresolved reference 解析重试（即便 PushModel 没有把它们标脏）。
	struct FPollParameters
	{
		bool bEnableVerboseProfiling = false;
		bool bForceRefreshObjectReferences = false;
	};

	/**
	 * Partial poll src data from properties in SrcData based on the state of the inlined per member pollmask
	 * non-pushbased properties are always polled, 
	 * Compare and update representation in DstStateBuffer and update ChangeMask. This is currently used to track and update dirtiness for the properties.
	 * @SrcData is a UClass/UStruct containing properties.
	 * @return True if any state is dirty, false if not.
	 * @todo This requires some thought as the property offset we get in all are expressed from the base, but we do generate descriptors for all parts of the inheritance chain.
	 * Do we carry around the base pointer or should we recalculate the offsets when we build the descriptor
	*/
	// 中文：【PushModel Poll 路径】仅对在 MemberPollMask（或 ChangeMask）中已被标脏的 push-based
	//       成员执行拷贝；非 push-based（如旧式属性）依旧每次都 poll；含 ObjectReference 的成员
	//       在 bForceRefreshObjectReferences=true 时也强制 poll。
	//       通常比传统 Poll 快得多，是当前主流路径（要求 UCLASS/UPROPERTY 满足 PushModel 条件）。
	IRISCORE_API bool PollPushBasedPropertyReplicationState(const void* RESTRICT SrcData, const FPollParameters& PollParameters);

	/** Certain rep notifies requires the current state to be stored before we overwrite it, this method will copy property values from src data, , where SrcData is a UClass/UStruct containing properties
	 * if the property is marked as dirty in NewStateToBeApplied
	 */
	// 中文：【RepNotify 阶段 1：保存旧值快照】在把"新收到的状态"应用到 UObject 之前，把 UObject 上
	//       当前的"旧值"按成员（仅那些有 RepNotifyFunction 且 NewStateToBeApplied 中是 dirty 的成员）
	//       拷进自身的 StateBuffer，作为 PreviousState 留待 CallRepNotifies 比较 / 传参用。
	//       NewStateToBeApplied==nullptr 或 IsInitState 时 fallback 为"全拷贝"。
	IRISCORE_API bool StoreCurrentPropertyReplicationStateForRepNotifies(const void* RESTRICT SrcData, const FPropertyReplicationState* NewStateToBeApplied);

	/** Push received state data to properties in DstData buffer. Note: DstData is a UClass/UStruct containing properties. Updates representation in DstStateBuffer and mark destination properties as dirty if updated. */
	// 中文：【Push 路径】把内部 StateBuffer 中的成员值反向写回 UObject 的原生属性内存（DstData）。
	//       bPushAll=true 强制全推（如客户端首次 Init 或 InitState），false 时仅推 dirty 成员。
	//       同时（在 WITH_PUSH_MODEL 下）通过 MARK_PROPERTY_DIRTY_UNSAFE 把这些 RepIndex 注册到
	//       PushModel 跟踪器，确保下一帧 Poll 时能感知到这些字段已变。Owner 用于定位 PushModel 句柄。
	IRISCORE_API void PushPropertyReplicationState(const UObject* Owner, void* RESTRICT DstData, bool bPushAll = false) const;


	/**
	 * Poll data from object referencing properties in SrcData, Note: SrcData is a UObject containing properties.
	 * Behaves like PollPropertyReplicationState when it comes to dirtiness and such.
	 * @return True if any state is dirty, false if not.
	 */
	// 中文：【ObjectReference Poll 路径】仅扫描 MemberTraits 中含 HasObjectReference 的成员，
	//       逐个 PollPropertyValue。该接口用于"unresolved reference 重试"：当某个 NetObject 引用
	//       在客户端尚未导入时，服务器/客户端可周期性地重新 poll 仅 reference 字段，避免全量 poll
	//       的开销。
	IRISCORE_API bool PollObjectReferences(const void* RESTRICT SrcData);

	/**
	 * Copy dirty properties from the other state including changemask
	 */
	// 中文：把 Other 中标 dirty 的成员（连同 ChangeMask 一起）合并到自身。两条分支：
	//   - 自身未 bound：直接调用 CopyDirtyMembers（按 ChangeMask 选择性拷贝）；
	//   - 自身已 bound：调用 Set() 做逐成员 compare + copy（保证 dirty tracking 正确传播）。
	IRISCORE_API void CopyDirtyProperties(const FPropertyReplicationState& Other);

	// 中文：CallRepNotifies 的参数包：
	//   - PreviousState：旧值快照（StoreCurrentPropertyReplicationStateForRepNotifies 产物），
	//     如果 RepNotify 函数声明带"上一个值"参数（KeepPreviousState 模式）会作为参数传入；
	//   - bIsInit：是否为初始状态（首次复制）。Init 路径下默认仅在新旧值不同时触发 RepNotify；
	//   - bOnlyCallIfDiffersFromLocal：仅在新值与本地值不同才调用 RepNotify（兼容 RepNotify_Changed），
	//     否则按 RepNotify_Always 语义无条件调用。
	struct FCallRepNotifiesParameters
	{
		// Previous state if requested
		const FPropertyReplicationState* PreviousState = nullptr;

		// This is an init state
		bool bIsInit = false;

		// Only call repnotify if value differs from local value
		bool bOnlyCallIfDiffersFromLocal = false;
	};

	/** Invoke repnotifies for all dirty members */
	// 中文：【RepNotify 阶段 3：触发回调】遍历所有成员，对每个有 RepNotifyFunction 且 dirty 的成员
	//       调用其 RepNotify UFUNCTION（PreviousState 作为可选 OutParm 参数）。对 C 数组成员只触发一次。
	IRISCORE_API void CallRepNotifies(void* RESTRICT DstData, const FCallRepNotifiesParameters& Params) const;
	// 中文：便捷重载，自动构造 FCallRepNotifiesParameters。
	inline void CallRepNotifies(void* RESTRICT DstData, const FPropertyReplicationState* PreviousState, bool bIsInit) const;

	/** Debug output state to FString */
	// 中文：调试输出，把当前 state 全部成员（或仅 dirty 成员）按 ExportTextItem_Direct 格式化。
	IRISCORE_API FString ToString(bool bIncludeAll = true) const;

	/** Debug output state to StringBuilder */
	// 中文：StringBuilder 版本（避免临时 FString 拷贝）。
	IRISCORE_API const TCHAR* ToString(FStringBuilderBase& StringBuilder, bool bIncludeAll = true) const;

	const FReplicationStateDescriptor* GetReplicationStateDescriptor() const { return ReplicationStateDescriptor; }
	uint8* GetStateBuffer() const { return StateBuffer; }

protected:
	/** Explicitly mark an array at the given Index as dirty. It will not mark element changemask bits as dirty. */
	// 中文：仅对 TArray 顶层 changemask 的"数组本身已修改"位（TArrayPropertyChangeMaskBitIndex=0）置 1，
	//       不影响每个元素的 element changemask 位。
	IRISCORE_API void MarkArrayDirty(uint32 Index);

public:
	// These methods are for internal use only.

	// 中文：TArray 元素 changemask 的位布局常量（Iris 在每个 TArray 成员的 ChangeMask 中预留：
	//   - bit 0：表示数组整体（含长度变化）已变；
	//   - bit 1..63：每个元素的 dirty 位，索引按 ElementIndex % 63 取模映射；
	//   - 共 64 位 = 1 + 63）
	enum : unsigned
	{
		// How many bits to use to track dirtiness for individual elements.
		// 中文：每个 TArray 成员用 63 bit 跟踪元素 dirty。
		TArrayElementChangeMaskBits = 63U,
		// The changemask bit into the TArray changemask info that indicates the TArray is dirty.
		// 中文：TArray "整体已 dirty"标记位 = 第 0 位。
		TArrayPropertyChangeMaskBitIndex = 0U,
		// At which offset in the TArray changemask element dirtiness begins.
		// 中文：元素 dirty 位的起始偏移 = 1（第 1 位起）。
		TArrayElementChangeMaskBitOffset = 1U,
	};

	/**
	 * If state has custom conditionals then this function will check whether the condition is enabled or not.
	 * If state doesn't have custom conditionals it will return true.
	 */
	// 中文：查询某成员的"自定义条件位"（位于 ConditionalChangeMask）。状态没有 LifetimeConditionals
	//       时直接返回 true。该位由 ReplicationSystem::SetReplicationConditionEnabled 等 API 控制，
	//       关闭时该成员不参与 dirty 检测与复制。
	IRISCORE_API bool IsCustomConditionEnabled(uint32 Index) const;

	/**
	 * Returns true if state is marked dirty for polling
	 */
	// 中文：state 是否需要本帧 poll——已经 dirty 或 PushModel pollmask 任一位被置 = true。
	IRISCORE_API bool IsDirtyForPolling() const;

private:
	// Returns true if member should be polled, returns true if memberpollmask is dirty or if changemask is dirty.
	// 中文：判断单个成员是否需要 poll（PushModel pollmask 命中 或 changemask 已 dirty）。
	bool IsDirtyForPolling(uint32 Index) const;

	// Returns true if changemask or header of the state is marked dirty
	// 中文：state 整体是否 dirty（Header 上的 StateDirty / InitStateDirty 任一为 true）。
	bool IsDirty() const;
	// 中文：当前 state 是否为 InitOnly（初始状态，使用 Header 上的 InitStateDirty 位代替 ChangeMask）。
	bool IsInitState() const;
	// 中文：分配 StateBuffer 并执行 Construct（含 Property->InitializeValue 与 ChangeMask 清零）。
	void ConstructStateInternal();
	// 中文：仅当 bOwnState=1 时 Destruct 并 Free StateBuffer。
	void DestructStateInternal();
	// 中文：把现成 StateBuffer 注入到该 state（外部所有权路径）。
	void InjectState(const FReplicationStateDescriptor* Descriptor, uint8* InStateBuffer);
	// 中文：单成员 Poll 的核心实现（对 TArray-with-element-changemask 走 InternalCompareAndCopyArrayWithElementChangeMask 分支；
	//       一般成员则做 InternalCompareMember 比较，不同则 MarkDirty + InternalCopyPropertyValue 拷贝）。
	void PollPropertyValue(uint32 Index, const void* SrcValue);
	// 中文：单成员 Push 的核心实现（调用 InternalApplyPropertyValue 把 Internal state 写到目标 Property 内存）。
	void PushPropertyValue(uint32 Index, void* DstValue) const;

	// 中文：成员变量：
	//   - ReplicationStateDescriptor：引用计数指向不可变的 Descriptor；
	//   - StateBuffer：实际数据缓冲区（外部表示，含 ChangeMask 等内嵌结构）；
	//   - bOwnState：1 = 自有 + 析构释放；0 = 外部注入 + 不释放。
	TRefCountPtr<const FReplicationStateDescriptor> ReplicationStateDescriptor;
	uint8* StateBuffer;
	uint32 bOwnState : 1;
};

void FPropertyReplicationState::CallRepNotifies(void* RESTRICT DstData, const FPropertyReplicationState* PreviousState, bool bIsInit) const
{
	FCallRepNotifiesParameters Params; 

	Params.PreviousState = PreviousState;
	Params.bIsInit = bIsInit;
	CallRepNotifies(DstData, Params);
}

}
