// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetSerializerConfig.h"
#include <limits>

/**
 * ====================================================================
 *  NetSerializer.h —— Iris 序列化子系统的"契约根文件"（模块灵魂）
 * ====================================================================
 *
 * 本文件定义了 Iris 所有具体 Serializer（Int/Float/Vector/Object/Struct/...）
 * 必须遵守的统一接口：一个"函数指针表" FNetSerializer，加上配套的：
 *   - FNetSerializerBaseArgs + 12 个 FNet*Args 参数包
 *   - ENetSerializerTraits 特性枚举
 *   - TNetSerializer<Impl>      —— 把任意"符合约定"的 Impl struct 萃取出函数指针表
 *   - TNetSerializerBuilder<>   —— SFINAE 探测器（见 NetSerializerBuilder.inl）
 *   - UE_NET_DECLARE_SERIALIZER / UE_NET_IMPLEMENT_SERIALIZER / UE_NET_GET_SERIALIZER 宏族
 *
 * 为什么采用"函数指针表"而非"虚函数/接口类"？
 *  1. 热路径性能：Quantize/Serialize 会被每帧为每个复制对象调用一次或多次。
 *     虚函数调用要查 vtable（额外 load + 间接跳转），对 CPU 分支预测不友好。
 *     函数指针表可以**一次查表**后直接 call，而且所有函数指针与 Config 指针一起
 *     存放在紧凑的连续内存里，缓存局部性远超虚函数。
 *  2. 没有继承链的开销：具体 Impl struct 不必带 vtable 指针（sizeof 更小，QuantizedType 可以是小 POD）。
 *  3. 编译期特化：TNetSerializerBuilder 通过 SFINAE 探测"有哪些成员"，缺失的函数由 default 版替代，
 *     空函数指针（如 CollectNetReferences）在运行时用 nullptr 判断就能跳过。
 *  4. 跨模块/跨插件的稳定 ABI：FNetSerializer 是纯 POD 式结构，宏展开后的 FXxxNetSerializerInfo 外壳
 *     仅含一个静态成员（Serializer），便于导出/导入。
 *
 * 与 UObject 世界的衔接：
 *  - Config 是 USTRUCT，便于 UHT/编辑器/蓝图识别；
 *  - Impl 本身不必是 UObject/USTRUCT，只是一个符合约定的"traits struct"。
 *
 * 扩展新 Serializer 的最小步骤（详见下方注释掉的示例）：
 *   1. 声明 FXxxNetSerializerConfig : FNetSerializerConfig（USTRUCT）
 *   2. 写 Impl struct：Version + SourceType/QuantizedType/ConfigType + 至少 Serialize/Deserialize
 *   3. UE_NET_DECLARE_SERIALIZER（头文件）+ UE_NET_IMPLEMENT_SERIALIZER（.cpp）
 *   4. 通过 FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry() 注册到系统
 */

/**
 * A FNetSerializer is needed for replication support of a certain type. Most types that can be 
 * marked as UPROPERTY are already supported. Types that aren't supported or need special support
 * will emit warnings when descriptors are built for a UCLASS, USTRUCT or UFUNCTION.
 * Find below how to implement one.
 */

#if 0
// Example.h
/** Always declare a serializer specific config for versioning reasons. */
USTRUCT()
struct FExampleNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

UE_NET_DECLARE_SERIALIZER(FFullExampleNetSerializer, EXAMPLE_API);

// Example.cpp
struct FExampleNetSerializer
{
	/** Version is required. */
	static constexpr uint32 Version = 0;

	// The various traits are optional and need only be specified if different from the defaults listed below.

	/** Specify when you want to make sure you implement all possible functions as you intend to forward calls to at least one other serializer. */
	static constexpr bool bIsForwardingSerializer = false;
	/**
	 * Specify when connection specific serialization is needed. Avoid it!
	 * @see ENetSerializerTraits
	 */
	static constexpr bool bHasConnectionSpecificSerialization = false;
	/** Specify when a CollectNetReferences implementation is needed. */
	static constexpr bool bHasCustomNetReference = false;
	/** Specify when the serializer requires dynamic state. Requires implementing CloneDynamicState and FreeDynamicState. */
	static constexpr bool bHasDynamicState = false;
	/** Set to false when a same value delta compression method is undesirable, for example when the serializer only writes a single bit for the state. */
	static constexpr bool bUseDefaultDelta = true;

	/**
	 * A typedef for the SourceType is required. Needed in order to calculate external state
	 * size and alignment and provide default implementations of some functions.
	 */
	typedef FSomeSourceType SourceType;

	/**
	 * A typedef for the QuantizedType is optional unless the SourceType isn't POD. Assumed to be SourceType if not specified.
	 * The QuantizedType needs to be POD.
	 */
	typedef FSomePodType QuantizedType;

	/** A typedef for the ConfigType is required. */
	typedef FNetSerializerConfig ConfigType;

	/** DefaultConfig is optional but highly recommended as the serializer can then be used without requiring special configuration setup. */
	inline static const ConfigType DefaultConfig;


	/** Required. Serialize is responsible for writing the quantized data to a bit stream provided by the serialization context. */
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	/** Required. Deserialize is responsible for reading the quantized data from a bit stream provided by the serialization context. */
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	/**
	 * Optional. Same as Serialize but where an acked previous state is provided for bitpacking purposes.
	 * This is implemented by default to do same value optimization, at the cost of a bit. If implemented
	 * then DeserializeDelta is required.
	 */
	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&);

	/**
	 * Optional. Same as Deserialize but where an acked previous state is provided for bitpacking purposes.
	 * This is implemented by default to do same value optimization, at the cost of a bit. If implemented
	 * then SerializeDelta is required.
	 */
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);

	/**
	 * Optional, unless there's a QuantizedType typedef or Dequantize implementation.
	 * Transforms potentially non-POD source data to POD form which can be serialized quickly.
	 * Quantization is only performed at most once per object and frame and is an excellent opportunity to perform slightly heavier computations in order
	 * to make the serialization quicker.
	 */
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&);

	/**
	 * Optional, unless there's a QuantizedType typedef or Quantize implementation. 
	 * The dequantize function is responsible for transforming the quantized state to a 
	 * valid source data form of the state, approximately equal to the original source data that was quantized.
	 */
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&);

	/** Optional. Determine whether data is equal to other data or not. The default implementation will use the equality operator. */
	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);

	/** Optional. Validate that the data fulfills serializer specific requirements. The default implementation will return true. */
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs&);

	/** Required if bHasDynamicState is true. Clones the quantized data from one state buffer to another. */
	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);

	/** Required if bHasDynamicState is true. Frees dynamic allocations in the state buffer and resets the data to default state. */
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	/** Required if bHasCustomNetReference is true. Add object references to a collector. */
	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

	/** Serializers that want to be selective about which members to modify in the target instance when applying state should implement Apply where the serializer is responsible for setting the members of the target instance. The function operates on non-quantized state. */
	static void Apply(FNetSerializationContext&, const FNetApplyArgs&);
};
UE_NET_IMPLEMENT_SERIALIZER(FExampleNetSerializer);

/**
 * How to properly implement the actual functions can be seen in various NetSerializer implementations,
 * such as FFloatNetSerializer to name one.
 */

#endif

namespace UE::Net
{
	class FNetBitArrayView;
	class FNetMemoryContext;
}

namespace UE::Net
{

/** 对象指针类型（UPTRINT）：任意量化/源数据的通用"不透明指针"。在 Args 中避免绑死具体类型。 */
typedef UPTRINT NetSerializerValuePointer;
/** Config 指针类型：指向 Serializer 对应的 const FXxxNetSerializerConfig 实例。 */
typedef const FNetSerializerConfig* NetSerializerConfigParam;

/**
 * FNetSerializerChangeMaskParam —— 描述成员在 ChangeMask 中占用的比特区间。
 *  - BitOffset：成员 ChangeMask 起始位（相对于对象 ChangeMask buffer）；
 *  - BitCount：成员占用的位数。对普通成员是 1；对 TArray/FastArray 可能是多位（元素级 ChangeMask）。
 * Serializer 可通过这些信息实现"只序列化脏位对应的内容"。
 */
struct FNetSerializerChangeMaskParam
{
	/** Offset in the change mask where we store the bits for the member. */
	uint32 BitOffset = 0;
	/** Number of bits used for the member. This is typically 1, except for special cases like arrays. */
	uint32 BitCount = 0;
};

/**
 * FNetSerializerBaseArgs —— 所有 FNet*Args 的公共基类，每次调用 Serializer 函数都必带。
 *  - NetSerializerConfig：对应 Serializer 的 Config 实例（每种 Serializer 子类的 Config 可能不同）；
 *  - ChangeMaskInfo：成员在 ChangeMask 中的位置（若有）；
 *  - Version：Serializer 版本号，便于跨版本兼容（当前并未全路径完全传递，仅占位）。
 */
/**
 * Things that need to be passed to most functions that are part of a NetSerializer.
 * Along with the function arguments there's often a FNetSerializationContext as well.
 */
struct FNetSerializerBaseArgs
{
	/** The serializer's config. */
	NetSerializerConfigParam NetSerializerConfig = 0;
	/** Change mask info if available. */
	FNetSerializerChangeMaskParam ChangeMaskInfo;
	/** The Version of the NetSerializer. Currently not properly propagated. */
	uint32 Version = 0;
};

/**
 * FNetCollectReferencesArgs —— 调用 CollectNetReferences 时的参数。
 *  - Source：指向量化后的数据；
 *  - Collector：指向 FNetReferenceCollector，Serializer 把自身持有的 UObject 引用追加到 Collector。
 * 仅当 Impl 的 bHasCustomNetReference = true 时被调用。
 * 典型用途：判定引用是否可解析（远程端已存在）、延迟发送依赖对象。
 */
/**
 * Parameters passed to a NetSerializer's CollectNetReferences function.
 * CollectNetReferences is typically called to determine whether all references
 * can be properly resolved or not.
 */
struct FNetCollectReferencesArgs : FNetSerializerBaseArgs
{
	/** A pointer to the quantized data. */
	NetSerializerValuePointer Source;
	/** A pointer to a FNetReferenceCollector. */
	NetSerializerValuePointer Collector;
};
/** CollectNetReferences 的函数指针类型：指向 static 函数。 */
typedef void(*NetCollectNetReferencesFunction)(FNetSerializationContext&, const FNetCollectReferencesArgs&);

/**
 * FNetSerializeArgs —— Serialize（写位流）的参数。
 *  - Source：指向"量化后"的数据（即经过 Quantize 的 POD，通常也是 ChangeMask 覆盖的存储）。
 * 写入对象：Context.GetBitStreamWriter()。
 */
/**
 * Parameters passed to a NetSerializer's Serialize function.
 * Serialize is called to write quantized data to a bit stream.
 */
struct FNetSerializeArgs : FNetSerializerBaseArgs
{
	/** A pointer to the quantized data. */
	NetSerializerValuePointer Source;
};
typedef void(*NetSerializeFunction)(FNetSerializationContext&, const FNetSerializeArgs&);

/**
 * FNetDeserializeArgs —— Deserialize（读位流）的参数。
 *  - Target：指向"量化数据"目标 buffer，Deserialize 负责把读到的内容写到这里。
 * 读取对象：Context.GetBitStreamReader()。
 */
/**
 * Parameters passed to a NetSerializer's Deserialize function.
 * Deserialize is called to read quantized data from a bit stream.
 */
struct FNetDeserializeArgs : FNetSerializerBaseArgs
{
	NetSerializerValuePointer Target;
};
typedef void(*NetDeserializeFunction)(FNetSerializationContext&, const FNetDeserializeArgs&);

/**
 * FNetSerializeDeltaArgs —— SerializeDelta（带"上一次 ack 状态"的增量写入）的参数。
 *  - 继承自 FNetSerializeArgs，添加 Prev 指向上一次已确认的量化状态；
 *  - Serializer 可对比 Prev 与 Source，做"只写差异位"的优化。
 * 默认实现：先写 1 位 IsEqual，若相等则不写数据；否则退化到完整 Serialize。
 */
/**
 * Parameters passed to a NetSerializer's SerializeDelta function.
 * SerializeDelta is called to write quantized data to a bit stream.
 * As a pointer to a previously acked quantized state is provided the
 * serializer can use bit packing for example to reduce the number of
 * bits to be serialized.
 */
struct FNetSerializeDeltaArgs : FNetSerializeArgs
{
	/** A pointer to acked quantized data, which can be used for bit packing. */
	NetSerializerValuePointer Prev;
};
typedef void(*NetSerializeDeltaFunction)(FNetSerializationContext&, const FNetSerializeDeltaArgs&);

/**
 * FNetDeserializeDeltaArgs —— 与 SerializeDelta 对偶。
 *  - Prev：上一次已确认的量化状态（发送端也用的是同一份）。
 *
 * @note DeserializeDelta 即使判断"与 Prev 相同"也必须把结果写入 Target（memcpy 过来），
 *       不能让 Target 保持未定义——这是接收端保证状态一致性的硬性要求。
 */
/**
 * Parameters passed to a NetSerializer's DeserializeDelta function.
 * DeserializeDelta is responsible to read the data produced by SerializeDelta.
 * @see FNetSerializeDeltaArgs
 * @note DeserializeDelta must always store the deserialized delta in the target memory,
 * even if it's determined that the data is the same as the quantized data passed
 * in the Prev pointer.
 */
struct FNetDeserializeDeltaArgs : FNetDeserializeArgs
{
	/** A pointer to quantized data which was used by the SerializeDelta call on the sending side. */
	NetSerializerValuePointer Prev;
};
typedef void(*NetDeserializeDeltaFunction)(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);

/**
 * FNetQuantizeArgs —— Quantize 阶段（每帧每对象最多一次）的参数。
 *  - Source：外部原始数据（非 POD 也可以）；
 *  - Target：量化状态 buffer，要求 POD + 位确定性（bit-deterministic，相同输入必得相同量化结果）。
 *
 * 为什么要单独 Quantize：
 *  - Quantize 可以承担较重的计算（sin/cos/归一化等），每帧只做一次；
 *  - Serialize 每连接调用一次，必须是"拿 POD 直接写位流"的极快路径；
 *  - 量化状态可以跨连接共享，避免 N 倍的计算。
 *
 * 动态状态（bHasDynamicState）：
 *  - 容器类 Serializer（TArray/Map）的 QuantizedType 通常固定大小（存指针 + 长度），
 *    实际元素 buffer 由 FNetSerializerArrayStorage 动态分配。
 *  - 必须实现 CloneDynamicState/FreeDynamicState；否则 copy/destruction 时会泄漏或 double-free。
 *
 * @warning 量化 buffer 内部的 padding bytes 必须清零——否则 memcmp 比较会假不等，
 *          Iris 通常通过 zero-initialize 整个 buffer 保证（也是为什么 "zero quantized state" 必须是合法状态）。
 */
/**
 * Parameters passed to a NetSerializer's Quantize function.
 * The purpose of the Quantize function is to transform the original source data
 * to a POD state. Quantized state buffers are passed to memcpy and similar functions. Apart from
 * required to being POD it must also be bit deterministic. 
 * As state buffers are initialized to zero before first use a quantized state of zero
 * should represent a valid state which the serializer's other functions can operate on.
 * There is an option to allow dynamic state which can be used by serializers operating on
 * container types to minimize the footprint of the quantized state, rather than always having 
 * a fixed buffer that can handle the maximum number of elements in the container for example.
 * For dynamic state additional functions need to be implemented; CloneDynamicState and FreeDynamicState.
 * @warning Beware of padding in your quantized state as creating it on the stack for example
 *          may cause non-determinstic state.
 */
struct FNetQuantizeArgs : FNetSerializerBaseArgs
{
	/** A pointer to the non-quantized source data. */
	NetSerializerValuePointer Source;
	/** A pointer to the quantized state buffer which contains valid, but unknown, quantized state. */
	NetSerializerValuePointer Target;
};
typedef void(*NetQuantizeFunction)(FNetSerializationContext&, const FNetQuantizeArgs&);

/**
 * FNetDequantizeArgs —— Dequantize（接收端把量化状态还原回业务数据）的参数。
 *  - Source：量化状态；
 *  - Target：写入原始数据的 buffer（通常是游戏对象的属性地址）。
 *
 * Dequantize 的结果通常与原值"近似相等"——量化可能损失精度（压缩 Vector/Float 时）。
 */
/**
 * Parameters passed to a NetSerializer's Dequantize function.
 * A Dequantize function must be provided if there's a Quantize function.
 * The dequantize function is responsible for transforming the quantized state
 * to a valid source data form of the state, approximately equal to the original
 * source data that was quantized.
 */
struct FNetDequantizeArgs : FNetSerializerBaseArgs
{
	/** A pointer to a valid quantized state buffer. */
	NetSerializerValuePointer Source;
	/** A pointer to the source data buffer which contains valid, but unknown, source data. */
	NetSerializerValuePointer Target;
};
typedef void(*NetDequantizeFunction)(FNetSerializationContext&, const FNetDequantizeArgs&);

/**
 * FNetIsEqualArgs —— IsEqual 的参数。
 *  - Source0/Source1：两个待比较的数据指针；
 *  - bStateIsQuantized：标识指针指向量化数据（true）还是原始数据（false）。
 *
 * 为什么要同时支持量化/非量化：
 *  - 发送端对比"当前原始值 vs 上次量化状态"时走非量化路径；
 *  - DeltaCompression 基线比较走量化路径（避免重复 Quantize）。
 *
 * 默认实现：调用 SourceType::operator==。若 Serializer 需要特殊相等语义（例如浮点 epsilon），可覆写。
 */
/**
 * Parameters passed to a NetSerializer's IsEqual function.
 * IsEqual is used to check whether data is network equal, that is if
 * the quantized forms of the data are equal. IsEqual needs to work for both
 * source data and quantized data.
 */
struct FNetIsEqualArgs : FNetSerializerBaseArgs
{
	/** Source data or quantized data. */
	NetSerializerValuePointer Source0;
	/** Source data or quantized data to compare with. */
	NetSerializerValuePointer Source1;
	/** Whether the data pointed to is source or quantized form. */
	bool bStateIsQuantized;
};
typedef bool(*NetIsEqualFunction)(FNetSerializationContext&, const FNetIsEqualArgs&);

/**
 * FNetValidateArgs —— Validate 的参数。
 *  - Source：原始业务数据（非量化）。
 *
 * 用途：在 Quantize 前做"可发送性"校验：枚举值是否在枚举范围、数组长度是否超限、字符串是否非法等。
 * 默认实现返回 true。Validate 失败会让该属性被跳过或报错（具体取决于上层策略）。
 */
/**
 * Parameters passed to a NetSerializer's Validate function.
 * Validate is used to determine whether the source data is correct
 * or not. An enum serializer could validate the the value is support by the enum
 * for example. An array serializer could validate that the array doesn't have more
 * number of elements than some limit.
 */
struct FNetValidateArgs : FNetSerializerBaseArgs
{
	/** A pointer to the non-quantized source data. */
	NetSerializerValuePointer Source;
};
typedef bool(*NetValidateFunction)(FNetSerializationContext&, const FNetValidateArgs&);

/**
 * FNetCloneDynamicStateArgs —— 深拷贝动态状态。
 *  - Source：原量化状态；
 *  - Target：目标 buffer（调用时内容未定义，由 Clone 完全覆盖）。
 *
 * 典型实现：先对 Target 做浅拷贝（memcpy 过 Source），再对内嵌的 FNetSerializerArrayStorage 调用 Clone，
 * 让动态分配的元素 buffer 被深拷贝一份。
 * 内存分配必须走 Context.GetInternalContext()->Alloc/Realloc/Free（不能直接 FMemory::Malloc）。
 */
/**
 * Forwarding serializers and serializers in need of dynamic state must implement
 * CloneDynamicState and FreeDynamicState. Forwarding serializers should only forward
 * calls to forwarding members and members with dynamic state.
 * CloneDynamicState will get undefined memory contents in the target state. It's up
 * to the clone function to deep copy the source quantized state and overwrite the
 * target state. Allocation of memory needs to be done via FNetSerializationContext.
 * @see FNetSerializationContext
*/
struct FNetCloneDynamicStateArgs : FNetSerializerBaseArgs
{
	/** A pointer to the quantized source data. */
	NetSerializerValuePointer Source;
	/** A pointer to the target data which should be overwritten with a deep copy of the quantized source data. */
	NetSerializerValuePointer Target;
};
typedef void(*NetCloneDynamicStateFunction)(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);

/**
 * FNetFreeDynamicStateArgs —— 释放动态状态。
 *  - Source：量化状态 buffer。Free 后调用方通常把这部分 memset 为 0，使其再次成为"零初始化合法状态"。
 *
 * 必须可重入：同一个 buffer 被 Free 多次不能崩（第二次为 no-op）。
 * 推荐做法：释放后立即把指针/长度字段清零，让"已 Free 的状态"与"零初始化状态"等价。
 */
/**
 * Forwarding serializers and serializers in need of dynamic state must implement
 * CloneDynamicState and FreeDynamicState. Forwarding serializers should only forward
 * calls to forwarding members and members with dynamic state.
 * FreeDynamicState must be re-entrant. To achieve this it's recommended to clear the quantized state
 * after freeing dynamically allocated memory. Freeing of memory needs to be done via FNetSerializationContext.
 * @see FNetSerializationContext
 */
struct FNetFreeDynamicStateArgs : FNetSerializerBaseArgs
{
	/** A pointer to the quantized source data. */
	NetSerializerValuePointer Source;
};
typedef void(*NetFreeDynamicStateFunction)(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

/**
 * FNetApplyArgs —— Apply（有选择地把反量化结果写入目标实例）的参数。
 *  - Source：非量化源数据（刚从 Dequantize 输出）；
 *  - Target：目标对象实例。
 *
 * 默认行为（没有 Apply）：整体覆盖所有成员。
 * 自定义 Apply 的场景：只同步部分成员的 Struct，例如 FTransform 只复制 Location 而保留 Target 的 Rotation。
 * Apply 操作于非量化数据，便于访问 UObject 指针、字符串等业务类型。
 */
/** Serializers that want to be selective about which members to modify in the target instance when applying state should implement Apply where the serializer is responsible for setting the members of the target instance. The function operates on non-quantized state. */
struct FNetApplyArgs : FNetSerializerBaseArgs
{
	/** A pointer to the non-quantized source data. */
	NetSerializerValuePointer Source;
	/** A pointer to the non-quantized target data. */
	NetSerializerValuePointer Target;
};
typedef void(*NetApplyFunction)(FNetSerializationContext&, const FNetApplyArgs&);

/**
 * ENetSerializerTraits —— Serializer 的运行期行为标志（存放在 FNetSerializer::Traits）。
 *
 * 每一位的含义：
 *  - IsForwardingSerializer：
 *      该 Serializer 只是"转发器"，自己不做实际序列化，而是委托给内部的其他 Serializer。
 *      此时必须实现所有 12 个可能的函数（哪怕只是 forward 调用），因为转发者无从得知被转发对象用到哪些函数。
 *      典型：FStructNetSerializer 转发到每个成员自己的 Serializer。
 *
 *  - HasDynamicState：
 *      Serializer 的量化状态含动态分配的内存。必须实现 CloneDynamicState / FreeDynamicState。
 *      复制/销毁量化状态时不能只 memcpy，需要走对应函数做深拷贝/释放。
 *      典型：FArrayPropertyNetSerializer、FStringNetSerializer。
 *
 *  - HasConnectionSpecificSerialization：
 *      同一份数据对不同连接需要不同序列化（如含玩家可见性过滤的引用）。
 *      **应尽力避免**——此标志会让 Iris 无法在多播/跨连接之间共享序列化 buffer，
 *      例如 RPC 广播时必须对每个目标连接单独序列化一份，CPU 与内存成本翻倍。
 *      典型：少数含 PlayerController 相关引用的 Serializer。
 *
 *  - HasCustomNetReference：
 *      Serializer 的量化状态里含自定义的 UObject 引用（不能通过默认反射扫描找到），
 *      必须实现 CollectNetReferences，让依赖解析/导出流水线能发现这些引用。
 *      典型：FObjectNetSerializer、FSoftObjectNetSerializer。
 *
 *  - UseSerializerIsEqual：
 *      复制系统在判断数据是否"变脏"时优先用 Serializer 的 IsEqual 而非默认 operator==。
 *      用于需要"网络等价性"（如浮点 epsilon 比较）而非"严格等价"的场景。
 *
 *  - HasApply：
 *      Serializer 实现了 Apply 函数，应用状态时调用 Apply 而非直接覆盖。
 */
/**
 * Various traits that can be set for a FNetSerializer.
 * These traits are typically set via constexpr bool in the declaration of the serializer.
 * Only publicly visible traits are part of the enum.
 */
enum class ENetSerializerTraits : uint32
{
	None = 0U,
	/** Forwarding serializers need to implement all functions that a serializer may have. */
	IsForwardingSerializer = 1U << 0U,
	/** Serializers in need of dynamic state must implement CloneDynamicState and FreeDynamicState. */
	HasDynamicState = IsForwardingSerializer << 1U,
	/**
	 * Connection specific serialization is sometimes required but should be avoided by all means necessary
	 * as it prevents sharing of serialized state. For example when multicasting RPCs and one of the arguments
	 * is using connection specific serialization it requires serializing the RPC for each connection and possibly
	 * allocating memory for duplicating the data for each connection. It wastes both CPU and memory.
	 */
	HasConnectionSpecificSerialization = HasDynamicState << 1U,
	/** There are net references that need to be gathered via calls to CollectNetReferences. */
	HasCustomNetReference = HasConnectionSpecificSerialization << 1U,

	/** Data replicated using this serializer should use the IsEqual implementation in order to determine whether the data is dirty or not. */
	UseSerializerIsEqual = HasCustomNetReference << 1U,

	/** Has an Apply function which should be used when applying its dequantized data to another instance. Useful for custom struct serializers where not all of the struct properties are replicated. Without a custom Apply all values will be overwritten. */
	HasApply = UseSerializerIsEqual << 1U,
};
ENUM_CLASS_FLAGS(ENetSerializerTraits);

/**
 * FNetSerializer —— **Iris 序列化的契约根结构体**。
 * 每个具体 Serializer 在编译期被 TNetSerializer<Impl>::ConstructNetSerializer 构造为一个 constexpr 实例，
 * 存放于 FXxxNetSerializerInfo::Serializer（由 UE_NET_IMPLEMENT_SERIALIZER 宏生成）。
 *
 * 字段约定：
 *   - Version：Serializer 版本号（来自 Impl::Version）。
 *   - Traits：ENetSerializerTraits 的按位组合。
 *
 *   — 12 个函数指针，对应 12 个 FNet*Args：
 *     - Serialize / Deserialize：必填。
 *     - SerializeDelta / DeserializeDelta：可选；缺失时 Builder 填充 default 实现。
 *     - Quantize / Dequantize：SourceType 非 POD 或存在 QuantizedType 时必填，否则 default = memcpy。
 *     - IsEqual：默认 = operator==。
 *     - Validate：默认 = 恒真。
 *     - CloneDynamicState / FreeDynamicState：仅当 bHasDynamicState 或 bIsForwardingSerializer 时必填。
 *     - CollectNetReferences：仅当 bHasCustomNetReference 时必填。
 *     - Apply：可选；没有时默认不调用。
 *
 *   - DefaultConfig：可选默认 Config 实例指针（Impl::DefaultConfig 的地址）。
 *   - QuantizedTypeSize / Alignment：量化状态 buffer 的大小与对齐（编译期算出后收窄到 uint16，避免占用过多空间）。
 *   - ConfigTypeSize / Alignment：Config 结构体的大小与对齐。
 *   - Name：Serializer 名称（调试 / 日志用），来自 TEXT(#SerializerName)。
 *
 * 运行期访问：用户通过 UE_NET_GET_SERIALIZER(FXxxNetSerializer) 取到 const FNetSerializer&。
 */
/**
 * The end result of a UE_NET_IMPLEMENT_SERIALIZER call on a struct adhering to 
 * the conventions of the TNetSerializerBuilder. If direct access to a serializer
 * is needed for some reason use UE_NET_GET_SERIALIZER.
 */
struct FNetSerializer
{
	uint32 Version;                                       // Impl::Version，见上
	ENetSerializerTraits Traits;                          // 运行期 trait 位图

	NetSerializeFunction Serialize;                       // 必填
	NetDeserializeFunction Deserialize;                   // 必填
	NetSerializeDeltaFunction SerializeDelta;             // 默认 = "1 位 IsEqual + 否则退化为 Serialize"
	NetDeserializeDeltaFunction DeserializeDelta;         // 与 SerializeDelta 对偶
	NetQuantizeFunction Quantize;                         // 默认 = SourceType 的浅拷贝（POD 时）
	NetDequantizeFunction Dequantize;                     // 默认 = SourceType 的浅拷贝
	NetIsEqualFunction IsEqual;                           // 默认 = operator==
	NetValidateFunction Validate;                         // 默认 = 恒真
	NetCloneDynamicStateFunction CloneDynamicState;       // 默认 = nullptr（仅 HasDynamicState/Forwarding 必填）
	NetFreeDynamicStateFunction FreeDynamicState;         // 默认 = nullptr
	NetCollectNetReferencesFunction CollectNetReferences; // 默认 = nullptr（仅 HasCustomNetReference 必填）
	NetApplyFunction Apply;                               // 默认 = nullptr（无则整体覆盖）
	const FNetSerializerConfig* DefaultConfig;            // 指向 Impl::DefaultConfig，可能为 nullptr
	uint16 QuantizedTypeSize;                             // sizeof(QuantizedType) 或 sizeof(SourceType)
	uint16 QuantizedTypeAlignment;                        // alignof 同上
	uint16 ConfigTypeSize;                                // sizeof(ConfigType)
	uint16 ConfigTypeAlignment;                           // alignof(ConfigType)

	const TCHAR* Name;                                    // 调试用名字（编译期字面量）
};

}

#include "NetSerializerBuilder.inl"

namespace UE::Net
{

/**
 * TNetSerializer<NetSerializerImpl> —— 把符合约定的 Impl struct 转为 FNetSerializer 函数表的"类型萃取器"。
 *
 * 核心函数 ConstructNetSerializer：
 *  - constexpr 声明——所有内容都在编译期确定；
 *  - 内部借用 TNetSerializerBuilder<Impl>（NetSerializerBuilder.inl）执行 SFINAE 探测；
 *  - Builder.Validate()：static_assert 校验 Impl 的必备成员齐全；
 *  - 逐字段填充：Version / Traits / 12 个函数指针 / DefaultConfig / 类型大小对齐 / Name。
 *  - 运行期访问：UE_NET_GET_SERIALIZER(Name) → const FNetSerializer&。
 *
 * 收窄 size_t → uint16 的 static_assert：
 *  - FNetSerializer 里大小/对齐字段是 uint16（节省内存），必须在编译期确认不会溢出。
 *  - 若未来某个 Serializer 的 Config 或 QuantizedType 超过 65535 字节，这里会编译失败（罕见但可能）。
 */
template<typename NetSerializerImpl>
class TNetSerializer
{
public:
	/**
	 * 编译期构造一个 FNetSerializer 实例。Name 通过宏传入（TEXT(#SerializerName)）。
	 */
	static constexpr FNetSerializer ConstructNetSerializer(const TCHAR* Name)
	{
		TNetSerializerBuilder<NetSerializerImpl> Builder;
		Builder.Validate();                                             // static_assert 校验必备成员

		FNetSerializer Serializer = {};
		Serializer.Version = Builder.GetVersion();
		Serializer.Traits = Builder.GetTraits();

		// 12 个函数指针——缺失的成员由 Builder 用 default 实现或 nullptr 填充
		Serializer.Serialize = Builder.GetSerializeFunction();
		Serializer.Deserialize = Builder.GetDeserializeFunction();
		Serializer.SerializeDelta = Builder.GetSerializeDeltaFunction();
		Serializer.DeserializeDelta = Builder.GetDeserializeDeltaFunction();
		Serializer.Quantize = Builder.GetQuantizeFunction();
		Serializer.Dequantize = Builder.GetDequantizeFunction();
		Serializer.IsEqual = Builder.GetIsEqualFunction();
		Serializer.Validate = Builder.GetValidateFunction();
		Serializer.CloneDynamicState = Builder.GetCloneDynamicStateFunction();
		Serializer.FreeDynamicState = Builder.GetFreeDynamicStateFunction();
		Serializer.CollectNetReferences = Builder.GetCollectNetReferencesFunction();
		Serializer.Apply = Builder.GetApplyFunction();

		Serializer.DefaultConfig = Builder.GetDefaultConfig();

		// 下面 4 个 static_assert 把 size_t/uint32 收窄到 uint16，保证 FNetSerializer 的紧凑布局
		static_assert(Builder.GetQuantizedTypeSize() <= std::numeric_limits<uint16>::max(), "");
		Serializer.QuantizedTypeSize = static_cast<uint16>(Builder.GetQuantizedTypeSize());
		static_assert(Builder.GetQuantizedTypeAlignment() <= std::numeric_limits<uint16>::max(), "");
		Serializer.QuantizedTypeAlignment = static_cast<uint16>(Builder.GetQuantizedTypeAlignment());

		static_assert(Builder.GetConfigTypeSize() <= std::numeric_limits<uint16>::max(), "");
		Serializer.ConfigTypeSize = static_cast<uint16>(Builder.GetConfigTypeSize());
		static_assert(Builder.GetConfigTypeAlignment() <= std::numeric_limits<uint16>::max(), "");
		Serializer.ConfigTypeAlignment = static_cast<uint16>(Builder.GetConfigTypeAlignment());

		Serializer.Name = Name;
		return Serializer;
	}
};

}

/**
 * UE_NET_DECLARE_SERIALIZER(Name, Api)
 *
 * 在头文件里声明一个 Serializer 外壳结构体 FXxxNetSerializerInfo：
 *   struct Api FXxxNetSerializerInfo
 *   {
 *       static const UE::Net::FNetSerializer Serializer;   // 契约根实例（函数指针表）
 *       static uint32 GetQuantizedTypeSize();
 *       static uint32 GetQuantizedTypeAlignment();
 *       static const FNetSerializerConfig* GetDefaultConfig();
 *   };
 *
 * - `Api` 是 DLL 导出宏（如 `ENGINE_API`、`MYMODULE_API`），用于跨 DLL 访问。
 * - 为何需要"信息结构体"而非直接暴露 FNetSerializer：
 *   * 让 GetQuantizedTypeSize / Alignment / DefaultConfig 作为非 constexpr 的函数暴露给运行期（其他模块代码），
 *     避免依赖方必须包含完整 Impl 定义；
 *   * 让 UE_NET_GET_SERIALIZER 可以在仅包含头文件的情况下取到引用。
 * - 信息结构体的存在也使"前向声明 + 实现分离"成为可能：Impl 结构体只在 .cpp 中定义。
 */
/** Declare a serializer. */
#define UE_NET_DECLARE_SERIALIZER(SerializerName, Api) struct Api SerializerName ## NetSerializerInfo  \
{ \
	static const UE::Net::FNetSerializer Serializer; \
	static uint32 GetQuantizedTypeSize(); \
	static uint32 GetQuantizedTypeAlignment(); \
	static const FNetSerializerConfig* GetDefaultConfig(); \
};

/**
 * UE_NET_IMPLEMENT_SERIALIZER(SerializerName)
 *
 * 在 .cpp 中定义 UE_NET_DECLARE_SERIALIZER 声明的所有成员：
 *   - `Serializer` 通过 TNetSerializer<SerializerName>::ConstructNetSerializer(TEXT(#SerializerName)) 编译期构造；
 *   - Get* 成员函数走 TNetSerializerBuilder<SerializerName> 拿信息。
 *
 * 展开后：
 *   const UE::Net::FNetSerializer FXxxNetSerializerInfo::Serializer = ...::ConstructNetSerializer(TEXT("FXxxNetSerializer"));
 *   uint32 FXxxNetSerializerInfo::GetQuantizedTypeSize() { return ...; }
 *   uint32 FXxxNetSerializerInfo::GetQuantizedTypeAlignment() { return ...; }
 *   const FNetSerializerConfig* FXxxNetSerializerInfo::GetDefaultConfig() { return ...; }
 *
 * 一个 Serializer = 一对 DECLARE + IMPLEMENT 宏 + 一个 Impl struct。
 */
/** Implement a serializer using the struct named SerializerName. */
#define UE_NET_IMPLEMENT_SERIALIZER(SerializerName) const UE::Net::FNetSerializer SerializerName ## NetSerializerInfo::Serializer = UE::Net::TNetSerializer<SerializerName>::ConstructNetSerializer(TEXT(#SerializerName)); \
	uint32 SerializerName ## NetSerializerInfo::GetQuantizedTypeSize() { return UE::Net::TNetSerializerBuilder<SerializerName>::GetQuantizedTypeSize(); }; \
	uint32 SerializerName ## NetSerializerInfo::GetQuantizedTypeAlignment() { return UE::Net::TNetSerializerBuilder<SerializerName>::GetQuantizedTypeAlignment(); }; \
	const FNetSerializerConfig* SerializerName ## NetSerializerInfo::GetDefaultConfig() { return UE::Net::TNetSerializerBuilder<SerializerName>::GetDefaultConfig(); };

/** Retrieve a const reference to a named serializer.
 *  获取某个 Serializer 的 const 引用——Iris 对外暴露的唯一"直接拿 Serializer"入口。
 *  典型用法：`const FNetSerializer& S = UE_NET_GET_SERIALIZER(FFloatNetSerializer);`
 */
#define UE_NET_GET_SERIALIZER(SerializerName) static_cast<const UE::Net::FNetSerializer&>(SerializerName ## NetSerializerInfo::Serializer)
/** Retrieve the quantized state size for a serializer.
 *  获取量化状态大小（字节）。上层用来分配 per-object 的量化 buffer。 */
#define UE_NET_GET_SERIALIZER_INTERNAL_TYPE_SIZE(SerializerName) SerializerName ## NetSerializerInfo::GetQuantizedTypeSize()
/** Retrieve the quantized state alignment for a serializer.
 *  获取量化状态对齐要求。 */
#define UE_NET_GET_SERIALIZER_INTERNAL_TYPE_ALIGNMENT(SerializerName) SerializerName ## NetSerializerInfo::GetQuantizedTypeAlignment()
/** Retrieve the default config, if present, for a serializer.
 *  获取默认 Config 指针（可能为 nullptr）。 */
#define UE_NET_GET_SERIALIZER_DEFAULT_CONFIG(SerializerName) SerializerName ## NetSerializerInfo::GetDefaultConfig()
