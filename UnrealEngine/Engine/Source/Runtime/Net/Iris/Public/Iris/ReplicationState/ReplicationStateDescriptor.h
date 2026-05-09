// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptor.h —— Iris 数据模型层最核心的不可变描述符
// -------------------------------------------------------------------------------------
// 【概念定位】
//   FReplicationStateDescriptor 是 Iris 整个网络复制管线的"形状描述"——任何一段被
//   网络化的数据（UClass 的属性子集、UStruct、UFunction 参数列表、FastArray Item 等）
//   都通过反射或手写宏一次性烘焙成一个 Descriptor。Descriptor 是：
//     1) 不可变 —— 烘焙后内容只读；
//     2) 引用计数 —— 由 NeedsRefCount trait 决定是否启用 atomic 引用计数；
//     3) 单块连续内存 —— Builder 用 FMemoryLayoutUtil 把 Descriptor 自身 + 全部成员
//        子数组（MemberDescriptors / Serializer / Traits / ChangeMask / Property / ...）
//        + SerializerConfig 等 一次 FMemory::Malloc 出来；Release 时整块 Free；
//     4) 跨连接共享 —— 多个连接的同类对象共用同一份 Descriptor（去重在 Registry）。
//
// 【关键字段分组】
//   - 成员数组（11 个，按用途分别索引相同 MemberIndex）：
//       MemberDescriptors             ：External/Internal 偏移（实际数据落点）
//       MemberSerializerDescriptors   ：每成员使用的 FNetSerializer + Config
//       MemberTraitsDescriptors       ：每成员的运行时 trait 位（HasObjectReference 等）
//       MemberPropertyDescriptors     ：与属性反射对应的 RepNotify / ArrayIndex（仅 Class 用）
//       MemberChangeMaskDescriptors   ：每成员在 ChangeMask 内占用的 bit 段
//       MemberReferenceDescriptors    ：所有 ObjectReference 的快速迭代表
//       MemberLifetimeConditionDescriptors：每成员的 ELifetimeCondition（仅 HasLifetimeConditionals）
//       MemberRepIndexToMemberIndexDescriptors：RepIndex → MemberIndex 反查（条件属性用）
//       MemberFunctionDescriptors     ：与 State 关联的 RPC/UFunction
//       MemberTagDescriptors          ：FRepTag 标签（NetCullDistance、RoleGroup 等）
//       MemberDebugDescriptors        ：调试名（在 Iris log/replay 中使用）
//   - 大小/对齐：External/Internal Size+Alignment、ChangeMaskBitCount、ChangeMasksExternalOffset、DefaultStateBuffer
//   - 行为函数指针：ConstructReplicationState / DestructReplicationState / CreateAndRegisterReplicationFragmentFunction
//   - 元信息：Traits（位掩码）、DescriptorIdentifier（CityHash64 稳定 ID）
//
// 【EReplicationStateTraits 17 位完整含义】（详见下文 enum 注释）
//   InitOnly / HasLifetimeConditionals / HasObjectReference / NeedsRefCount /
//   HasRepNotifies / KeepPreviousState / HasDynamicState / IsSourceTriviallyConstructible /
//   IsSourceTriviallyDestructible / AllMembersAreReplicated / IsFastArrayReplicationState /
//   IsNativeFastArrayReplicationState / HasConnectionSpecificSerialization /
//   HasPushBasedDirtiness / HasFullPushBasedDirtiness / SupportsDeltaCompression /
//   UseSerializerIsEqual / IsDerivedFromStructWithCustomSerializer / IsStructWithCustomSerializer
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/TypeHash.h"
#include <atomic>

struct FNetSerializerConfig;
class FProperty;
class FString;
namespace UE::Net
{
	struct FNetDebugName;
	struct FNetSerializer;
	typedef uint64 FRepTag;     // RepTag = uint64 标签 ID（用于 NetCullDistance/RoleGroup 等元属性查询）
	struct FReplicationStateDescriptor;
	class FFragmentRegistrationContext;

	class FReplicationFragment;
	// 【CreateAndRegisterReplicationFragmentFunc】可选函数指针：当 Struct 想绑定一个
	// 自定义 FReplicationFragment（例如 FastArray 的 FFastArrayReplicationFragment）
	// 时，由此入口创建并注册到 FFragmentRegistrationContext。
	// 注：官方文档明确"highly discouraged"——能用默认 PropertyReplicationFragment 就别用此 hook。
	typedef FReplicationFragment* (*CreateAndRegisterReplicationFragmentFunc)(UObject* Owner, const FReplicationStateDescriptor* Descriptor, FFragmentRegistrationContext& Context);
}

namespace UE::Net
{

// MemberDescriptors - Offset to where to find member data in both external and internal representation
// 【FReplicationStateMemberDescriptor】每个 Member 的双向偏移：
//   - ExternalMemberOffset：在 ExternalState（用户态）缓冲中的偏移；
//     对类属性 = FProperty::GetOffset_ForGC()；
//     对手写 State = offsetof(StateClass, MemberName)。
//   - InternalMemberOffset：在 InternalState（量化）缓冲中的偏移，用于序列化；
//     由 NetSerializer 的 InternalType 大小/对齐决定。
struct FReplicationStateMemberDescriptor
{
	uint32 ExternalMemberOffset;		// this is our offset in the external state	
	uint32 InternalMemberOffset;		// this is the internal offset
};

// MemberSerializer - What serializer and config to use for the given member
// 【FReplicationStateMemberSerializerDescriptor】绑定每个 Member 的序列化器。
//   - Serializer：FNetSerializer 静态实例（FFloatNetSerializer / FInt32NetSerializer / FStructNetSerializer 等）；
//   - SerializerConfig：序列化器需要的额外配置（Quantize 范围、enum 表大小等），
//     可能是序列化器自带 default config，也可能是 Builder 单独 malloc 的特化 config。
struct FReplicationStateMemberSerializerDescriptor
{
	const FNetSerializer* Serializer;
	const FNetSerializerConfig* SerializerConfig;
};

// 【EReplicationStateMemberTraits】单个 Member 的运行期 trait 位掩码（uint16）。
// 由 Builder 在烘焙时按 FProperty 类型/Serializer 能力推导得出，运行期被
// PropertyReplicationFragment / NetSerializer 用于走分支。
enum class EReplicationStateMemberTraits : uint16
{
	None = 0U,

	// 量化数据含动态分配（如 TArray、FString 内含 ptr+元素），需要在 Free 时显式 free。
	HasDynamicState = 1U << 0U,
	// 量化数据含 NetObjectReference，Iris 需要在 ApplyState 时做引用解析。
	HasObjectReference = HasDynamicState << 1U,
	// 序列化时需要"每连接特化"信息（如视角主控、控制器筛选）。
	HasConnectionSpecificSerialization = HasObjectReference << 1U,
	// RepNotify 即使值不变也总是触发（支持"事件式"通知）。
	HasRepNotifyAlways = HasConnectionSpecificSerialization << 1U,
	// 比较"是否相等"时使用 Serializer 自带的 IsEqual 而不是默认 memcmp。
	UseSerializerIsEqual = HasRepNotifyAlways << 1U,
	// 该成员属于 push-based 模型（无需每帧 poll，更新由 MARK_PROPERTY_DIRTY 显式触发）。
	HasPushBasedDirtiness = UseSerializerIsEqual << 1U,
};
ENUM_CLASS_FLAGS(EReplicationStateMemberTraits);

// 【FReplicationStateMemberTraitsDescriptor】薄包装，每个 Member 一项 trait 掩码。
struct FReplicationStateMemberTraitsDescriptor
{
	EReplicationStateMemberTraits Traits;
};

// 【FReplicationStateMemberTagDescriptor】RepTag 索引——把"语义化标签"（FRepTag）
// 映射到具体 MemberIndex，便于运行期快速查询（如 NetCullDistanceSquared）。
//   - InnerTagIndex：当 tag 来自嵌套 struct 时记录 inner tag 表里的位置；~0 表示无嵌套。
struct FReplicationStateMemberTagDescriptor
{
	FRepTag Tag;
	// Which member in the replication state
	uint16 MemberIndex;
	// When we propagate tags from a struct in a replication state we need to know how to get additional tag information. ~0 means invalid.
	uint16 InnerTagIndex;
};

// 【FReplicationStateMemberFunctionDescriptor】与 State 关联的 UFunction（多用于 RPC）：
//   - Function：UFunction 反射对象；
//   - Descriptor：UFunction 参数列表对应的 ReplicationStateDescriptor（参数本身也是一组属性）。
struct FReplicationStateMemberFunctionDescriptor
{
	const UFunction* Function;
	const FReplicationStateDescriptor* Descriptor;
};

// MemberChangeMaskDescriptor - Data needed for tracking of dirty members
// 【FReplicationStateMemberChangeMaskDescriptor】每成员在 ChangeMask 中占用的 bit 段。
//   - 普通成员：BitCount = 1；
//   - TArray 成员（启用 net.Iris.UseChangeMaskForTArray=1）：BitCount = 1 + N（主位 + N 元素位）；
//   - FastArray Item：根据 max element 数量分配。
struct FReplicationStateMemberChangeMaskDescriptor
{
	uint16 BitOffset;					// Offset in the change mask where we store the bits for the member
	uint16 BitCount;					// BitCount, In most cases we have one dirty bit per member but we will support assigning more bits for specific member in order to support dirty member tracking for arrays etc.
};

// 【FNetReferenceInfo】描述一个 NetObjectReference 的解析策略。
// Iris 区分 4 种解析行为：
//   - Invalid           ：无效占位（通常用于动态内存中"非引用区域"）；
//   - ResolveOnClient   ：默认行为，客户端尝试解析；解析失败可周期重试；
//   - MustExistOnClient ：服务端在客户端 ack 该引用对象前不复制本对象；
//   - ResolveOnlyWhenRecvd：单次解析；接收时若解析失败置 nullptr 不再重试。
struct FNetReferenceInfo
{
	enum EResolveType : uint8
	{
		Invalid = 0U,					// Invalid, used when encoding references contained in dynamic memory, we identify them by setting the ResolveType to Invalid
		ResolveOnClient,				// This reference should be resolved on the client - Default behavior
		MustExistOnClient,				// This reference must be acknowledged by client before server replicates object with reference
		ResolveOnlyWhenRecvd,			// This reference will only be resolved if it exists when we receive the data, if it is unresolvable client will set it to nullptr and not try to resolve it again until it gets replicated the next time
	};

	FNetReferenceInfo() : ResolveType(Invalid), Padding(0) {}
	explicit FNetReferenceInfo(FNetReferenceInfo::EResolveType InResolveType) : ResolveType(InResolveType), Padding(0) {}

	EResolveType ResolveType;
	uint8 Padding;
};

// We store information about all members that store references to other objects in order to allow us to quickly iterate over all references
// 【FReplicationStateMemberReferenceDescriptor】对象引用快速迭代表。Iris 把所有引用
// 单独建表，避免每次回收/解析都遍历整个 Descriptor。
//   - Offset           ：若引用在 StateBuffer 中 → 直接偏移；若在动态分配的 TArray 内 → 指向数组成员（再嵌套）；
//   - Info             ：解析策略；
//   - InnerReferenceIndex：嵌套引用（struct 内部 ref）的内部表索引；~0 表示非嵌套。
struct FReplicationStateMemberReferenceDescriptor
{
	uint32 Offset;				// If data is in an dynamic array this contains offset to ArrayMember, if ref is in the StateBuffer the offset is to the reference itself

	FNetReferenceInfo Info;		// Actual info about the reference

	uint16 MemberIndex;			// Member index for this reference
	uint16 InnerReferenceIndex;	// if set to something else than ~0 this means that this is a nested reference
};

// Per member debug descriptor, not strictly needed as long as we have the property list
// 【FReplicationStateMemberDebugDescriptor】调试用成员名（NetTrace / 日志输出时使用）。
// 在 Class 路径下若存在 MemberProperties 则可由 Property->GetName() 得出，此表是双重保险。
struct FReplicationStateMemberDebugDescriptor
{
	const FNetDebugName* DebugName;
};

// 【FReplicationStateIdentifier】Descriptor 的稳定身份标识（跨进程/服务端客户端一致）：
//   - Value           ：CityHash64(StateName 或 PathName)。是主键，用于 ReplicationProtocolManager 去重。
//   - DefaultStateHash：CityHash64(序列化后的默认状态)，用于 delta-compression 基线/版本校验；
//                       目前不参与 ==/< 比较（"我们可能将其设为可选"）。
struct FReplicationStateIdentifier
{
	uint64 Value; // Currently this is a CityHash of the name of the state
	uint64 DefaultStateHash; // Currently this is a CityHash of the serialized default state

	// We currently do not include the DefaultStateHash when comparing FReplicationStateIdentifier as we might make it optional
	bool operator==(const FReplicationStateIdentifier& Other)const { return Value == Other.Value; }
	bool operator<(const FReplicationStateIdentifier& Other)const { return Value < Other.Value; }
	bool operator!=(const FReplicationStateIdentifier& Other)const { return Value != Other.Value; }
};

inline uint32 GetTypeHash(const FReplicationStateIdentifier& Identifier)
{
	return ::GetTypeHash(Identifier.Value);
}

// 【FReplicationStateMemberLifetimeConditionDescriptor】每个 Member 的
// ELifetimeCondition（COND_None / COND_OwnerOnly / COND_SkipOwner / ...）压缩成 int8。
// 仅当 Descriptor 带有 HasLifetimeConditionals trait 时本表非空。
struct FReplicationStateMemberLifetimeConditionDescriptor
{
	// This is a more compact storage form of ELifetimeCondition
	int8 Condition;
};

/**
 *  To be able to enable/disable custom conditional properties we need a fast way to go from RepIndex to MemberIndex.
 *
 *  【FReplicationStateMemberRepIndexToMemberIndexDescriptor】RepIndex → MemberIndex 反查表。
 *  - RepIndex 是 UPROPERTY 在 UClass 内的全局索引（来自 LifetimeProperties）；
 *  - MemberIndex 是该属性在本 Descriptor 内的 0-based 索引；
 *  - 当游戏代码用 SetReplicationCondition(RepIndex, ...) 启停某属性时，需要 O(1) 反查
 *    其在 Descriptor 中的位置 → 进而操作 ConditionalChangeMask 对应位。
 *  - InvalidEntry = 65535 表示该 RepIndex 在本 Descriptor 中无映射（非本 State 的属性）。
 */
struct FReplicationStateMemberRepIndexToMemberIndexDescriptor
{
	enum : uint16
	{
		InvalidEntry = 65535U,
	};

	// MemberIndex whose property RepIndex matches the index of this entry. ~0 if this entry is invalid.
	uint16 MemberIndex;
};

// Additional data required to call RepNotifies etc
// 【FReplicationStateMemberPropertyDescriptor】仅 Class 路径需要的 RepNotify 元数据：
//   - RepNotifyFunction：旧的 OnRep_Xxx 回调 UFunction；nullptr 表示无 RepNotify；
//   - ArrayIndex       ：当属性是 TArray 元素时为元素下标，否则 0。
struct FReplicationStateMemberPropertyDescriptor
{
	const UFunction* RepNotifyFunction;
	uint16 ArrayIndex;
};

//
// Behavior / Feature Traits 
//
// 【EReplicationStateTraits】Descriptor 整体行为/能力位（uint32）。
// 由 FReplicationStateDescriptorBuilder::BuildReplicationStateTraits 在烘焙末尾汇总。
// 各位含义：
//   - InitOnly                       : 仅 Init 阶段下发一次（首次复制），无 ChangeMask；
//   - HasLifetimeConditionals        : 含 ELifetimeCondition 的属性 → 需要 ConditionalChangeMask + 反查表；
//   - HasObjectReference             : 含至少一个 ObjectReference → 需要走引用收集/解析路径；
//   - NeedsRefCount                  : 启用 atomic 引用计数（非静态 Descriptor 必须）；
//   - HasRepNotifies                 : 含至少一个 RepNotify → CallRepNotifies 阶段需要遍历；
//   - KeepPreviousState              : 接收侧需保留前一帧 state 用于 RepNotify 比较；
//   - HasDynamicState                : 量化数据含动态分配 → Free 时需走 FreeDynamicState；
//   - IsSourceTriviallyConstructible : External 源类型可 trivially construct（构造函数为 no-op）；
//   - IsSourceTriviallyDestructible  : External 源类型可 trivially destruct；
//   - AllMembersAreReplicated        : 该 struct 的全部 UPROPERTY 都被复制 → 可整块拷贝/比较；
//   - IsFastArrayReplicationState    : 描述符对应一个 FastArray (FFastArraySerializer 派生)；
//   - IsNativeFastArrayReplicationState : 进一步用 Iris 原生 FastArray (FIrisFastArraySerializer)；
//   - HasConnectionSpecificSerialization: 含每连接特化序列化的成员（如 NetRole）；
//   - HasPushBasedDirtiness          : 至少有一个成员是 push-based；
//   - HasFullPushBasedDirtiness      : 全部成员都是 push-based（可完全跳过 poll 阶段）；
//   - SupportsDeltaCompression       : 全部成员的 NetSerializer 都支持 delta；
//   - UseSerializerIsEqual           : 比较时走 Serializer::IsEqual（自定义比较）；
//   - IsDerivedFromStructWithCustomSerializer: 自身没自定义 NetSerializer，但父 Struct 有 → 走父 Struct 序列化；
//   - IsStructWithCustomSerializer   : 该 Struct 注册了自定义 NetSerializer。
enum class EReplicationStateTraits : uint32
{
	None								= 0U,
	InitOnly							= 1U,
	// LifetimeConditionals is backward compatibility with EReplicationCondition.
	HasLifetimeConditionals				= InitOnly << 1U,
	HasObjectReference					= HasLifetimeConditionals << 1U,
	NeedsRefCount						= HasObjectReference << 1U,
	HasRepNotifies						= NeedsRefCount << 1U,
	KeepPreviousState					= HasRepNotifies << 1U,
	HasDynamicState						= KeepPreviousState << 1U,
	IsSourceTriviallyConstructible		= HasDynamicState << 1U,
	IsSourceTriviallyDestructible		= IsSourceTriviallyConstructible << 1U,
	AllMembersAreReplicated				= IsSourceTriviallyDestructible << 1U,
	IsFastArrayReplicationState			= AllMembersAreReplicated << 1U,
	IsNativeFastArrayReplicationState	= IsFastArrayReplicationState << 1U,
	HasConnectionSpecificSerialization	= IsNativeFastArrayReplicationState << 1U,
	HasPushBasedDirtiness				= HasConnectionSpecificSerialization << 1U,
	HasFullPushBasedDirtiness			= HasPushBasedDirtiness << 1U,
	// Whether delta compression is supported or not
	SupportsDeltaCompression			= HasFullPushBasedDirtiness << 1U,
	UseSerializerIsEqual				= SupportsDeltaCompression << 1U,
	// Whether this is a descriptor for a struct derived from something with a NetSerializer, but doesn't have a custom NetSerializer itself.
	IsDerivedFromStructWithCustomSerializer	= UseSerializerIsEqual << 1U,
	IsStructWithCustomSerializer = IsDerivedFromStructWithCustomSerializer << 1U,
};
ENUM_CLASS_FLAGS(EReplicationStateTraits);

// Required functions to construct and destruct external state in the provided buffer
// 【ConstructReplicationStateFunc / DestructReplicationStateFunc】
//   外部状态构造/析构函数指针。Iris 在分配好 ExternalSize 字节缓冲后调用 Construct
//   完成 placement new；Free 时调用 Destruct。手写状态由 IRIS_IMPLEMENT_CONSTRUCT_AND_DESTRUCT 生成；
//   反射 Class/Struct 由 Builder 通过 InternalPropertyReplicationState::Construct/Destruct 完成。
typedef void (*ConstructReplicationStateFunc)(uint8* Buffer, const FReplicationStateDescriptor* Descriptor);
typedef void (*DestructReplicationStateFunc)(uint8* Buffer, const FReplicationStateDescriptor* Descriptor);

// A ReplicationState is our replication primitive, all members of a ReplicationState has the same high level conditional,
// i.e. connection level, IsInit, and has the same owner
// filtering on the block level might be Initial, Connection, Owner 
// Within a ReplicationState we do we also support per member conditionals
//
// 【FReplicationStateDescriptor】Iris 复制原语的元描述符。
//
// 【设计契约】
//   一个 ReplicationState 内的所有成员共享相同的"高层条件"：
//     - 是否 Init（首次一次性下发） / 同一 Owner / 同一 Connection 级别筛选。
//   高层条件用于 Block 级过滤（Initial / Connection / Owner）。在此之上还支持
//   per-member conditionals（通过 LifetimeConditionDescriptor）。
//
// 【字段顺序非常重要】此结构体的字段顺序对应 IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR
// 宏中初始化器的排列；任何字段重排都会导致手写 Descriptor 失效。
struct FReplicationStateDescriptor
{
	// RefCounting required for runtime created descriptors
	// 【AddRef / Release】仅当 NeedsRefCount trait 启用时实际生效。
	//   - Release：当引用归零时，依次：Free 默认状态缓冲（如 HasDynamicState 还需先 FreeDynamicState）；
	//     析构 NeedDestruction 的 SerializerConfig；Release 关联的 FunctionDescriptor；
	//     最后 Free 整块 Descriptor 内存（Builder 单次 malloc 出来的）。
	IRISCORE_API void AddRef() const;
	IRISCORE_API void Release() const;

	// 是否为 Init State（仅在首次复制下发）。Init State 没有 ChangeMask。
	bool IsInitState() const { return EnumHasAnyFlags(Traits, EReplicationStateTraits::InitOnly); }
	// 是否包含 ObjectReference。决定是否需要走引用收集/解析。
	bool HasObjectReference() const { return EnumHasAnyFlags(Traits, EReplicationStateTraits::HasObjectReference); }

	// 【三块 Mask 在 ExternalState 中的偏移】
	//   - ChangeMask 偏移 = ChangeMasksExternalOffset；
	//   - ConditionalChangeMask 紧跟 ChangeMask 后（ChangeMask 字节大小按 32-bit word 向上取整）；
	//   - PollMask 紧跟 ConditionalChangeMask 后；若无 LifetimeConditionals 则跳过条件块。
	uint32 GetChangeMaskOffset() const { return ChangeMasksExternalOffset; }
	uint32 GetConditionalChangeMaskOffset() const { return ChangeMasksExternalOffset + 4U*((ChangeMaskBitCount + 31)/32); }
	uint32 GetMemberPollMaskOffset() const { return GetConditionalChangeMaskOffset() + (EnumHasAnyFlags(Traits, EReplicationStateTraits::HasLifetimeConditionals) ? (4U*((ChangeMaskBitCount + 31)/32)) : 0U); }

	// ===== 11 个成员数组（按 MemberIndex 平行索引）=====
	// MemberCount 项：每个 Member 在 External/Internal 状态缓冲中的偏移
	const FReplicationStateMemberDescriptor* MemberDescriptors;
	// MemberCount 项：每个 Member 在 ChangeMask 中占用的 bit 段
	const FReplicationStateMemberChangeMaskDescriptor* MemberChangeMaskDescriptors;
	// MemberCount 项：每个 Member 使用哪个 NetSerializer + Config
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors;
	// MemberCount 项：每个 Member 的运行期 trait（HasObjectReference 等）
	const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors;
	// FunctionCount 项（可为 nullptr）：与 State 关联的 RPC 函数
	const FReplicationStateMemberFunctionDescriptor* MemberFunctionDescriptors;
	// TagCount 项（可为 nullptr）：FRepTag 索引
	const FReplicationStateMemberTagDescriptor* MemberTagDescriptors;

	// ObjectReferenceCount 项（可为 nullptr）：对象引用快速迭代表
	const FReplicationStateMemberReferenceDescriptor* MemberReferenceDescriptors;

	// This should possibly be moved to its own external descriptor as we do not want to rely on UProperties if we can.
	// Currently we need this since we do not know anything about the external types.
	// 【MemberProperties】MemberCount 项 FProperty*。仅 Class/Struct 路径有效（手写 State 为 nullptr）。
	// 长期目标是去掉对 UProperty 的运行时依赖，但目前 PropertyReplicationFragment 还需要它来
	// 走 Poll/Push/RepNotify 的反射读写。
	const FProperty** MemberProperties;

	// Additional data associated with applying state data for properties
	// We keep this in a separate array as we might only need this if we can receive data
	// 【MemberPropertyDescriptors】仅"接收侧"需要的属性辅助信息（RepNotify 函数、ArrayIndex）。
	// 服务端发送侧也允许 nullptr，节省内存。
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors;

	// Non-null if trait HasLifetimeConditionals is set.
	// 仅 HasLifetimeConditionals 启用时非空：MemberCount 项 ELifetimeCondition。
	const FReplicationStateMemberLifetimeConditionDescriptor* MemberLifetimeConditionDescriptors;

	// Non-null if RepIndexCount > 0
	// RepIndex → MemberIndex 反查表。表长 = RepIndexCount（不一定等于 MemberCount，
	// 因为 RepIndex 在整个 UClass 范围内编号，可能存在空洞）。
	const FReplicationStateMemberRepIndexToMemberIndexDescriptor* MemberRepIndexToMemberIndexDescriptors;

	// Non-null for derived struct descriptors.
	// 当本 Descriptor 是某 Struct 的"派生 Descriptor"（Struct 自身无自定义 NetSerializer 但父类有）
	// 时，BaseStruct 指向真正提供序列化逻辑的父 UScriptStruct。
	const UScriptStruct* BaseStruct;

	// Optional debug info
	const FNetDebugName* DebugName;
	// MemberCount 项调试名（在 NetTrace / Iris log 中使用）。
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors;

	// ===== 大小 / 对齐 =====
	uint32 ExternalSize;		// Size of the external representation including alignment
	uint32 InternalSize;		// Size of the internal representation including alignment
	uint16 ExternalAlignment;
	uint16 InternalAlignment;

	// ===== 计数 =====
	uint16 MemberCount;
	uint16 FunctionCount;
		
	uint16 TagCount;
	uint16 ObjectReferenceCount;

	// How many RepIndex to MemberIndex entries there are.
	// RepIndex 反查表长度。等于 UClass 内 LifetimeProperties 的最大 RepIndex + 1。
	uint16 RepIndexCount;

	// How many bits do we need for our tracking of dirty changes
	// ChangeMask 总位数：所有 Member 的 BitCount 累加。
	uint16 ChangeMaskBitCount;

	// This is the offset to where we store data for ChangeMask, ConditionalChangeMask and MemberPollMask in the external state.
	// Retrieve offset with helper methods, GetChangeMaskOffset(), GetConditionalChangeMaskOffset(), GetMemberPollMaskOffset()
	// 【ChangeMasksExternalOffset】三块 Mask 在 ExternalState 中的起始偏移。
	// 物理布局：[Header][Member 数据区...][ChangeMask][ConditionalChangeMask][PollMask]。
	// 三块 Mask 紧凑相邻，便于一次 Reset/Copy。
	uint32 ChangeMasksExternalOffset;

	// We need to assign a unique key that is stable between server and client (name hash of class + state type for now)
	// 稳定 ID（CityHash64）—— 用于 ProtocolManager 跨连接共享去重，并用于服务端/客户端协议比对。
	FReplicationStateIdentifier DescriptorIdentifier;

	// Function to construct external state representation in a preallocated buffer
	ConstructReplicationStateFunc ConstructReplicationState;

	// Function to destruct external state representation
	DestructReplicationStateFunc DestructReplicationState;

	// Function used to construct custom replication fragments
	// 自定义 ReplicationFragment 创建钩子（FastArray、自定义 fragment struct 使用）；
	// 默认 nullptr 时走 PropertyReplicationFragment（标准路径）。
	CreateAndRegisterReplicationFragmentFunc CreateAndRegisterReplicationFragmentFunction;

	// 整体 trait 位掩码（详见 EReplicationStateTraits 注释）。
	EReplicationStateTraits Traits;

	// 引用计数（mutable，因为 const Descriptor 也可 AddRef/Release）。仅 NeedsRefCount 启用时使用。
	// 0 表示无活动引用 —— Release 至 0 时整块释放 Descriptor 内存。
	mutable std::atomic<int32> RefCount;

	// Pointer to default state buffer, must be explicitly destroyed if set since it might contain dynamic data
	// 【DefaultStateBuffer】指向默认状态的"内部"量化缓冲（用于 Compare with default、
	// CityHash 计算 DefaultStateHash、delta compression 基线）。可能含动态分配，
	// Release 时若 HasDynamicState 需先 FreeDynamicState 再 Free 整块。
	const uint8* DefaultStateBuffer;
};

// 【DescribeReplicationDescriptor】把 Descriptor 摘要打印到字符串（调试/日志用）。
// 输出：identifier / member 数量 / external+internal 大小+对齐 / 每成员的偏移和 ChangeMask bit。
IRISCORE_API void DescribeReplicationDescriptor(FString& OutString, const FReplicationStateDescriptor* Descriptor);

}

