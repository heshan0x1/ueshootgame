// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// InternalNetSerializers.h —— Iris 内部专用 NetSerializer 的 Config 类型定义
// -----------------------------------------------------------------------------
// 本头文件统一声明 5 个 "internal" Serializer 的 Config（USTRUCT，需要被 UE
// 反射系统识别，所以放在公开模块路径下，但通过 UE_NET_DECLARE_SERIALIZER_INTERNAL
// 宏标记为内部），以及它们的初始化辅助函数：
//   * FBitfieldNetSerializerConfig            —— 位域成员（uint8 BitMask）
//   * FArrayPropertyNetSerializerConfig       —— TArray 元素描述
//   * FLastResortPropertyNetSerializerConfig  —— 兜底 NetSerialize 桥接
//   * FNetRoleNetSerializerConfig             —— ENetRole 双字段（Role/RemoteRole）
//   * FFieldPathNetSerializerConfig           —— FFieldPath 路径
//
// 设计动机：
//   * 这些 Serializer 是"由 ReplicationStateDescriptorBuilder 在烘焙阶段根据
//     反射元信息自动选用"的，不希望成为对外公开的扩展点（用户应自己写
//     UE_NET_IMPLEMENT_NETSERIALIZER_INFO 注册），所以使用 INTERNAL 宏；
//   * 但 Config 仍然是 USTRUCT，因为 ReplicationStateDescriptor 默认状态缓冲
//     的烘焙、复制等都依赖反射。
// =============================================================================

#pragma once

#include "Iris/Serialization/InternalNetSerializer.h"
#include "Templates/RefCounting.h"
#include "InternalNetSerializers.generated.h"

namespace UE::Net
{
	// 前置声明：被 FArrayPropertyNetSerializerConfig 用作元素状态描述。
	struct FReplicationStateDescriptor;
}

// -----------------------------------------------------------------------------
// FBitfieldNetSerializerConfig —— 位域属性 Config
// -----------------------------------------------------------------------------
// UE 反射中的 bitfield bool 通常表现为 FBoolProperty + 字节内某一位。
// 该 Config 仅记录所在字节的 BitMask（单一位的 mask, 形如 0x04），
// Quantize/Dequantize 时用 mask 做位提取与回写。
USTRUCT()
struct FBitfieldNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	// 位域所在字节内的 mask（单位即 1 个比特）。0 表示未初始化。
	UPROPERTY()
	uint8 BitMask = 0;
};

// -----------------------------------------------------------------------------
// FArrayPropertyNetSerializerConfig —— TArray 属性 Config
// -----------------------------------------------------------------------------
// 数组型属性（`TArray<T>`）在 Iris 中通过单层间接走 ArrayPropertyNetSerializer：
//   * 数据：用元素 ReplicationStateDescriptor 描述每个元素的 layout/serializer；
//   * 长度：存储为 uint16 + 上限 / 位宽预编码；
//   * 引用：通过 TFieldPath<FArrayProperty> 在反射阶段定位运行时 FProperty 以
//     便 ScriptArrayHelper 操作真实容器内存。
USTRUCT()
struct FArrayPropertyNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	// 构造时显式置位 ConfigTraits = NeedDestruction，使 Iris 在协议销毁时
	// 调用 ~FArrayPropertyNetSerializerConfig（用于释放 StateDescriptor 引用）。
	FArrayPropertyNetSerializerConfig();
	~FArrayPropertyNetSerializerConfig();

	// 禁止拷贝/移动 —— Config 内部持有 RefCount 智能指针 + TFieldPath，
	// 让 USTRUCT 默认拷贝路径绕过会破坏引用计数与 lifetime。
	FArrayPropertyNetSerializerConfig(const FArrayPropertyNetSerializerConfig&) = delete;
	FArrayPropertyNetSerializerConfig(FArrayPropertyNetSerializerConfig&&) = delete;
	
	FArrayPropertyNetSerializerConfig& operator=(const FArrayPropertyNetSerializerConfig&) = delete;
	FArrayPropertyNetSerializerConfig& operator=(FArrayPropertyNetSerializerConfig&&) = delete;

	// 数组允许的最大元素数 —— 反序列化校验时会拒收超过此值的报文。
	UPROPERTY()
	uint16 MaxElementCount = 0;

	// 元素数量的位宽（GetBitsNeeded(MaxElementCount)），用于按 bit-width 写读 NumElements。
	UPROPERTY()
	uint16 ElementCountBitCount = 0;

	// TFieldPath<FArrayProperty>：反射路径，用于在运行时获取真实 FArrayProperty*，
	// 进而通过 FScriptArrayHelper 操作真实 SourceType 内存（非量化态）。
	UPROPERTY()
	TFieldPath<FArrayProperty> Property;

	// 元素的 ReplicationStateDescriptor。元素 Layout（成员、Serializer、Trait）
	// 全在这一份 Descriptor 里。引用计数共享，避免重复烘焙。
	TRefCountPtr<const UE::Net::FReplicationStateDescriptor> StateDescriptor;
};

// 关闭 USTRUCT 默认拷贝（与 UPROPERTY 默认 ops 解耦），防止 RefCountPtr 浅拷贝越界。
template<>
struct TStructOpsTypeTraits<FArrayPropertyNetSerializerConfig> : public TStructOpsTypeTraitsBase2<FArrayPropertyNetSerializerConfig>
{
	enum
	{
		WithCopy = false
	};
};

// -----------------------------------------------------------------------------
// FLastResortPropertyNetSerializerConfig —— "兜底" NetSerialize 桥接 Config
// -----------------------------------------------------------------------------
// 任何无法匹配到具体 Serializer 的 FProperty 都会落到此处，逐字节调用
// 旧式 FProperty::NetSerializeItem(FArchive&) 接口完成（慢路径但兼容性强）。
//
// 限制（与文件原英文注释一致）：
//   - 不支持有意义的 delta compression（无法精确比较"含旧式 NetSerialize 内
//     部对象引用的字节流"）；
//   - 必须分配动态内存以保存量化态字节流（长度运行时才知道）。
/**
 * Any property that doesn't have any other option will end up using this.
 * As the name suggests it's a last resort.
 * - Cannot support delta compression in a meaningful way.
 * - Must allocate memory to store quantized state.
 */
USTRUCT()
struct FLastResortPropertyNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	// 量化态最大可承载的位数，默认 65535。超过此值视为非法报文（SetError）。
	static constexpr uint32 DefaultMaxQuantizedSizeBits = 65535;

	// 反射路径，运行时拿 FProperty* 来调它的 NetSerializeItem。
	UPROPERTY()
	TFieldPath<FProperty> Property;

	// true 表示此属性不参与 ReplicationProtocol 默认状态哈希计算。
	// 用于"内容会因连接而异"的属性（如随连接刷新的数据）。
	UPROPERTY()
	bool bExcludeFromDefaultStateHash = false;

	// 限制单次 Quantize 后的位数上限。超出会 ensure + 报 GNetError_ArraySizeTooLarge。
	UPROPERTY()
	uint32 MaxQuantizedSizeBits = DefaultMaxQuantizedSizeBits;
};

// -----------------------------------------------------------------------------
// FNetRoleNetSerializerConfig —— ENetRole（含远端 Role 自动交换）Config
// -----------------------------------------------------------------------------
// AActor 同时持有 Role 与 RemoteRole，两端意义对调：
//   * 服务器端的 Role（SimulatedProxy/Authority）发送到客户端时要变成 RemoteRole；
//   * 反之亦然。
// 因此 Iris 用一个独立的 NetRoleNetSerializer 在 Quantize/Dequantize 阶段
// 同步处理"另一端"的字段（依赖两个内/外偏移）。
USTRUCT()
struct FNetRoleNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	// 在量化（internal）状态布局中，"另一个 Role"相对当前成员的偏移
	// （单位：字节）。一般对应 Role <-> RemoteRole。
	UPROPERTY()
	int32 RelativeInternalOffsetToOtherRole = 0;

	// 同上，但是针对真实 SourceType（external，FActor 内存布局）的偏移。
	UPROPERTY()
	int32 RelativeExternalOffsetToOtherRole = 0;

	// ENetRole 的合法取值范围（uint8）。Validate 时用于校验。
	UPROPERTY()
	uint8 LowerBound = 0;
	UPROPERTY()
	uint8 UpperBound = 0;
	// 序列化所需位宽（如 [0,3] 区间需要 2 位）。
	UPROPERTY()
	uint8 BitCount = 0;

	// ENetRole::ROLE_AutonomousProxy 与 ROLE_SimulatedProxy 的具体枚举值，
	// 在"是否需要降级"判断中要用（DowngradeAutonomous 等）。
	UPROPERTY()
	uint8 AutonomousProxyValue = 0;
	UPROPERTY()
	uint8 SimulatedProxyValue = 0;

	// 反射的 ENetRole 枚举对象，运行时校验/调试用。非 UPROPERTY()。
	const UEnum* Enum = nullptr;
};

// -----------------------------------------------------------------------------
// FFieldPathNetSerializerConfig —— FFieldPath Config
// -----------------------------------------------------------------------------
// FFieldPath（指向某 FProperty 的反射路径）需要原样还原 Owner+逐段 FName
// path，便于运行时再 Resolve。Config 仅记录"该字段所在的 FProperty"用于调试。
USTRUCT()
struct FFieldPathNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TFieldPath<FProperty> Property;
};

// -----------------------------------------------------------------------------
// FFieldPathNetSerializerSerializationHelper —— FieldPath 量化中转结构
// -----------------------------------------------------------------------------
// FFieldPath 内部成员 protected，无法直接由 StructNetSerializer 烘焙。
// 因此 Iris 引入这个 Helper：把 Owner（弱引用 UStruct）+ FName 路径数组
// 拷贝到此中转 USTRUCT，再让 StructNetSerializer 走标准烘焙路径。
USTRUCT()
struct FFieldPathNetSerializerSerializationHelper
{
	GENERATED_BODY()

	// 弱引用所在的 UStruct（类/结构体），承载 path 的解析根节点。
	UPROPERTY();
	TWeakObjectPtr<UStruct> Owner;
	// 逐段 FName，对应 FProperty 在 Owner 中的嵌套路径。
	UPROPERTY();
	TArray<FName> PropertyPath;
};

namespace UE::Net
{

// 通过 INTERNAL 宏注册 5 个内部 Serializer 的全局 singleton。
// IRISCORE_API：跨模块（如 ReplicationState 烘焙器）需要拿 default Config。
UE_NET_DECLARE_SERIALIZER_INTERNAL(FBitfieldNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER_INTERNAL(FLastResortPropertyNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER_INTERNAL(FArrayPropertyNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER_INTERNAL(FNetRoleNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER_INTERNAL(FFieldPathNetSerializer, IRISCORE_API);

// 从 FBoolProperty 推算 Config.BitMask；位域字节地址在反射里隐含，
// 这里只需要把 mask 提出来。
IRISCORE_API bool InitBitfieldNetSerializerConfigFromProperty(FBitfieldNetSerializerConfig& OutConfig, const FBoolProperty* Bitfield);

// 把 FProperty 包装到 LastResortConfig.Property，让兜底 serializer 能调到原生 NetSerialize。
IRISCORE_API bool InitLastResortPropertyNetSerializerConfigFromProperty(FLastResortPropertyNetSerializerConfig& OutConfig, const FProperty* Property);

// 工具：从 ArrayPropertyNetSerializer 的量化态中取出"裸数据数组指针 + 元素数"，
// 提供给上层（如 FastArray 元素遍历、跨 array 引用收集）使用。
// 返回 ElementCount，OutArrayBuffer 为元素首地址。
uint32 GetNetArrayPropertyData(NetSerializerValuePointer QuantizedArray, NetSerializerValuePointer& OutArrayBuffer);

// 仅根据 ENetRole UEnum 填充范围/位宽相关字段；
// Internal/External offset 仍需在描述符烘焙时根据具体 layout 计算填入。
IRISCORE_API bool PartialInitNetRoleSerializerConfig(FNetRoleNetSerializerConfig& OutConfig, const UEnum* Enum);

}
