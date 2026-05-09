// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// SoftObjectNetSerializers.cpp —— 软引用 NetSerializer 实现
// -----------------------------------------------------------------------------
// 三个软引用 Serializer 的实现：
//   • FSoftObjectNetSerializer
//       - 量化态 FFSoftObjectNetSerializerQuantizedData 同时含路径量化态（Path）
//         和对象引用量化态（ObjectReference），bIsObject 决定走哪条路径。
//       - 路径走 FStringNetSerializerBase 的 ANSI/UTF 编码；对象走
//         FObjectNetSerializer（最终落到 FObjectReferenceCache + PackageMap）。
//       - bHasCustomNetReference = true：在 CollectNetReferences 中把对象引用
//         注册到 FNetReferenceCollector，让 Iris 的引用解析阶段处理对端定位。
//   • FSoftObjectPathNetSerializer / FSoftClassPathNetSerializer
//       - 仅路径串。直接复用 FStringNetSerializerBase 的 Quantize/Dequantize/
//         Clone/Free/Serialize/Deserialize，把"源类型 ↔ FString"转换层薄薄地包一层。
//
// 注意：FSoftObjectNetSerializer 的等值比较在量化态对象路径分支调用
//       Super::IsQuantizedEqual（基于字节）；当前代码保持原有行为，
//       未对 ObjectReference 做单独 IsEqual（可视为"以路径为权威标识"）。
// =============================================================================

#include "Iris/Serialization/SoftObjectNetSerializers.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/Serialization/InternalNetSerializer.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/Serialization/ObjectNetSerializer.h"
#include "Iris/Serialization/QuantizedObjectReference.h"
#include "Iris/Serialization/StringNetSerializerUtils.h"
#include "Templates/IsPODType.h"
#include "UObject/SoftObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SoftObjectNetSerializers)


namespace UE::Net
{

// FSoftObjectNetSerializer 的量化态。
// 同时容纳"路径量化态"和"对象引用量化态"，由 bIsObject 决定有效字段。
// 路径与对象引用并不互斥共享内存（结构体内并列存储），便于在二者之间切换时
// 释放/复用动态分配；尾部 Padding[7] 让 sizeof 取到 8 字节对齐边界。
struct FFSoftObjectNetSerializerQuantizedData
{
	// 路径量化态（FString 编码后的字节缓冲 + 标志）
	Private::FStringNetSerializerBase::QuantizedType Path;
	// 对象引用量化态（FObjectNetSerializer 内部使用的 NetRefHandle 等）
	FQuantizedObjectReference ObjectReference;
	// true：该 SoftObjectPtr 当前指向一个非"网络稳定命名"的运行期对象，
	//       走 FObjectNetSerializer 直接以对象引用复制；
	// false：走路径字符串复制（最常见的资产路径形态）。
	bool bIsObject;
	uint8 Padding[7];
};

}

// 量化态必须是 POD，使 Iris 的状态拷贝/比较走 memcpy/memcmp 快路径。
template<> struct TIsPODType<UE::Net::FFSoftObjectNetSerializerQuantizedData> { enum { Value = true }; };

namespace UE::Net
{

// -----------------------------------------------------------------------------
// FSoftObjectNetSerializer —— FSoftObjectPtr 序列化器
// -----------------------------------------------------------------------------
// 设计要点：
//   • 优先走"路径"分支（节省带宽 + 与编辑器/资产管线一致），仅当对象已加载且
//     名字"对网络不稳定"（如运行期生成的 Actor）才退回"对象引用"分支。
//   • 自身非"持有动态状态"以外，还声明 bHasCustomNetReference 让 Iris 知道
//     需要在 CollectNetReferences 中收集 ObjectReference。
//   • bIsForwardingSerializer = true：表示本 Serializer 会把序列化工作"转发"
//     给嵌套的其它 Serializer（FObjectNetSerializer / FStringNetSerializerBase），
//     若 hooks 缺失，UE_NET_IMPLEMENT_SERIALIZER 会触发 assert。
struct FSoftObjectNetSerializer : public Private::FStringNetSerializerBase
{
	// Version
	static const uint32 Version = 0;

	// Traits
	static constexpr bool bIsForwardingSerializer = true; // 缺失方法时触发断言
	static constexpr bool bHasCustomNetReference = true;

	// Types
	typedef FSoftObjectPtr SourceType;
	typedef FFSoftObjectNetSerializerQuantizedData QuantizedType;
	typedef FSoftObjectNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	// Implementation
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	// Delta 序列化交给默认实现：等价于 Serialize + IsEqual 比较
	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);
	// 自定义引用收集：把对象引用注册到 NetReferenceCollector（仅对象分支）。
	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	typedef Private::FStringNetSerializerBase Super;

	// 缓存内嵌 ObjectNetSerializer 的全局指针与默认配置，便于转发调用。
	inline static const FNetSerializer* ObjectNetSerializer = &UE_NET_GET_SERIALIZER(FObjectNetSerializer);
	inline static const FNetSerializerConfig* ObjectNetSerializerConfig = UE_NET_GET_SERIALIZER_DEFAULT_CONFIG(FObjectNetSerializer);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FSoftObjectNetSerializer);

// -----------------------------------------------------------------------------
// FSoftObjectPathNetSerializer —— FSoftObjectPath 序列化器（纯路径）
// -----------------------------------------------------------------------------
// 直接继承 FStringNetSerializerBase，把 SourceType 切到 FSoftObjectPath。
// 量化阶段把 SoftObjectPath.ToString() 当作 FString 进入基类编码路径。
struct FSoftObjectPathNetSerializer : public Private::FStringNetSerializerBase
{
	// Version
	static const uint32 Version = 0;

	// Types
	typedef FSoftObjectPath SourceType;
	typedef FSoftObjectPathNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	// Implementation
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FSoftObjectPathNetSerializer);

// -----------------------------------------------------------------------------
// FSoftClassPathNetSerializer —— FSoftClassPath（继承自 FSoftObjectPath）
// -----------------------------------------------------------------------------
// 与 FSoftObjectPathNetSerializer 等价；区别仅在源类型，专用于 UClass 路径。
struct FSoftClassPathNetSerializer : public Private::FStringNetSerializerBase
{
	// Version
	static const uint32 Version = 0;

	// Types
	typedef FSoftClassPath SourceType;
	typedef FSoftClassPathNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	// Implementation
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FSoftClassPathNetSerializer);
const FSoftClassPathNetSerializer::ConfigType FSoftClassPathNetSerializer::DefaultConfig;

// FSoftObjectNetSerializer
// 量化：检查 SoftObjectPtr 是否指向已加载且"网络稳定命名"为否的对象。
//   - 否则路径分支：取 GetUniqueID().ToString() 走 FString 编码。
//   - 是则对象分支：把 UObject* 交给 FObjectNetSerializer 做 NetRef 量化。
// 切换分支时：若上一帧是路径态、这一帧变成对象态，需要先释放路径动态分配。
void FSoftObjectNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	QuantizedType TempValue = {};

	const UObject* Object = Source.Get();
	// 优先使用路径方式。仅当对象不是"网络稳定命名"时才退回对象序列化
	const bool bIsObject = Object && !Object->IsFullNameStableForNetworking();
	if (bIsObject && !Target.bIsObject)
	{
		// 旧帧曾走路径分支，先释放路径量化态的字节缓冲
		FNetFreeDynamicStateArgs FreeArgs;
		FreeArgs.Source = reinterpret_cast<NetSerializerValuePointer>(&Target.Path);
		Super::FreeDynamicState(Context, FreeArgs);
	}

	TempValue.bIsObject = bIsObject;
	if (bIsObject)
	{
		// 对象分支：转发到 FObjectNetSerializer。
		// 注意 Source 此时取 &Object（一个 UObject* 的地址）。
		FNetQuantizeArgs QuantizeArgs = Args;
		QuantizeArgs.NetSerializerConfig = reinterpret_cast<NetSerializerConfigParam>(ObjectNetSerializerConfig);
		QuantizeArgs.Source = reinterpret_cast<NetSerializerValuePointer>(&Object);
		QuantizeArgs.Target = reinterpret_cast<NetSerializerValuePointer>(&TempValue.ObjectReference);
		ObjectNetSerializer->Quantize(Context, QuantizeArgs);
	}
	else
	{
		// 路径分支：把 SoftObjectPath.ToString() 交给基类做 FString 量化。
		const FSoftObjectPath& SoftObjectPath = Source.GetUniqueID();
		const FString& SoftObjectPathString = SoftObjectPath.ToString();

		FNetQuantizeArgs QuantizeArgs = Args;
		QuantizeArgs.Source = 0;
		QuantizeArgs.Target = reinterpret_cast<NetSerializerValuePointer>(&TempValue.Path);
		FStringNetSerializerBase::Quantize(Context, QuantizeArgs, SoftObjectPathString);
	}

	Target = TempValue;
}

// 反量化：根据 bIsObject 还原 SoftObjectPtr。
//   - 对象分支：通过 FObjectNetSerializer 取得 UObject*，赋值给 SoftObjectPtr
//     （会自动构造 UniqueID 并释放此前 weak 指针）；
//   - 路径分支：从基类得到 FString，塞进 SoftObjectPtr.GetUniqueID().SetPath。
//   - 最后一律调用 FixupForPIE 处理 PIE 路径前缀（若启用 PIE）。
void FSoftObjectNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	if (Source.bIsObject)
	{
		UObject* Object = nullptr;

		FNetDequantizeArgs DequantizeArgs = Args;
		DequantizeArgs.NetSerializerConfig = ObjectNetSerializerConfig;
		DequantizeArgs.Source = reinterpret_cast<NetSerializerValuePointer>(&Source.ObjectReference);
		DequantizeArgs.Target = reinterpret_cast<NetSerializerValuePointer>(&Object);
		ObjectNetSerializer->Dequantize(Context, DequantizeArgs);
		
		// 这一步会释放任何旧的 weak 引用，并在内部构建 UniqueID
		Target = Object;
	}
	else
	{
		FString SoftObjectPathString;
		Super::Dequantize(Context, Args, SoftObjectPathString);

		// 路径分支：清空 weak ptr 后直接 SetPath
		Target.ResetWeakPtr();
		Target.GetUniqueID().SetPath(MoveTemp(SoftObjectPathString));
	}

	// PIE 适配：如果运行在 PIE 模式，需要把路径前缀转换为 PIE 命名空间
	Target.GetUniqueID().FixupForPIE();
}

// 序列化协议：bIsObject:1 → 写对象引用 / 写字符串。
void FSoftObjectNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if (Writer->WriteBool(Source.bIsObject))
	{
		// 对象分支：转发到 FObjectNetSerializer 序列化 ObjectReference
		FNetSerializeArgs SerializeArgs = Args;
		SerializeArgs.NetSerializerConfig = ObjectNetSerializerConfig;
		SerializeArgs.Source = reinterpret_cast<NetSerializerValuePointer>(&Source.ObjectReference);
		ObjectNetSerializer->Serialize(Context, SerializeArgs);
	}
	else
	{
		// 路径分支：转发到 FStringNetSerializerBase 序列化 Path
		FNetSerializeArgs SerializeArgs = Args;
		SerializeArgs.NetSerializerConfig = 0;
		SerializeArgs.Source = reinterpret_cast<NetSerializerValuePointer>(&Source.Path);
		Super::Serialize(Context, SerializeArgs);
	}
}

// 反序列化：与 Serialize 对称。注意切换分支时释放旧分支的动态状态。
void FSoftObjectNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	QuantizedType TempValue = {};
	TempValue.bIsObject = Reader->ReadBool();
	if (TempValue.bIsObject)
	{
		FNetDeserializeArgs DeserializeArgs = Args;
		DeserializeArgs.NetSerializerConfig = ObjectNetSerializerConfig;
		DeserializeArgs.Target = reinterpret_cast<NetSerializerValuePointer>(&TempValue.ObjectReference);
		ObjectNetSerializer->Deserialize(Context, DeserializeArgs);

		// 旧帧若为路径态，释放其动态分配
		if (!Target.bIsObject)
		{
			FNetFreeDynamicStateArgs FreeArgs = {};
			FreeArgs.Source = reinterpret_cast<NetSerializerValuePointer>(&Target.Path);
			Super::FreeDynamicState(Context, FreeArgs);
		}
	}
	else
	{
		// 复用旧路径分配，避免重复 alloc/free
		TempValue.Path = Target.Path;

		FNetDeserializeArgs DeserializeArgs = Args;
		DeserializeArgs.NetSerializerConfig = 0;
		DeserializeArgs.Target = reinterpret_cast<NetSerializerValuePointer>(&TempValue.Path);
		Super::Deserialize(Context, DeserializeArgs);
	}

	Target = TempValue;
}

// Delta 路径走默认模板：等同于完整序列化 + IsEqual 短路。
// （字符串/对象不支持位级别 delta）
void FSoftObjectNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	NetSerializeDeltaDefault<FSoftObjectNetSerializer::Serialize, FSoftObjectNetSerializer::IsEqual>(Context, Args);
}

void FSoftObjectNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	NetDeserializeDeltaDefault<sizeof(QuantizedType), FSoftObjectNetSerializer::Deserialize, FSoftObjectNetSerializer::FreeDynamicState, FSoftObjectNetSerializer::CloneDynamicState>(Context, Args);
}

// 等值比较：
//   - 量化态：先比 bIsObject；同分支后委托相应实现
//     · 对象分支：调用 ObjectNetSerializer->IsEqual 但当前实现传入的实际 Source
//                 为 Path 字段（参见原代码注释保留），效果上仍是字节量化态比较。
//     · 路径分支：Super::IsQuantizedEqual 走 memcmp。
//   - 源态：用大小写敏感的字符串比较 SoftObjectPath.ToString()，
//     避免量化前的 lower 转换开销。
bool FSoftObjectNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Source0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Source1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		if (Source0.bIsObject != Source1.bIsObject)
		{
			return false;
		}

		if (Source0.bIsObject)
		{
			FNetIsEqualArgs EqualArgs = Args;
			EqualArgs.NetSerializerConfig = ObjectNetSerializerConfig;
			EqualArgs.Source0 = reinterpret_cast<NetSerializerValuePointer>(&Source0.Path);
			EqualArgs.Source1 = reinterpret_cast<NetSerializerValuePointer>(&Source1.Path);
			return ObjectNetSerializer->IsEqual(Context, EqualArgs);
		}
		else
		{
			FNetIsEqualArgs EqualArgs = Args;
			EqualArgs.Source0 = reinterpret_cast<NetSerializerValuePointer>(&Source0.Path);
			EqualArgs.Source1 = reinterpret_cast<NetSerializerValuePointer>(&Source1.Path);
			return Super::IsQuantizedEqual(Context, Args);
		}
	}
	else
	{
		const SourceType& Source0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Source1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		const FString& SoftObjectPathString0 = Source0.GetUniqueID().ToString();
		const FString& SoftObjectPathString1 = Source1.GetUniqueID().ToString();
		// 这里可能不需要做大小写敏感的字符串比较，但若要去掉则需在量化阶段先把字符串转小写
		return SoftObjectPathString0.Equals(SoftObjectPathString1, ESearchCase::CaseSensitive);
	}
}

// 校验：把路径串交给基类 Validate（长度上限）。
bool FSoftObjectNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	const FSoftObjectPath& SoftObjectPath = Source.GetUniqueID();
	const FString& SoftObjectPathString = SoftObjectPath.ToString();
	return FStringNetSerializerBase::Validate(Context, Args, SoftObjectPathString);
}

// 克隆动态状态：仅当前为路径分支才需要深拷贝路径缓冲；对象分支按值拷贝即可。
void FSoftObjectNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	Target = Source;
	if (!Source.bIsObject)
	{
		FNetCloneDynamicStateArgs CloneArgs = Args;
		CloneArgs.NetSerializerConfig = 0;
		CloneArgs.Source = NetSerializerValuePointer(&Source.Path);
		CloneArgs.Target = NetSerializerValuePointer(&Target.Path);
		Super::CloneDynamicState(Context, CloneArgs);
	}
}

// 释放动态状态：仅路径分支需要释放（对象分支无 dyn state）。
void FSoftObjectNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& Source = *reinterpret_cast<QuantizedType*>(Args.Source);

	if (!Source.bIsObject)
	{
		FNetFreeDynamicStateArgs FreeArgs = Args;
		FreeArgs.NetSerializerConfig = 0;
		FreeArgs.Source = NetSerializerValuePointer(&Source.Path);
		Super::FreeDynamicState(Context, FreeArgs);
	}
}

// 收集对象引用到 NetReferenceCollector。仅对象分支会触发：
// 把 ObjectReference 标记为 ResolveOnClient，让客户端在反序列化阶段
// 通过 FObjectReferenceCache + IrisObjectReferencePackageMap 解析回 UObject*。
void FSoftObjectNetSerializer::CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);

	if (Source.bIsObject)
	{
		const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
		FNetReferenceCollector& Collector = *reinterpret_cast<FNetReferenceCollector*>(Args.Collector);

		// ResolveOnClient：客户端反序列化时再去 ObjectReferenceCache 解析为 UObject*。
		const FNetReferenceInfo ReferenceInfo(FNetReferenceInfo::EResolveType::ResolveOnClient);
		Collector.Add(ReferenceInfo, Value.ObjectReference, Args.ChangeMaskInfo);
	}
}

// FSoftObjectPathNetSerializer
// 量化：把 FSoftObjectPath.ToString() 交给基类做 FString 编码。
void FSoftObjectPathNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	const FString& SoftObjectPathString = Source.ToString();
	return FStringNetSerializerBase::Quantize(Context, Args, SoftObjectPathString);
}

// 反量化：从基类拿到 FString 后调用 SetPath。
void FSoftObjectPathNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	FString SoftObjectPathString;
	FStringNetSerializerBase::Dequantize(Context, Args, SoftObjectPathString);

	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
	Target.SetPath(SoftObjectPathString);
}

// 等值：量化态走基类字节比对；源态走大小写敏感的字符串比较。
bool FSoftObjectPathNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		return IsQuantizedEqual(Context, Args);
	}
	else
	{
		const SourceType& Source0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Source1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		const FString& SoftObjectPathString0 = Source0.ToString();
		const FString& SoftObjectPathString1 = Source1.ToString();
		return SoftObjectPathString0.Equals(SoftObjectPathString1, ESearchCase::CaseSensitive);
	}
}

bool FSoftObjectPathNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	const FString& SoftObjectPathString = Source.ToString();
	return FStringNetSerializerBase::Validate(Context, Args, SoftObjectPathString);
}

// FSoftClassPathNetSerializer
// 与 FSoftObjectPathNetSerializer 实现完全平行（仅源类型不同）。
void FSoftClassPathNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	const FString& SoftClassPathString = Source.ToString();
	return FStringNetSerializerBase::Quantize(Context, Args, SoftClassPathString);
}

void FSoftClassPathNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	FString SoftClassPathString;
	FStringNetSerializerBase::Dequantize(Context, Args, SoftClassPathString);

	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
	Target.SetPath(SoftClassPathString);
}

bool FSoftClassPathNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		return IsQuantizedEqual(Context, Args);
	}
	else
	{
		const SourceType& Source0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Source1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		const FString& SoftClassPathString0 = Source0.ToString();
		const FString& SoftClassPathString1 = Source1.ToString();
		return SoftClassPathString0.Equals(SoftClassPathString1, ESearchCase::CaseSensitive);
	}
}

bool FSoftClassPathNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	const FString& SoftClassPathString0 = Source.ToString();
	return Private::FStringNetSerializerBase::Validate(Context, Args, SoftClassPathString0);
}

}

