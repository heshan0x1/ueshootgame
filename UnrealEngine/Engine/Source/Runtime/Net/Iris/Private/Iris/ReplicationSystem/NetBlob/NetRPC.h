// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NetRPC.h —— Iris RPC 内部数据结构（FNetRPC）
// -----------------------------------------------------------------------------
// FNetRPC 是 Iris 中"一次 RPC 调用"的网络载体（继承 FNetObjectAttachment）。
// 序列化布局（精简，详见 .cpp）：
//   Header:        [SizeBitCount=24]    位长度（先占位，再回填）
//   ObjectRef:     若不带对象上下文则写完整 FNetObjectReference；
//                  SerializeWithObject 路径则只写 SubObject 引用
//   FunctionLocator: { DescriptorIndex(uint16), FunctionIndex(uint16) } —— 通过
//                  ReplicationProtocol 的状态描述符表 + 成员函数描述符表定位 UFunction
//                  Locator 用 nibble-count 变长编码（1~4 nibble × 2 字段）
//   Payload:       FunctionParameters 的 Quantize 结果（可含 reference exports）
//
// 关键设计：
//   * 走的是 NetObjectAttachment 通道 —— 与对象状态分离，可走 Normal / OOB / Huge
//     三种发送队列。
//   * 参数 Quantize 在 Create() 时一次性完成，存放在 QuantizedBlobState 内；发送时
//     再 Serialize（量化结果）写位流；接收侧 Deserialize → Dequantize → ProcessEvent。
//   * 引用收集在 Create() 时完成（OnlyCollectReferencesThatCanBeExported），存入
//     ReferencesToExport，发送时通过 GetNetObjectReferenceExports 暴露。
//   * MaxRpcSizeInBits = 2^24 - 1 （约 2MB）—— 单个 RPC 序列化大小硬限。
// =============================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "UObject/WeakObjectPtr.h"

namespace UE::Net
{
	class FNetRPCCallContext;
}

namespace UE::Net::Private
{

class FNetRPC final : public FNetObjectAttachment
{
public:
	// 中文：在对端定位 UFunction 的复合索引。
	//   Iris 的对象按 ReplicationProtocol 持有一组 ReplicationStateDescriptor，
	//   每个 Descriptor 内部又有 MemberFunctionDescriptors 数组。两个 16-bit 索引
	//   足够覆盖任何蓝图/原生 RPC 函数。
	struct FFunctionLocator
	{
		// Which ReplicationStateDescriptor in the Object's protocol the function information can be found
		// 中文：对象 ReplicationProtocol::ReplicationStateDescriptors 中的索引。
		uint16 DescriptorIndex;
		// Which index in to the MemberFunctionDescriptor array in the above ReplicationStateDescriptor contains the function information
		// 中文：上述 Descriptor 的 MemberFunctionDescriptors 数组中的索引。
		uint16 FunctionIndex;
	};

	FNetRPC(const FNetBlobCreationInfo& CreationInfo);

	// 中文：发送侧工厂——
	//   1) 解析 UFunction → FunctionLocator + FunctionDescriptor（含状态描述符）
	//   2) Quantize 函数参数到 QuantizedBlobState（dedicated server 上可选 Protect 内存以便 stomp 检测）
	//   3) 收集所有可导出对象引用 → ReferencesToExport，并在 Flags 上 OR HasExports
	//   4) 把 SuperFunction 链回溯到顶层（蓝图派生函数共用基类 locator）
	static FNetRPC* Create(UReplicationSystem* ReplicationSystem, const FNetBlobCreationInfo& CreationInfo, const FNetObjectReference& ObjectReference, const UFunction* Function, const void* FunctionParameters);

	// 中文：接收侧——执行 RPC。包含目标对象解析、方向校验、Dequantize、Forward
	// 委托广播以及最终的 UObject::ProcessEvent。
	void CallFunction(FNetRPCCallContext& Context);

	void SetFunctionLocator(const FFunctionLocator& InfFunctionLocator) { FunctionLocator = InfFunctionLocator; }
	const FFunctionLocator& GetFunctionLocator() const { return FunctionLocator; }

private:
	virtual ~FNetRPC();

	// 中文：导出"参数中包含的对象引用"，便于发送侧把它们登记到 export 列表，
	// 接收侧才能解析 ref 字段。
	virtual TArrayView<const FNetObjectReference> GetNetObjectReferenceExports() const override final;

	// 中文：以下 4 个 Serialize/Deserialize 是 NetBlob 框架虚接口的具体实现。
	//   * SerializeWithObject：上层连接已知 RefHandle（不需要再写源对象引用，只写 sub-object）
	//   * Serialize          ：上层不知道对象，需要写完整 FNetObjectReference
	//   流程：写 Header(占位) → 写 ObjectRef/SubObjectRef → 写 FunctionLocator
	//        → 写 Payload → 回填 Header 中的实际位长。
	virtual void SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const override;
	virtual void DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) override;

	virtual void Serialize(FNetSerializationContext& Context) const override;
	virtual void Deserialize(FNetSerializationContext& Context) override;

	// 中文：Header 写"整个 RPC 占用的位数"。第一次以 PayloadSize=-1 占位，
	// 全部内容写完后再用准确大小回填，便于接收端解析失败时跳过整段。
	void InternalSerializeHeader(FNetSerializationContext& Context, int32 PayloadSize=-1) const;
	uint32 InternalDeserializeHeader(FNetSerializationContext& Context);

	// 中文：FunctionLocator 用 1~4 nibble 变长写出（先 2bit 表示 nibble 数 - 1）。
	void SerializeFunctionLocator(FNetSerializationContext& Context) const;
	void DeserializeFunctionLocator(FNetSerializationContext& Context);

	// 中文：写完整对象引用（FNetObjectReference 序列化）。
	void InternalSerializeObjectReference(FNetSerializationContext& Context) const;
	void InternalDeserializeObjectReference(FNetSerializationContext& Context);

	// 中文：仅写 sub-object（已知 root RefHandle）。
	void InternalSerializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle) const;
	void InternalDeserializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle);

	// 中文：Payload —— Quantize 后的 FunctionParameters。委托给 FNetObjectAttachment::SerializeBlob。
	void InternalSerializeBlob(FNetSerializationContext& Context) const;
	void InternalDeserializeBlob(FNetSerializationContext& Context);

	// 中文：接收侧根据 FunctionLocator + ObjectRef 解析出 UFunction 与目标 UObject*；
	// 同时根据 UFunction 标志位修补 ENetBlobFlags（Reliable/Ordered）。
	bool ResolveFunctionAndObject(FNetSerializationContext& Context);

	// 中文：服务器端校验——RPC 来自的连接是否真的拥有目标对象（owning connection）。
	// 防止客户端代他人执行 RPC。
	bool IsServerAllowedToExecuteRPC(FNetSerializationContext& Context) const;

private:

	// 中文：Header 中保存 RPC 总位长所用的位数 = 24，对应最大 payload (2^24)-1 ≈ 16Mb。
	static constexpr uint32 HeaderSizeBitCount = 24U;
	static constexpr uint32 MaxRpcSizeInBits = (1U << HeaderSizeBitCount) - 1U;

private:
	// If we have exports we normally expect the number to be low
	// 中文：参数中可导出引用数量通常很少，inline 4 已足够；溢出走堆。
	typedef TArray<FNetObjectReference, TInlineAllocator<4>> FNetRPCExportsArray;

	FFunctionLocator FunctionLocator;                        // 函数定位器
	const UFunction* Function;                               // 解析后的目标 UFunction（只在收/发期持有）
	TWeakObjectPtr<UObject> ObjectPtr;                       // 解析得到的目标对象（弱引用避免延长生命周期）
	TUniquePtr<FNetRPCExportsArray> ReferencesToExport;      // 参数中需要导出的对象引用列表（发送侧 Create 时收集）
};

}
