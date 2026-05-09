// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationFragment.h —— Iris "Fragment" 抽象层（核心扩展点之一）
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/  ← 对外可扩展。
// 角色：Iris 把"一个对象的某一块可复制状态"抽象为一个 FReplicationFragment。一个 NetObject 由若干 Fragment 组成，
//      每个 Fragment 绑定到一个 FReplicationStateDescriptor（状态形态）+ 一个 ExternalSrcBuffer（运行时缓冲指针）。
//      FReplicationInstanceProtocol 记录这张"Fragment 表"，用于 Poll/Quantize/Apply 全流程。
//
// 与文档对应：
//   - Iris_Architecture.md §3.7 / §3.8（Fragment 与 Protocol/Operation 的关系）。
//   - ReplicationState.md §2.1 / §2.4（Descriptor + PropertyReplicationState）。
//   - ReplicationSystem.md §4 / §6（Protocol + Polling + 7 个子模块）。
//
// 主要内容：
//   1) FReplicationStateOwnerCollector
//       - 用于把"应当收到 PreNetReceive/PostNetReceive/RepNotifies"的 UObject 收集起来；
//       - 仅在向后兼容路径（NeedsLegacyCallbacks）下使用；同一对象会被去重（栈顶判等）。
//
//   2) FReplicationStateApplyContext
//       - Apply 阶段（接收侧）传给每个 Fragment 的上下文：Descriptor、NetSerializationContext、StateBufferData、
//         以及一组 bit 标志（bIsInit / bHasUnresolvableReferences / bMightHaveUnresolvableInitReferences）。
//       - StateBufferData 是 union：默认 ReplicationSystem 给一份临时 dequantize 后的"外部状态缓冲"；
//         若 Fragment 设置了 HasPersistentTargetStateBuffer trait，则改为提供 ChangeMaskData + 原始量化 RawStateBuffer。
//
//   3) EReplicationStateToStringFlags
//       - ReplicatedStateToString 的过滤参数（仅打印 dirty 成员等）。
//
//   4) EReplicationFragmentTraits（关键）
//       - 一个对象级 fragment 的能力位集合，决定它在 Poll/Apply/Send/Recv 各阶段的行为：
//         * HasInterpolation              —— 预留（未实现）。
//         * HasRepNotifies                —— 描述符里有 RepNotify 函数，需要在 Apply 后回调。
//         * KeepPreviousState             —— Apply 前需保存上一帧外部状态（供 RepNotify 比较旧值）。
//         * DeleteWithInstanceProtocol    —— 由 InstanceProtocol 管控生命周期（销毁时一并 delete）。
//         * HasPersistentTargetStateBuffer—— Fragment 自己维护持久 ExternalState；Apply 时拿到 ChangeMaskData+RawStateBuffer。
//         * CanReplicate                  —— 可作为复制源（服务端需要 SendState）。
//         * CanReceive                    —— 可作为接收端（客户端需要 RecvState/RepNotify 等）。
//         * NeedsPoll                     —— 需要 PollAndCopy 阶段触达（从 UObject 拉脏标记/拉值到内部 State）。
//         * NeedsLegacyCallbacks          —— 需要 PreNetReceive/PostNetReceive 等遗留回调。
//         * NeedsPreSendUpdate            —— 需要在 PreSendUpdate 时被回调（少数 Fragment 用）。
//         * HasPushBasedDirtiness         —— 支持 PushModel 标脏（部分成员）。
//         * HasFullPushBasedDirtiness     —— 全部成员都是 PushModel（无需 Poll 全比对，只看 dirty 标记）。
//         * HasPropertyReplicationState   —— Fragment 内部使用 FPropertyReplicationState（PropertyReplicationFragment / FastArrayReplicationFragment）。
//         * HasObjectReference            —— 含 UObject* / Soft 引用，需要 ReferenceCache 处理（GC、解引）。
//         * SupportsPartialDequantizedState —— ApplyReplicatedState 能正确处理部分 dequantize 状态（按 ChangeMask）。
//
//   5) EReplicationFragmentPollFlags
//       - PollReplicatedState 的传参：
//         * ForceRefreshCachedObjectReferencesAfterGC —— GC 后强制刷新引用缓存（即使没有标脏）。
//         * PollDirtyState   —— 只 poll dirty 成员（PushModel 路径）。
//         * PollAllState     —— 全量 poll（传统行为）。
//         * EnableVerboseProfiling —— 打开细粒度 profiler scope。
//
//   6) FReplicationFragment（抽象基类）
//       - 关键虚函数：
//         * ApplyReplicatedState —— 接收侧把状态写回 UObject（必须实现）。
//         * CollectOwner          —— 收集需要 PreNet/PostNetReceive 的 owner（向后兼容）。
//         * CallRepNotifies       —— 触发 RepNotify。仅当 HasRepNotifies trait 设置时被调用。
//         * PollReplicatedState   —— 服务端从 owner 抓取属性到内部 State；返回是否变脏。
//         * ReplicatedStateToString —— 调试输出。
//
//   7) EFragmentRegistrationFlags + FReplicationFragmentInfo + FReplicationFragments
//       - 注册 fragment 的入参枚举与"已注册 fragment 信息"小结构。
//       - FReplicationFragments：TArray<FReplicationFragmentInfo, TInlineAllocator<32>> —— 注册阶段的临时容器，
//         之后会被打包进 FReplicationInstanceProtocol。
//
//   8) FFragmentRegistrationContext
//       - 注册时贯穿调用链的上下文对象：持有 ReplicationStateRegistry / ReplicationSystem / MainObject / Template / Traits。
//       - Bridge 在 BeginReplication 路径上构造它，调用 fragment 的 Register() 让其把自己 Add 到 Fragments 列表中。
//       - SetIsFragmentlessNetObject —— 显式声明"我没有可复制属性也没有 RPC"，避免被误报警告。
//       - friend Private::FFragmentRegistrationContextPrivateAccessor —— 只允许 Iris 私有代码访问 Template / DefaultStateSource。
//
// 设计要点：
//   - Fragment 把"复制状态"和"对外接口"两件事解耦：Descriptor 只描述形态，Fragment 描述行为。
//   - 同一个 Descriptor 可被多个 Fragment 形态共享（PropertyReplicationFragment / FastArrayReplicationFragment / 用户自定义）。
//   - Owner 概念仅在"传统兼容路径"下需要——Iris 的核心数据通路只走 ExternalSrcBuffer 指针 + Descriptor。
// =====================================================================================================================

#pragma once

#include "CoreTypes.h"

#include "Containers/Array.h"
#include "Containers/Map.h"

#include "Misc/CoreMiscDefines.h"
#include "Misc/EnumClassFlags.h"

// Forward declarations

class UReplicationSystem;
namespace UE::Net
{
	class FFragmentRegistrationContext;
	class FNetSerializationContext;
	struct FReplicationStateDescriptor;
	namespace Private
	{
		class FReplicationStateDescriptorRegistry;
		struct FFragmentRegistrationContextPrivateAccessor;
	}
}

namespace UE::Net
{

// FReplicationStateOwnerCollector
// 在向后兼容路径下，Apply 流程需要给每个"逻辑 owner"调用一次 PreNetReceive / PostNetReceive / PostRepNotifies。
// 一个 NetObject 可能由多个 Fragment 组成，但它们大多共享同一个 Owner（UObject*）；
// 本收集器以一个固定外部数组为后盾（不做动态分配），并对"连续 Add 同一个对象"做栈顶去重。
class FReplicationStateOwnerCollector
{
public:
	// 构造：传入由调用方分配的 Owners 数组以及容量。
	FReplicationStateOwnerCollector(UObject** InOwners, uint32 InMaxOwnerCount)
	: Owners(InOwners)
	, OwnerCount(0)
	, MaxOwnerCount(InMaxOwnerCount)
	{
	}

	// 添加一个 owner；与栈顶相同则忽略，达到 MaxOwnerCount 后静默丢弃（调用方需保证容量足够）。
	void AddOwner(UObject* Object)
	{
		if (OwnerCount < MaxOwnerCount)
		{
			if (OwnerCount && Owners[OwnerCount - 1] == Object)
			{
				return;
			}
			Owners[OwnerCount] = Object;
			++OwnerCount;
		}
	}

	UObject** GetOwners() const { return Owners; }
	uint32 GetOwnerCount() const { return OwnerCount; }

	// 复用同一个 collector 实例时清空当前内容。
	void Reset() { OwnerCount = 0; }

private:
	UObject** Owners;
	uint32 OwnerCount;
	const uint32 MaxOwnerCount;
};

// FReplicationStateApplyContext
// Apply 阶段（接收侧）传给 Fragment 的上下文。
// 由 FDequantizeAndApplyHelper / FReplicationInstanceOperations::DequantizeAndApply 在调用 Fragment 前组装。
struct FReplicationStateApplyContext
{
	// 当前正在应用的 ReplicationState 描述符（成员表 / changemask 偏移 / 默认值等）。
	const FReplicationStateDescriptor* Descriptor;
	// 序列化上下文（含 BitStream / ChangeMask / 内部 NetSerializationContext / 错误状态等）。
	FNetSerializationContext* NetSerializationContext;

	// We have two different variants of applying state data
	// Either we let the ReplicationSystem create and construct a temporary state OR it is up to the ReplicationFragment to manage the external buffer
	// We might for example want to allow a fragment to keep a persistent buffer and simply write directly into it.
	// 两种 Apply 路径的状态缓冲表示：
	//  - 默认（无 HasPersistentTargetStateBuffer trait）：Iris 临时构造一份 ExternalStateBuffer（已 dequantize），Fragment 直接读用户态结构。
	//  - 有 HasPersistentTargetStateBuffer trait：Fragment 自己持有外部缓冲（如 FastArrayReplicationFragment），
	//    Iris 仅传入 ChangeMaskData + RawStateBuffer（量化后的内部状态），由 Fragment 自行 partial dequantize。
	union FStateBufferData
	{
		uint8* ExternalStateBuffer;
		struct
		{
			const uint32* ChangeMaskData;
			const uint8* RawStateBuffer;
		};
	};

	FStateBufferData StateBufferData;

	// Indicates that this is the first time we receive this state, (it might also be set if we get this call after resolving references)
	// 这是该对象 / 状态的"首次接收"——典型用于 RepNotify 的"InitialOnly"语义：
	// 也可能是引用解析完成后的"补送" Apply（仍带 bIsInit），表示之前因引用未解析延迟过。
	uint32 bIsInit : 1;
	// Set when we are applying state data and a member of the state contains an unresolvable object reference
	// 当前应用的状态中有 UObject* 引用尚不能解析（远端对象未到达 / 异步加载中），上层据此延迟回调或提示。
	uint32 bHasUnresolvableReferences : 1;
	// Set if this fragment is for a init state and the object has unresolved init references
	// 该 Fragment 属于 init state（不参与 changemask 跟踪、首次必发）且其引用尚未解析；
	// 仅 init state 关心此位（普通帧用 bHasUnresolvableReferences）。
	uint32 bMightHaveUnresolvableInitReferences : 1;
};

// 控制 ReplicatedStateToString 输出过滤。
enum class EReplicationStateToStringFlags : uint32
{
	None								= 0U,
	// 只打印当前 dirty 的成员（用于诊断"为什么这个对象本帧被发送"）。
	OnlyIncludeDirtyMembers				= 1U,
};
ENUM_CLASS_FLAGS(EReplicationStateToStringFlags);

/** Traits describing a ReplicationFragmet */
// EReplicationFragmentTraits —— Fragment 能力位
// 详细位含义见文件头注释。常用判断："是否参与 Send"、"是否参与 Recv"、"是否需要 Poll"、"是否 PushBased"。
// 每个具体 Fragment 在构造时根据 Descriptor 的 EReplicationStateTraits 推导出自身 Traits 集合。
enum class EReplicationFragmentTraits : uint32
{
	None								= 0,

	// Not implemented
	// 预留：插值（未实现）。
	HasInterpolation					= 1,

	// Fragment has rep notifies
	// 含 RepNotify 函数；接收侧会在 Apply 后调度 CallRepNotifies。
	HasRepNotifies						= HasInterpolation << 1,

	// Save previous state before apply
	// Apply 之前要把"本地当前外部状态"另存一份，供 RepNotify 比较旧值用。
	// 只有需要旧值的 RepNotify（带参 OldValue）才需此 trait。
	KeepPreviousState					= HasRepNotifies << 1,

	// Fragment is owned by instance protocol and will be destroyed when the instance protocol is destroyed
	// Fragment 实例由 InstanceProtocol 管理生命周期：协议销毁时一并 delete。
	// 大多数自动注册的 Fragment（PropertyReplicationFragment 等）都设置了此位。
	DeleteWithInstanceProtocol			= KeepPreviousState << 1,

	// Pass raw quantized data when applying state data
	// Apply 时不使用临时构造的 ExternalState，而是直接拿 RawStateBuffer + ChangeMaskData 给 Fragment 自己处理。
	// 用于 Fragment 维护"持久外部缓冲"的场景，如 FastArrayReplicationFragment。
	HasPersistentTargetStateBuffer		= DeleteWithInstanceProtocol << 1,

	// Fragment can be used to source replication data
	// 服务端能从此 Fragment 读出复制源数据（即拥有 SendState）。
	CanReplicate						= HasPersistentTargetStateBuffer << 1,

	// Fragment can receive replication data
	// 客户端能向此 Fragment 写入复制数据（即拥有 RecvState）。
	CanReceive							= CanReplicate << 1,

	// Fragment requires polling to detect dirtiness
	// 需要在 Poll 阶段从 owner 拉值/检测脏；非 PushBased 的传统属性都需此 trait。
	NeedsPoll							= CanReceive << 1,

	// Fragment requires legacy callbacks
	// 需要在 Apply 前后调用 PreNet/PostNetReceive 等遗留回调。
	NeedsLegacyCallbacks				= NeedsPoll << 1,

	// Fragment requires PreSendUpdate to be called 
	// 在每帧 PreSendUpdate 阶段要被回调（少数特殊 Fragment 用）。
	NeedsPreSendUpdate					= NeedsLegacyCallbacks << 1,

	// Fragment supports push based dirtiness
	// 支持 PushModel：部分成员可由游戏代码主动标脏（MARK_PROPERTY_DIRTY 宏 + Iris LegacyPushModel）。
	HasPushBasedDirtiness				= NeedsPreSendUpdate << 1,

	// Fragment supports full push based dirtiness
	// 全部成员都是 PushModel：Poll 阶段可完全跳过比对，只检查 dirty 标记即可知道是否变脏。
	HasFullPushBasedDirtiness			= HasPushBasedDirtiness << 1,

	// Fragment is a PropertyReplication, or is using PropertyReplicationState
	// 内部使用 FPropertyReplicationState（即 PropertyReplicationFragment 系列与 FastArrayReplicationFragment）。
	HasPropertyReplicationState			= HasFullPushBasedDirtiness << 1,

	// Fragment has object references
	// 含 UObject 引用：参与 ObjectReferenceCache 的解析、GC 后的引用刷新等。
	HasObjectReference					= HasPropertyReplicationState << 1,

	// Fragment supports partial dequantized state in apply
	// Apply 时 Iris 可能仅 dequantize 一部分成员（按 changemask）；该 trait 表明 Fragment 能正确处理这种"部分 state"。
	SupportsPartialDequantizedState		= HasObjectReference << 1,
};
ENUM_CLASS_FLAGS(EReplicationFragmentTraits);

// PollReplicatedState 调用方式选项
enum class EReplicationFragmentPollFlags : uint32
{
	None = 0,

	/** If set, we need to refresh all cached object references, typically set after a GC to detect stale references in cached data, regardless of if they are dirty or not. */
	// GC 后强制刷新缓存的 UObject 引用，即使该 Fragment 未脏；防止 GC 后悬挂指针。
	ForceRefreshCachedObjectReferencesAfterGC = 1,

	/** Pull members marked for polling or marked as dirty in the changemask, non-push based members will alwasys be considered for polling. Note: if ForceRefreshCachedObjectReferencesAfterGC is set as well, object references must be polled as well. */
	// 仅 poll：被显式标记为 dirty 的成员 + 所有非 PushBased 成员。配合 ForceRefresh 标志可一并刷新引用。
	PollDirtyState = ForceRefreshCachedObjectReferencesAfterGC << 1,

	/** Normal full poll. */
	// 全量 poll：所有成员均参与值比对（不论是否 PushBased）。
	PollAllState = PollDirtyState << 1,

	/** Enable Verbose profiling  */
	// 开启细粒度 profiler scope（成员级），常规帧关闭以省 CPU。
	EnableVerboseProfiling = PollAllState << 1,
};
ENUM_CLASS_FLAGS(EReplicationFragmentPollFlags);

/** 
* ReplicationFragment
* Binds one or more ReplicationState(s) to the owner and is the key piece to defining the state that makes up a NetObject
* Used to extract and set state data on the game side.
*/
// FReplicationFragment —— 抽象基类
// 一个 Fragment 把一块"逻辑状态"绑定到 owner：Descriptor 是形态描述，外部 SrcBuffer 是运行时存储位置。
// 派生类必须实现 ApplyReplicatedState；其它虚函数为可选（按 traits 决定是否被调用）。
class FReplicationFragment
{
public:
	FReplicationFragment(const FReplicationFragment&) = delete;
	FReplicationFragment& operator=(const FReplicationFragment&) = delete;

	// 构造：仅设置 traits，派生类负责保存 Descriptor / Owner / SrcBuffer 等。
	explicit FReplicationFragment(EReplicationFragmentTraits InTraits) : Traits(InTraits) {}
	virtual ~FReplicationFragment() {}

	/** Traits */
	inline EReplicationFragmentTraits GetTraits() const { return Traits; }

	/**
	* This is called from the ReplicationSystem / ReplicationBridge whenever we have new data
	* Depending on the traits of the fragment we either get pointer to a StateBuffer in the expected external format including changemask information or we
	* get the raw quantized state buffer along with the changemask information for any received states.
	*/
	// 接收侧"应用状态到 owner"的核心入口（必须实现）。
	// 实参 Context.StateBufferData 形态由 HasPersistentTargetStateBuffer trait 决定（见 union 注释）。
	virtual void ApplyReplicatedState(FReplicationStateApplyContext& Context) const = 0;

	/**
	* Optional method required for backwards compatibility mode which is used to propagate required calls to Pre/PostNetReceive/PostRepNotifies.
	*/
	// 仅 NeedsLegacyCallbacks 时调用：把"应当收到 Pre/PostNetReceive 的 owner"加入 collector。
	// 默认空实现：纯量化 fragment 通常无需 owner 侧回调。
	virtual void CollectOwner(FReplicationStateOwnerCollector* Owners) const {};

	/**
	* Optional method required for backwards compatibility mode which will be invoked for all Fragment with the EReplicationFragmentTraits::HasRepNotifies trait set.
	*/
	// 触发 RepNotify。仅 HasRepNotifies trait 设置时被调用。
	// PropertyReplicationFragment 在此遍历 dirty 成员、按 PreviousState 比较、并调用 UFunction。
	virtual void CallRepNotifies(FReplicationStateApplyContext& Context) {};

	/**
	 * Optional Poll method required for backwards compatibility mode which will be invoked for all Fragment with the EReplicationFragmentTraits::NeedsPoll trait set.
	 * @return True if the state is dirty, false if not.
	 */
	// 服务端 Poll：从 owner 抓取属性值到内部 SendState（或检测 PushModel 的 dirty 标记）。
	// 仅 NeedsPoll trait 时被调用。返回 true 表示有变化（写 changemask）。
	virtual bool PollReplicatedState(EReplicationFragmentPollFlags PollOption = EReplicationFragmentPollFlags::PollAllState) { return false; }

	/**
	* Optional method to output state data to StringBuilder.
	*/
	// 调试输出：把当前 ApplyContext 表示的状态打印到 StringBuilder（NetTrace / RepDebugger 用）。
	virtual void ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags = EReplicationStateToStringFlags::None) const {};
	
protected:
	// 派生类构造时设置；之后可由派生类基于 Descriptor traits 在自身构造时追加更多位。
	EReplicationFragmentTraits Traits;
};

// EFragmentRegistrationFlags
// CreateAndRegisterFragmentsForObject 的入参，控制注册阶段的策略。
enum class EFragmentRegistrationFlags : uint32
{
	None = 0U,
	
	// Indicates that we only should register RPC:s
	// 仅注册 RPC（含 UFunction 的 fragment），不注册属性。常用于"我只想给这个对象开 RPC 通道，不复制属性"。
	RegisterRPCsOnly = 1U,
	// Indicates that this objects should use the CDO for class defaults instead of the archetype
	// 默认状态来源用 CDO 而非 archetype（对一些子对象/动态创建对象适用）。
	InitializeDefaultStateFromClassDefaults = RegisterRPCsOnly << 1U,
	// Allow building descriptors for FastArrays that contain additional properties, NOTE: This should be avoided as fastarrays normally only should contain a single replicated property.
	// 允许 FastArray 携带额外的复制属性。FastArray 规范上只应有一个 TArray 成员；本 flag 是兼容存量代码的妥协。
	AllowFastArraysWithAdditionalProperties = InitializeDefaultStateFromClassDefaults << 1U,
};
ENUM_CLASS_FLAGS(EFragmentRegistrationFlags);

/**
* Used when registering ReplicationFragments
*/
// 注册阶段的单个 Fragment 项：包含描述符、SrcBuffer、Fragment 实例指针。
// 之后会被 protocol 工厂打包到 FReplicationInstanceProtocol。
struct FReplicationFragmentInfo
{
	const FReplicationStateDescriptor* Descriptor = nullptr;
	void* SrcReplicationStateBuffer = nullptr;
	FReplicationFragment* Fragment = nullptr;
};
// 注册过程使用的临时容器；TInlineAllocator<32> 避免常见对象的小数量分配。
typedef TArray<FReplicationFragmentInfo, TInlineAllocator<32>> FReplicationFragments;

// FFragmentRegistrationContext
// 在 BeginReplication 路径上为某个 NetObject 创建一次；
// 由 ReplicationFragmentUtil / 用户自定义注册函数把 Fragment 添加进来；
// 最后由 Bridge 把 Fragments / Template / DefaultStateSource 取走，构造 InstanceProtocol。
class FFragmentRegistrationContext
{
public:
	// 构造：只能由 Bridge 内部代码使用（接受 ReplicationStateRegistry 与 ReplicationSystem 私有指针）。
	explicit FFragmentRegistrationContext(Private::FReplicationStateDescriptorRegistry* InReplicationStateRegistry, UReplicationSystem* InReplicationSystem, const EReplicationFragmentTraits InFragmentTraits, UObject* MainObject)
		: ReplicationStateRegistry(InReplicationStateRegistry)
		, ReplicationSystem(InReplicationSystem)
		, MainObjectInstance(MainObject)
		, FragmentTraits(InFragmentTraits)
	{
	}

	/** Returns the traits */
	// 返回当前注册批次默认应用的 traits（CanReplicate / CanReceive 等由 Bridge 决定）。
	// 单个 Fragment 在构造时会以此为基础再追加自身 traits。
	const EReplicationFragmentTraits GetFragmentTraits() const { return FragmentTraits; }

	/** Register ReplicationFragment */
	// 注册一个已构造好的 Fragment：把它和 Descriptor + SrcBuffer 一起加入列表。
	// SrcBuffer 是 ExternalSrcBuffer：服务端从该地址读、客户端往该地址写。
	void RegisterReplicationFragment(FReplicationFragment* Fragment, const FReplicationStateDescriptor* Descriptor, void* SrcReplicationStateBuffer)
	{ 
		Fragments.Add({ Descriptor, SrcReplicationStateBuffer, Fragment }); 
	}

	/** Call this when you have a netobject that is replicated but will never contain any RPCs or replicated properties. Prevents the registration code from complaining about potential errors. */
	// 显式声明本对象不含任何复制属性 / RPC，避免被警告"忘了注册 fragment"。
	void SetIsFragmentlessNetObject(bool bIsFragmentless) { bIsAFragmentlessNetObject = bIsFragmentless; }

	/** Returns true when the netobject knows it won't contain any replicated properties or RPCs */
	bool IsFragmentlessNetObject() const { return bIsAFragmentlessNetObject; }

	/** Returns true if the fragments (or the lack of) were registered by the instance */
	// 注册阶段是否"已表态"——要么注册了 fragment，要么显式声明 fragmentless。
	bool WasRegistered() const
	{
		return Fragments.Num() > 0 || bIsAFragmentlessNetObject;
	}

	/** Returns the number of fragments registered */
	int32 NumFragments() const { return Fragments.Num(); }

	// 私有访问者：让 ReplicationFragmentInternal.h 中的 Accessor 类能读 / 写部分私有字段，但不暴露给普通用户。
	friend Private::FFragmentRegistrationContextPrivateAccessor;

private:

	// 注册期间临时累积的 fragment 列表；最终被搬到 InstanceProtocol。
	FReplicationFragments Fragments;
	// 描述符注册表（用于去重、复用同一 Descriptor）。
	Private::FReplicationStateDescriptorRegistry* ReplicationStateRegistry;
	// 反向访问 ReplicationSystem（用于读 config / push model 等）。
	UReplicationSystem* ReplicationSystem;

	/** The main object used to pull most fragments from. Used as the basic for the default state source */
	// 当前 NetObject 的"主对象"——属性大多来自它（也是默认状态来源）。
	// 子对象 / 组件等可能由独立 fragment 注入，但这里仍保留 main 作为锚点。
	UObject* MainObjectInstance = nullptr;

	/** The externally provided template used to clone the main object instance from */
	// 调用方可显式指定的模板对象——例如蓝图实例化时不能用 archetype 时由 caller 传入。
	const UObject* Template = nullptr;

	/** The final reference we used to build the default state for the main object. Since Template is optional this lets callers know what was used from a template, archetype or cdo. */
	// 真正用于构造默认状态的对象（archetype / template / CDO 三选一）；外层据此判断 default state 出处。
	const UObject* MainObjectDefaultStateSource = nullptr;

	// 本批次默认 traits（由 Bridge 决定 CanReplicate 还是 CanReceive，子 fragment 在构造时基于此扩展）。
	const EReplicationFragmentTraits FragmentTraits;

	bool bIsAFragmentlessNetObject = false;

};

}
