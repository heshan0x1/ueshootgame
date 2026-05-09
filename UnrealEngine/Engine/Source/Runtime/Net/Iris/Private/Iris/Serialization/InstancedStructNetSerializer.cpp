// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// InstancedStructNetSerializer.cpp —— FInstancedStruct 序列化实现
// -----------------------------------------------------------------------------
// 关键流程（见每个函数注释）：
//   * Quantize：保存 (StructType=UObject 量化引用, StructData=嵌套 struct 量化态);
//   * Serialize：先写 1 bit 是否有效，无效则提前结束。否则写 type + data。
//   * Deserialize：读 type → ObjectNetSerializer.Dequantize 拿 UScriptStruct → 烘焙 Descriptor → 读 data。
//   * Apply：应用到客户端真实对象时若类型不变则保留非复制成员；否则 InitializeAs 重置。
//
// 重要细节：
//   * 类型 UScriptStruct 通过 FObjectNetSerializer 序列化 —— 走 NetGUID/PathExport，
//     因此跨进程也能正确解析；
//   * Descriptor 烘焙开销大，所以用 FInstancedStructDescriptorCache 缓存（默认 8 项 LRU）；
//   * SupportedTypes 当前作为"硬白名单"未启用（看 InitInstancedStructNetSerializerConfig 中
//     bIsAllowingArbitraryStruct == true，注释引用 UE-180981 待办）。
// =============================================================================

#include "Iris/Serialization/InstancedStructNetSerializer.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(InstancedStructNetSerializer)


#include "StructUtils/InstancedStruct.h"
#include "HAL/IConsoleManager.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/Serialization/NetSerializerArrayStorage.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/QuantizedObjectReference.h"
#include "Templates/IsPODType.h"

// 标记 NeedDestruction，使 RefCountPtr<Descriptor> + DescriptorCache 在销毁时释放。
FInstancedStructNetSerializerConfig::FInstancedStructNetSerializerConfig()
: FNetSerializerConfig()
{
	ConfigTraits = ENetSerializerConfigTraits::NeedDestruction;
}

FInstancedStructNetSerializerConfig::~FInstancedStructNetSerializerConfig() = default;

namespace UE::Net
{

// LRU 容量（CVar 可调）。<=0 视为无限缓存（用 Map）。
static int32 MaxCachedInstancedStructDescriptorCount = 8;
static FAutoConsoleVariableRef CVarMaxCachedInstancedStructDescriptors(TEXT("InstancedStruct.MaxCachedReplicationStateDescriptors"), MaxCachedInstancedStructDescriptorCount, TEXT("How many ReplicationStateDescriptors the InstancedStructNetSerializer is allowed to cache for InstancedStructs without a type allow list. Warning: A value <= 0 means an unlimited amount of descriptors."));

// 错误码：当反序列化端遇到未知/非法 struct 类型时上报。
const FName NetError_InstancedStructNetSerializer_InvalidStructType("Invalid struct type");

// 量化态的核心数据结构。
// 注意 StructName / StructDescriptorTraits 不被序列化（不参与比特流），仅作运行时优化。
struct FFInstancedStructNetSerializerQuantizedData
{
	// 嵌套 struct 的对齐量化 buffer（按 Descriptor->InternalSize/Alignment 动态分配）。
	FNetSerializerAlignedStorage StructData;
	// UScriptStruct 类型自身的量化对象引用（NetGUID/PathExport）。
	FQuantizedObjectReference StructType;

	// Not serialized. Fully qualified path. For ReplicationStateDescriptor lookup, validation etc. 
	// 完整 path FName，用于 DescriptorCache 查表（避免每次都 GetPathName）。
	FName StructName;
	// Not serialized. To optimize away some calls like dynamic memory management and object references.
	// 缓存元素 Descriptor 的 trait 子集；用于跳过 CloneDynamicState / CollectRefs 等无谓调用。
	EReplicationStateTraits StructDescriptorTraits;
};

// FInstancedStructPropertyNetSerializerInfo ——
// 把 "FInstancedStruct" 这个 named struct 注册到 PropertyNetSerializerInfoRegistry，
// 让 ReplicationStateDescriptorBuilder 在烘焙时给 FInstancedStruct 字段绑定本 serializer。
struct FInstancedStructPropertyNetSerializerInfo : public FNamedStructPropertyNetSerializerInfo
{
public:
	FInstancedStructPropertyNetSerializerInfo();

protected:
	// 强制每个 FInstancedStruct 属性使用属性专属 Config（用于属性级白名单/调试名）。
	virtual bool CanUseDefaultConfig(const FProperty* Property) const;
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override;
};

}

// QuantizedData 标记为 POD：FNetSerializerAlignedStorage / FQuantizedObjectReference 都不持有 vtable，
// 可以直接 memcpy 进/出 ReplicationProtocol 缓冲。
template<> struct TIsPODType<UE::Net::FFInstancedStructNetSerializerQuantizedData> { enum { Value = true }; };

namespace UE::Net
{

// 主 Serializer 定义。所有 12 个契约函数都显式声明 + 实现，因为 bIsForwardingSerializer=true。
struct FInstancedStructNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Traits
	// QuantizedType 内含动态分配（StructData）；
	// 类型可能引用对象（StructType），需要参与 CollectNetReferences；
	// 自身只是 dispatch，需 Apply 等所有契约 → forwarding。
	static constexpr bool bHasDynamicState = true;
	static constexpr bool bIsForwardingSerializer = true;
	static constexpr bool bHasCustomNetReference = true;

	typedef FInstancedStruct SourceType;
	typedef FFInstancedStructNetSerializerQuantizedData QuantizedType;
	typedef FInstancedStructNetSerializerConfig ConfigType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs&);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

	// 应用到客户端真实对象。Apply 是 Iris 特有的高层 API（不在 12 项契约中），
	// 用于把"反量化的临时态"合并/拷贝到真实游戏对象上，区别于 RepNotify。
	static void Apply(FNetSerializationContext&, const FNetApplyArgs&);

private:
	// Frees dynamic memory allocated by the struct instance. Zeros the struct storage. Does not free the struct storage. After the call the Value is ready to be re-purposed for a different type of struct.
	// 释放 struct 实例的 dynamic state（不 free StructData 本体），并 memzero —— 让 storage 可以重复利用给另一种类型。
	static void FreeStructInstance(FNetSerializationContext&, FInstancedStructNetSerializerConfig*, QuantizedType& Value);
	// Frees dynamic memory allocated by the struct instance, frees the storage for the struct instance and reset the entire quantized state to default.
	// 完全 Reset：含 StructData free、整个 QuantizedType 清零。
	static void Reset(FNetSerializationContext&, FInstancedStructNetSerializerConfig*, QuantizedType&);

	// 内部 helper，仅释放 struct 实例 dynamic state（递归调 StructNetSerializer.FreeDynamicState）。
	static void InternalFreeStructInstance(FNetSerializationContext&, FInstancedStructNetSerializerConfig*, QuantizedType&);

	// 注册委托：在 PreFreezeNetSerializerRegistry 阶段把 FInstancedStructPropertyNetSerializerInfo
	// 注册到全局 PropertyNetSerializerInfoRegistry。
	class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
	{
	public:
		virtual ~FNetSerializerRegistryDelegates()
		{
			UE_NET_UNREGISTER_NETSERIALIZER_INFO(FInstancedStructPropertyNetSerializerInfo);
		}

	private:
		virtual void OnPreFreezeNetSerializerRegistry() override
		{
			UE_NET_REGISTER_NETSERIALIZER_INFO(FInstancedStructPropertyNetSerializerInfo);
		}

		UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FInstancedStructPropertyNetSerializerInfo);
	};

	// 缓存指向其他通用 serializer 的指针，避免每次调用都 UE_NET_GET_SERIALIZER。
	inline static const FNetSerializer* StructNetSerializer = &UE_NET_GET_SERIALIZER(FStructNetSerializer);
	inline static const FNetSerializer* ObjectNetSerializer = &UE_NET_GET_SERIALIZER(FObjectNetSerializer);
	inline static FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;
};

UE_NET_IMPLEMENT_SERIALIZER(FInstancedStructNetSerializer);

// -----------------------------------------------------------------------------
// Serialize —— 写"是否有效 + (类型 + 数据)"
// -----------------------------------------------------------------------------
// 报文：[1 bit valid] ([type 引用][struct 数据])?
void FInstancedStructNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);

	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	FStructNetSerializerConfig StructConfig;
	if (!Value.StructName.IsNone())
	{
		StructConfig.StateDescriptor = Config->DescriptorCache.FindOrAddDescriptor(Value.StructName);
		ensureMsgf(StructConfig.StateDescriptor.IsValid(), TEXT("Struct type is no longer resolvable: %s. Sending FInstancedStruct as uninitialized."), ToCStr(Value.StructName.ToString()));
	}

	if (Writer->WriteBool(StructConfig.StateDescriptor.IsValid()))
	{
		// Serialize struct type
		{
			FNetSerializeArgs SerializeArgs = Args;
			SerializeArgs.Source = NetSerializerValuePointer(&Value.StructType);
			SerializeArgs.NetSerializerConfig = ObjectNetSerializer->DefaultConfig;
			ObjectNetSerializer->Serialize(Context, SerializeArgs);
		}

		// Serialize struct data
		{
			FNetSerializeArgs SerializeArgs = Args;
			SerializeArgs.Source = NetSerializerValuePointer(Value.StructData.GetData());
			SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
			StructNetSerializer->Serialize(Context, SerializeArgs);
		}
	}
}

// -----------------------------------------------------------------------------
// Deserialize —— 读"是否有效 + (类型 → 烘焙 Descriptor → 数据)"
// -----------------------------------------------------------------------------
void FInstancedStructNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	// Was the instanced struct valid on the sending side?
	if (Reader->ReadBool())
	{
		FQuantizedObjectReference StructType;
		const UScriptStruct* Struct = nullptr;

		// Deserialize struct type
		{
			FNetDeserializeArgs DeserializeArgs = Args;
			DeserializeArgs.Target = NetSerializerValuePointer(&StructType);
			DeserializeArgs.NetSerializerConfig = ObjectNetSerializer->DefaultConfig;
			ObjectNetSerializer->Deserialize(Context, DeserializeArgs);
		}

		// Dequantize to get the UScriptStruct.
		// $IRIS TODO : Allow receiving end to skip payloads which it's unable to parse due to missing struct.
		{
			UObject* Object = nullptr;

			FNetDequantizeArgs DequantizeArgs = {};
			DequantizeArgs.Source = NetSerializerValuePointer(&StructType);
			DequantizeArgs.Target = NetSerializerValuePointer(&Object);
			DequantizeArgs.NetSerializerConfig = ObjectNetSerializer->DefaultConfig;
			ObjectNetSerializer->Dequantize(Context, DequantizeArgs);

			if (Object != nullptr)
			{
				Struct = Cast<UScriptStruct>(Object);
				ensureMsgf(Struct != nullptr, TEXT("Unable to cast object %s to UScriptStruct"), ToCStr(Object->GetPathName()));
				if (Struct == nullptr)
				{
					Context.SetError(GNetError_InvalidValue);
					return;
				}
			}
		}

		// Deserialize struct data
		if (Struct != nullptr)
		{
			FStructNetSerializerConfig StructConfig;
			StructConfig.StateDescriptor = Config->DescriptorCache.FindOrAddDescriptor(Struct);
			if (!StructConfig.StateDescriptor.IsValid())
			{
				ensureMsgf(StructConfig.StateDescriptor.IsValid(), TEXT("Unable to create ReplicationStateDescriptor for struct %s."), ToCStr(Struct->GetPathName()));
				Context.SetError(GNetError_InvalidValue);
				return;
			}

			// If we changed types we need to free the struct, adjust the storage size and update struct info.
			if (StructType != Value.StructType)
			{
				FreeStructInstance(Context, Config, Value);
				Value.StructData.AdjustSize(Context, StructConfig.StateDescriptor->InternalSize, StructConfig.StateDescriptor->InternalAlignment);
				Value.StructType = StructType;

				Value.StructDescriptorTraits = StructConfig.StateDescriptor->Traits;
				const FString& PathNameString = Struct->GetPathName();
				Value.StructName = FName(PathNameString);
			}

			FNetDeserializeArgs DeserializeArgs = Args;
			DeserializeArgs.Target = NetSerializerValuePointer(Value.StructData.GetData());
			DeserializeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
			StructNetSerializer->Deserialize(Context, DeserializeArgs);
		}
		else
		{
			UE_LOG(LogIris, Error, TEXT("Unable to find struct using FQuantizedObjectReference %s when deserializing instanced struct."), ToCStr(StructType.ToString()));
			ensure(Struct != nullptr);

			Context.SetError(GNetError_InvalidValue);
			return;
		}
	}
	else
	{
		Reset(Context, Config, Value);
	}
}

// -----------------------------------------------------------------------------
// SerializeDelta / DeserializeDelta —— 暂未实现 Delta 压缩
// -----------------------------------------------------------------------------
// 接收端可能拿不到对应 UScriptStruct（被 cook out / 模块未加载）；此时 Delta
// 流难以优雅降级。当前 fallback 走完整 Serialize/Deserialize。
// $IRIS TODO 注释见函数内。
void FInstancedStructNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	// Skip DC support for now. Need to figure out how to gracefully handle missing UScriptStruct on the receiving end.
	Serialize(Context, Args);
}

void FInstancedStructNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	// Skip DC support for now. Need to figure out how to gracefully handle missing UScriptStruct on the receiving end.
	Deserialize(Context, Args);
}

// -----------------------------------------------------------------------------
// Quantize —— 真实 FInstancedStruct → 量化态
// -----------------------------------------------------------------------------
// 1. 取真实 ScriptStruct，FindOrAddDescriptor 拿 ReplicationStateDescriptor；
// 2. 类型变更才需要重新分配 StructData，并重新量化 StructType（UObject 引用）；
// 3. 调 StructNetSerializer.Quantize 把真实 struct 量化进 StructData。
// 类型不变时复用旧 StructData allocation —— 减少抖动。
void FInstancedStructNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));

	if (Source.IsValid())
	{
		const UScriptStruct* Struct = Source.GetScriptStruct();

		TRefCountPtr<const FReplicationStateDescriptor> DescriptorRef = Config->DescriptorCache.FindOrAddDescriptor(Struct);
		if (ensureMsgf(DescriptorRef.IsValid(), TEXT("Unable to create descriptor for struct %s. Unexpected."), ToCStr(Struct->GetFullName())))
		{
			const FString& PathNameString = Struct->GetPathName();
			const FName PathName(PathNameString);
			// If the struct type is the same as previous instance we don't need to free memory or adjust allocations.
			if (PathName != Target.StructName)
			{
				// We need to free the previous struct data prior to overwriting it. Doing this early.
				FreeStructInstance(Context, Config, Target);

				// Adjust struct storage size
				Target.StructData.AdjustSize(Context, DescriptorRef->InternalSize, DescriptorRef->InternalAlignment);

				// Adjust struct name and traits
				Target.StructName = PathName;
				Target.StructDescriptorTraits = DescriptorRef->Traits;

				// Quantize the struct type since the receiver need it to be able to serialize the data properly.
				{
					FNetQuantizeArgs QuantizeArgs = Args;
					QuantizeArgs.Source = NetSerializerValuePointer(&Struct);
					QuantizeArgs.Target = NetSerializerValuePointer(&Target.StructType);
					QuantizeArgs.NetSerializerConfig = ObjectNetSerializer->DefaultConfig;
					ObjectNetSerializer->Quantize(Context, QuantizeArgs);
				}
			}

			// Quantize the struct instance into the target memory.
			if (Target.StructData.Num() > 0)
			{
				FStructNetSerializerConfig StructConfig;
				StructConfig.StateDescriptor = DescriptorRef;

				FNetQuantizeArgs QuantizeArgs = Args;
				QuantizeArgs.Source = NetSerializerValuePointer(Source.GetMemory());
				QuantizeArgs.Target = NetSerializerValuePointer(Target.StructData.GetData());
				QuantizeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
				StructNetSerializer->Quantize(Context, QuantizeArgs);
			}

			return;
		}
	}

	// Path taken for uninitialized FInstancedStruct or if an error was detected.
	Reset(Context, Config, Target);
}

// -----------------------------------------------------------------------------
// Dequantize —— 量化态 → 真实 FInstancedStruct（按需 InitializeAs 切类型）
// -----------------------------------------------------------------------------
// $IRIS TODO : Consider implementing Apply to avoid unnecessary memory operations.
// 注意：Dequantize 永远把"完整 struct 内容"覆盖到 Target；如果只想要"按字段 Apply"
// 应单独走 Apply 路径（见下方）。
void FInstancedStructNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));

	if (!Source.StructType.IsValid())
	{
		Target.Reset();
		return;
	}

	const UScriptStruct* Struct = nullptr;
	{
		UObject* Object = nullptr;

		FNetDequantizeArgs DequantizeArgs = {};
		DequantizeArgs.Source = NetSerializerValuePointer(&Source.StructType);
		DequantizeArgs.Target = NetSerializerValuePointer(&Object);
		DequantizeArgs.NetSerializerConfig = ObjectNetSerializer->DefaultConfig;
		ObjectNetSerializer->Dequantize(Context, DequantizeArgs);

		if (Object != nullptr)
		{
			Struct = Cast<UScriptStruct>(Object);
			ensureMsgf(Struct != nullptr, TEXT("Unable to cast object %s to UScriptStruct"), ToCStr(Object->GetPathName()));
		}
	}
	
	if (ensureMsgf(Struct != nullptr, TEXT("Unable to find struct using FQuantizedObjectReference %s"), ToCStr(Source.StructType.ToString())))
	{
		// Re-initialize if the type changes.
		if (Struct != Target.GetScriptStruct())
		{
			Target.InitializeAs(Struct);
		}

		FStructNetSerializerConfig StructConfig;
		StructConfig.StateDescriptor = Config->DescriptorCache.FindOrAddDescriptor(Struct);

		if (ensureMsgf(StructConfig.StateDescriptor.IsValid(), TEXT("Unable to create ReplicationStateDescriptor for struct %s."), ToCStr(Source.StructName.ToString())))
		{
			FNetDequantizeArgs DequantizeArgs = Args;
			DequantizeArgs.NetSerializerConfig = &StructConfig;
			DequantizeArgs.Source = NetSerializerValuePointer(Source.StructData.GetData());
			DequantizeArgs.Target = NetSerializerValuePointer(Target.GetMutableMemory());
			StructNetSerializer->Dequantize(Context, DequantizeArgs);
		}
	}
}

// -----------------------------------------------------------------------------
// IsEqual —— 量化态：长度 + StructType + 字节比对；真实态：FInstancedStruct::operator==
// -----------------------------------------------------------------------------
bool FInstancedStructNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		if (Value0.StructData.Num() != Value1.StructData.Num())
		{
			return false;
		}

		if (Value0.StructType != Value1.StructType)
		{
			return false;
		}

		if (FMemory::Memcmp(Value0.StructData.GetData(), Value1.StructData.GetData(), Value0.StructData.Num()) != 0)
		{
			return false;
		}

		return true;
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		return Value0 == Value1;
	}
}

bool FInstancedStructNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<const SourceType*>(Args.Source);

	return true;
}

// -----------------------------------------------------------------------------
// CloneDynamicState —— 深克隆量化态（StructData allocation + 子 struct 内 dynamic）
// -----------------------------------------------------------------------------
void FInstancedStructNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));

	Target.StructData.Clone(Context, Source.StructData);

	if (EnumHasAnyFlags(Source.StructDescriptorTraits, EReplicationStateTraits::HasDynamicState))
	{
		FStructNetSerializerConfig StructConfig;
		StructConfig.StateDescriptor = Config->DescriptorCache.FindOrAddDescriptor(Source.StructName);

		if (ensureMsgf(StructConfig.StateDescriptor.IsValid(), TEXT("Unable to create ReplicationStateDescriptor for struct %s."), ToCStr(Source.StructName.ToString())))
		{
			FNetCloneDynamicStateArgs CloneArgs = Args;
			CloneArgs.NetSerializerConfig = &StructConfig;
			CloneArgs.Source = NetSerializerValuePointer(Source.StructData.GetData());
			CloneArgs.Target = NetSerializerValuePointer(Target.StructData.GetData());
			StructNetSerializer->CloneDynamicState(Context, CloneArgs);
		}
	}
}

// -----------------------------------------------------------------------------
// FreeDynamicState —— 释放整段量化态
// -----------------------------------------------------------------------------
void FInstancedStructNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));
	
	InternalFreeStructInstance(Context, Config, Value);
	Value.StructData.Free(Context);
}

// -----------------------------------------------------------------------------
// CollectNetReferences —— 收集 StructType 的对象引用 + 嵌套 struct 内引用
// -----------------------------------------------------------------------------
// 1) StructType 自身就是一个 UScriptStruct 引用 → ResolveOnClient；
// 2) 若元素 Descriptor 标记 HasObjectReference，递归调 StructNetSerializer.CollectNetReferences。
void FInstancedStructNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));

	if (Value.StructType.IsValid())
	{
		FNetReferenceCollector& Collector = *reinterpret_cast<FNetReferenceCollector*>(Args.Collector);

		// What's the proper reference type?
		const FNetReferenceInfo ReferenceInfo(FNetReferenceInfo::EResolveType::ResolveOnClient);
		Collector.Add(ReferenceInfo, Value.StructType, Args.ChangeMaskInfo);
	}

	if (EnumHasAnyFlags(Value.StructDescriptorTraits, EReplicationStateTraits::HasObjectReference))
	{
		FStructNetSerializerConfig StructConfig;
		StructConfig.StateDescriptor = Config->DescriptorCache.FindOrAddDescriptor(Value.StructName);

		if (ensureMsgf(StructConfig.StateDescriptor.IsValid(), TEXT("Unable to create ReplicationStateDescriptor for struct %s."), ToCStr(Value.StructName.ToString())))
		{
			FNetCollectReferencesArgs CollectReferencesArgs = Args;
			CollectReferencesArgs.NetSerializerConfig = &StructConfig;
			CollectReferencesArgs.Source = NetSerializerValuePointer(Value.StructData.GetData());
			StructNetSerializer->CollectNetReferences(Context, CollectReferencesArgs);
		}
	}
}

// -----------------------------------------------------------------------------
// Apply —— 把 Source（来自反量化）合并到 Target（真实游戏对象）
// -----------------------------------------------------------------------------
// 类型不变 → 复用 Target storage 调 ApplyStruct（仅覆盖 replicated 字段，
// 保留非 replicated 字段，比 Dequantize 整体覆盖更友好）。
// 类型变化 → InitializeAs 重置 Target，再 ApplyStruct。
void FInstancedStructNetSerializer::Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
	FInstancedStructNetSerializerConfig* Config = const_cast<FInstancedStructNetSerializerConfig*>(static_cast<const FInstancedStructNetSerializerConfig*>(Args.NetSerializerConfig));

	// If source and target has the same type make sure to not clobber not replicated properties.
	const UScriptStruct* ScriptStruct = Source.GetScriptStruct();
	if (ScriptStruct != Target.GetScriptStruct())
	{
		Target.InitializeAs(ScriptStruct);
	}

	if (ScriptStruct)
	{
		const TRefCountPtr<const FReplicationStateDescriptor> Descriptor = Config->DescriptorCache.FindOrAddDescriptor(ScriptStruct);
		FReplicationStateOperations::ApplyStruct(Context, Target.GetMutableMemory(), Source.GetMemory(), Descriptor);
	}
}

// -----------------------------------------------------------------------------
// 内部释放/重置三件套
// -----------------------------------------------------------------------------
// FreeStructInstance：先 InternalFreeStructInstance 释放 dynamic state，再
//                     memzero StructData（复用 storage 给下一种类型）。
// Reset：彻底释放（包含 storage 本体）+ memzero 整个 QuantizedType。
// InternalFreeStructInstance：仅当元素 Descriptor 含 HasDynamicState 时才递归
//                             调 StructNetSerializer.FreeDynamicState。
void FInstancedStructNetSerializer::FreeStructInstance(FNetSerializationContext& Context, FInstancedStructNetSerializerConfig* Config, QuantizedType& Value)
{
	InternalFreeStructInstance(Context, Config, Value);
	if (Value.StructData.Num() > 0)
	{
		FMemory::Memzero(Value.StructData.GetData(), Value.StructData.Num());
	}
}

void FInstancedStructNetSerializer::Reset(FNetSerializationContext& Context, FInstancedStructNetSerializerConfig* Config, QuantizedType& Value)
{
	InternalFreeStructInstance(Context, Config, Value);
	Value.StructData.Free(Context);

	FMemory::Memzero(&Value, sizeof(QuantizedType));
}

void FInstancedStructNetSerializer::InternalFreeStructInstance(FNetSerializationContext& Context, FInstancedStructNetSerializerConfig* Config, QuantizedType& Value)
{
	if (Value.StructData.Num() > 0 && EnumHasAnyFlags(Value.StructDescriptorTraits, EReplicationStateTraits::HasDynamicState))
	{
		FStructNetSerializerConfig StructConfig;
		StructConfig.StateDescriptor = Config->DescriptorCache.FindOrAddDescriptor(Value.StructName);

		FNetFreeDynamicStateArgs FreeArgs;
		FreeArgs.NetSerializerConfig = &StructConfig;
		FreeArgs.Source = NetSerializerValuePointer(Value.StructData.GetData());

		StructNetSerializer->FreeDynamicState(Context, FreeArgs);
	}
}

}

namespace UE::Net
{

// 配置初始化（注册时调用） ——
// 1) 暂时不启用 SupportedTypes 白名单（见 UE-180981）；
// 2) 给 DescriptorCache 设置调试名 "OuterClassName.PropertyName"；
// 3) 限制 LRU 容量（CVar MaxCachedInstancedStructDescriptorCount）。
void InitInstancedStructNetSerializerConfig(FInstancedStructNetSerializerConfig* Config, const FProperty* Property)
{
	// We want to be explicit about which structs are supported in the config. That requires UE-180981. For now let's allow any UScriptStruct.
	Config->SupportedTypes.Reset();

	{
		FString DebugName;
		DebugName.Reserve(256);

		FFieldVariant Owner = Property->GetOwnerVariant();
		if (const UObject* Object = Owner.ToUObject())
		{
			DebugName.Append(Object->GetName()).AppendChar(TEXT('.'));
		}
		else if (const FField* Field = Owner.ToField())
		{
			DebugName.Append(Field->GetName()).AppendChar(TEXT('.'));
		}
		DebugName.Append(Property->GetName());

		Config->DescriptorCache.SetDebugName(DebugName);
	}

	// Add supported type info to the cache.
	Config->DescriptorCache.AddSupportedTypes(TConstArrayView<TSoftObjectPtr<UScriptStruct>>(Config->SupportedTypes));

	const bool bIsAllowingArbitraryStruct = true;
	if (bIsAllowingArbitraryStruct)
	{
		Config->DescriptorCache.SetMaxCachedDescriptorCount(MaxCachedInstancedStructDescriptorCount);
	}
}

// FInstancedStructPropertyNetSerializerInfo 实现 ——
// 与 PropertyNetSerializerInfoRegistry 对接：声明绑定到 named struct
// "InstancedStruct"，并强制每个属性独立 Config（属性级白名单 + 调试信息）。
FInstancedStructPropertyNetSerializerInfo::FInstancedStructPropertyNetSerializerInfo()
: FNamedStructPropertyNetSerializerInfo(FName("InstancedStruct"), UE_NET_GET_SERIALIZER(FInstancedStructNetSerializer))
{
}

bool FInstancedStructPropertyNetSerializerInfo::CanUseDefaultConfig(const FProperty* Property) const
{
	// Creating property specific configs so that we can validate and allow only property specific types. This allows us property specific tracking of used types too.
	// 强制 false：每个 InstancedStruct 属性都拿独立 Config，便于做属性级白名单与缓存隔离。
	return false;
}

FNetSerializerConfig* FInstancedStructPropertyNetSerializerInfo::BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const
{
	// Placement-new 在调用者提供的 buffer 里构造 Config，再做属性级初始化。
	FInstancedStructNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FInstancedStructNetSerializerConfig();
	InitInstancedStructNetSerializerConfig(Config, Property);
	return Config;
}

}

namespace UE::Net::Private
{

// =============================================================================
// FInstancedStructDescriptorCache 实现 —— LRU/Map 双模式 Descriptor 缓存
// =============================================================================

FInstancedStructDescriptorCache::FInstancedStructDescriptorCache()
{
	// Intentionally left empty for debugging purposes.
}

FInstancedStructDescriptorCache::~FInstancedStructDescriptorCache()
{
	// Intentionally left empty for debugging purposes.
}

// Name for debugging purposes
void FInstancedStructDescriptorCache::SetDebugName(const FString& InDebugName)
{
	DebugName = InDebugName;
}

// 切换 LRU/无限缓存模式：
//   * MaxCount <= 0  → 用 DescriptorMap（无限），LRU 清空；
//   * MaxCount  > 0  → 用 DescriptorLruCache（限大小），Map 清空。
void FInstancedStructDescriptorCache::SetMaxCachedDescriptorCount(int32 MaxCount)
{
	if (MaxCount <= 0)
	{
		DescriptorLruCache.Empty(0);
		MaxCachedDescriptorCount = 0;
	}
	else
	{
		// Clear DescriptorMap which is only used for unlimited MaxCount
		// 切换模式时把另一存储清空，避免重复缓存导致内存浪费。
		UE_CLOG(!DescriptorMap.IsEmpty(), LogIris, Warning, TEXT("Clearing DescriptorMap from FIstancedStructDescriptorCache %s"), ToCStr(DebugName));
		DescriptorMap.Empty();
		DescriptorLruCache.Empty(MaxCount);
		MaxCachedDescriptorCount = MaxCount;
	}
}

void FInstancedStructDescriptorCache::AddSupportedTypes(const TConstArrayView<TSoftObjectPtr<UScriptStruct>>& InSupportedTypes)
{
	for (const TSoftObjectPtr<UScriptStruct>& Type : InSupportedTypes)
	{
		SupportedTypes.Add(Type);
	}
}


// 白名单检查：空 SupportedTypes 表示放行任意 UScriptStruct。
// 否则要求 Struct IsChildOf 列表中任意一项（继承基类语义）。
bool FInstancedStructDescriptorCache::IsSupportedType(const UScriptStruct* Struct) const
{
	if (ensure(Struct != nullptr))
	{
		if (SupportedTypes.IsEmpty())
		{
			return true;
		}

		for (const TSoftObjectPtr<UScriptStruct>& SupportedType : SupportedTypes)
		{
			if (const UScriptStruct* SupportedStruct = SupportedType.Get())
			{
				if (Struct->IsChildOf(SupportedStruct))
				{
					return true;
				}
			}
		}
	}

	return false;
}

TRefCountPtr<const FReplicationStateDescriptor> FInstancedStructDescriptorCache::FindDescriptor(FName StructPath)
{
	UE::TScopeLock Lock(Mutex);
	if (MaxCachedDescriptorCount > 0)
	{
		return DescriptorLruCache.FindAndTouchRef(StructPath);
	}
	else
	{
		return DescriptorMap.FindRef(StructPath);
	}
}

TRefCountPtr<const FReplicationStateDescriptor> FInstancedStructDescriptorCache::FindDescriptor(const UScriptStruct* Struct)
{
	if (!Struct)
	{
		return TRefCountPtr<const FReplicationStateDescriptor>();
	}

	const FString& PathNameString = Struct->GetPathName();
	const FName PathName(PathNameString);
	return FindDescriptor(PathName);
}

// FindOrAddDescriptor(FName) ——
// 命中即返回；未命中则用 StaticLoadObject 加载 UScriptStruct（按 PathName 字符串），
// 再调 CreateAndCacheDescriptor 烘焙 + 入缓存。
TRefCountPtr<const FReplicationStateDescriptor> FInstancedStructDescriptorCache::FindOrAddDescriptor(FName StructPath)
{
	TRefCountPtr<const FReplicationStateDescriptor> Descriptor;

	Descriptor = FindDescriptor(StructPath);
	if (Descriptor.IsValid())
	{
		return Descriptor;
	}

	const UObject* Object = StaticLoadObject(UScriptStruct::StaticClass(), nullptr, ToCStr(StructPath.ToString()), nullptr, LOAD_None);
	if (const UScriptStruct* Struct = Cast<UScriptStruct>(Object))
	{
		Descriptor = CreateAndCacheDescriptor(Struct, StructPath);
	}
	else
	{
		// Cast fail?
		ensureMsgf(Object == nullptr, TEXT("Unable to cast object %s to UScriptStruct"), ToCStr(Object->GetPathName()));
	}

	return Descriptor;
}

// FindOrAddDescriptor(UScriptStruct) ——
// 类型已就绪不需要 Load。先 IsSupportedType 通过白名单校验，再烘焙缓存。
TRefCountPtr<const FReplicationStateDescriptor> FInstancedStructDescriptorCache::FindOrAddDescriptor(const UScriptStruct* Struct)
{
	TRefCountPtr<const FReplicationStateDescriptor> Descriptor;
	if (ensure(Struct != nullptr))
	{
		const FString& PathNameString = Struct->GetPathName();
		const FName PathName(PathNameString);

		Descriptor = FindDescriptor(PathName);
		if (Descriptor.IsValid())
		{
			return Descriptor;
		}

		if (!IsSupportedType(Struct))
		{
			return Descriptor;
		}

		// Create descriptor and add it to the cache.
		Descriptor = CreateAndCacheDescriptor(Struct, PathName);
	}

	return Descriptor;
}

// CreateAndCacheDescriptor —— 真正烘焙 + 加锁入缓存
// 调用 FReplicationStateDescriptorBuilder::CreateDescriptorForStruct（昂贵），
// 因此一定要缓存复用。
TRefCountPtr<const FReplicationStateDescriptor> FInstancedStructDescriptorCache::CreateAndCacheDescriptor(const UScriptStruct* Struct, FName StructPath)
{
	TRefCountPtr<const FReplicationStateDescriptor> Descriptor;

	FReplicationStateDescriptorBuilder::FParameters Params;
	Descriptor = FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Struct, Params);

	{
		UE::TScopeLock Lock(Mutex);
		if (MaxCachedDescriptorCount > 0)
		{
			DescriptorLruCache.Add(StructPath, Descriptor);
		}
		else
		{
			DescriptorMap.Add(StructPath, Descriptor);
		}
	}

	return Descriptor;
}

}


