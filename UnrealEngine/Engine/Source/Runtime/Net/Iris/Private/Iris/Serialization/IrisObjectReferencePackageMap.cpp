// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// IrisObjectReferencePackageMap.cpp
// ---------------------------------------------------------------------------------------------
// UIrisObjectReferencePackageMap 与配套读/写 Scope 的实现。
//
// 核心写法（读/写两侧高度对偶）：
//   写侧：
//     1) 在 References / Names 数组里查找当前对象/Name；
//     2) 找到就拿到下标；找不到就 Add 到末尾，下标 = 新长度 - 1；
//     3) 用 SerializeIntPacked 把下标写到位流。
//   读侧：
//     1) 从位流读出 IntPacked 下标；
//     2) IsValidIndex 校验防越界（恶意/损坏包防御）；
//     3) 用下标到数组里取出 UObject* / FName。
//
// 两个特别说明：
//   - SerializeName 受 CVar `net.iris.EnableIrisPackageMapNameExports` 控制，关闭后退化
//     回 Super 的旧字符串实现——便于在 Iris 名字导出还在演进时回退。
//   - WriteScope 构造里有一次 ensure 校验"读用 Exports != 写用 Exports"——避免同一 buffer
//     同时充当读源和写槽，否则会导致索引语义错乱。
// =============================================================================================

#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IrisObjectReferencePackageMap)

namespace UE
{
	namespace Net
	{
		/** CVar：是否启用"FName 也走 Iris Exports 索引"。关闭则退化为 Super 的字符串序列化。
		 *  当 Iris Name 导出系统出现问题时可作为应急回退开关。 */
		bool bEnableIrisPackageMapNameExports = true;
		static FAutoConsoleVariableRef CVarEnableIrisPackageMapNameExports(TEXT("net.iris.EnableIrisPackageMapNameExports"), bEnableIrisPackageMapNameExports, TEXT("If enabled, iris captures and exports fnames when calling into old serialziation code instead of serializing a strings."));
	}
}

// ------------------------------------------------------------------------------------------
// SerializeObject —— 对象引用劫持
// ------------------------------------------------------------------------------------------
// 根据 Ar 是 Saving / Loading 选择写槽 / 读源（同一时刻只能有其一非空）；为空则 ensure 失败。
// 注意 OutNetGUID 在本实现里完全忽略——我们不再发 NetGUID，只发数组下标。
// ------------------------------------------------------------------------------------------
bool UIrisObjectReferencePackageMap::SerializeObject(FArchive& Ar, UClass* InClass, UObject*& Obj, FNetworkGUID* OutNetGUID)
{
	// 根据方向选择对应的 Exports 容器；保存时用写槽，读取时用只读源。
	UE::Net::FIrisPackageMapExports* PackageMapExports = Ar.IsSaving() ? PackageMapExportsForWriting : PackageMapExportsForReading;

	// 进了这里却没有挂 Exports，说明 Scope 没正确建立 —— 这是调用约束错误。
	if (!ensure(PackageMapExports))
	{
		return false;
	}

	UE::Net::FIrisPackageMapExports::FObjectReferenceArray* References = &PackageMapExports->References;

	if (Ar.IsSaving())
	{
		// 写侧：先去重查找，找不到就追加到末尾；位流上只写下标。
		int32 Index;
		if (!References->Find(Obj, Index))
		{
			Index = References->Add(Obj);
		}
		uint32 IndexToWrite = static_cast<uint32>(Index);
		Ar.SerializeIntPacked(IndexToWrite);
	}
	else
	{
		// 读侧：先读下标，越界检查防御损坏包。
		uint32 ReadIndex;
		Ar.SerializeIntPacked(ReadIndex);
		if (References->IsValidIndex(ReadIndex))
		{
			Obj = (*References)[ReadIndex];
		}
		else
		{
			// 索引越界：协议不一致或包损坏。返回 false 让上层走错误处理路径。
			ensureMsgf(false, TEXT("UIrisObjectReferencePackageMap::SerializeObject, failed to read object reference index %u is out of bounds. Current ObjectReference num: %u"), ReadIndex, References->Num());
			return false;
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------
// SerializeName —— FName 劫持（可被 CVar 关闭）
// ------------------------------------------------------------------------------------------
// 如果 CVar 关闭，退化为父类（UPackageMap）的旧字符串序列化；否则与 SerializeObject 同构：
// 写下标 / 读下标。
// ------------------------------------------------------------------------------------------
bool UIrisObjectReferencePackageMap::SerializeName(FArchive& Ar, FName& InName)
{
	using namespace UE::Net;

	UE::Net::FIrisPackageMapExports* PackageMapExports = Ar.IsSaving() ? PackageMapExportsForWriting : PackageMapExportsForReading;

	// CVar 关闭时退化为旧字符串路径——保留紧急回退能力。
	if (!bEnableIrisPackageMapNameExports)
	{
		return Super::SerializeName(Ar, InName);
	}

	if (!ensure(PackageMapExports))
	{
		return false;
	}

	FIrisPackageMapExports::FNameArray* Names = &PackageMapExports->Names;
	if (Ar.IsSaving())
	{
		// 写侧：去重 + 追加 + 写下标。
		int32 Index;
		if (!Names->Find(InName, Index))
		{
			Index = Names->Add(InName);
		}
		uint32 IndexToWrite = static_cast<uint32>(Index);
		Ar.SerializeIntPacked(IndexToWrite);
	}
	else
	{
		// 读侧：读下标 + 越界校验 + 反查。
		uint32 ReadIndex;
		Ar.SerializeIntPacked(ReadIndex);
		if (Names->IsValidIndex(ReadIndex))
		{
			InName = (*Names)[ReadIndex];
		}
		else
		{
			ensureMsgf(false, TEXT("UIrisObjectReferencePackageMap::SerializeName, failed to read name index %u is out of bounds. Current Name num: %u"), ReadIndex, Names->Num());
			return false;
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------
// InitForRead / InitForWrite
// ------------------------------------------------------------------------------------------
// 配套 Read/Write Scope 调用。InitForWrite 会主动 Reset() 一次，确保从空开始捕获；
// InitForRead 不重置（其语义只是把"只读源"挂上去）。
// ------------------------------------------------------------------------------------------
void UIrisObjectReferencePackageMap::InitForRead(const UE::Net::FIrisPackageMapExports* InPackageMapExports, const UE::Net::FNetTokenResolveContext& InNetTokenResolveContext)
{ 
	// 接口接受 const 但内部存非常量（共享存储），所以 const_cast。读侧不会写 References/Names，
	// 实际不变性靠 SerializeObject/SerializeName 中的分支保证。
	PackageMapExportsForReading = const_cast<UE::Net::FIrisPackageMapExports*>(InPackageMapExports);
	NetTokenResolveContext = InNetTokenResolveContext;
}

void UIrisObjectReferencePackageMap::InitForWrite(UE::Net::FIrisPackageMapExports* InPackageMapExports)
{
	PackageMapExportsForWriting = InPackageMapExports;
	if (ensureMsgf(PackageMapExportsForWriting, TEXT("UIrisObjectReferencePackageMap requires valid PackageMapExports to capture exports.")))
	{
		// 写之前一律清空：防止跨调用残留导致索引错位。
		PackageMapExportsForWriting->Reset();
	}
}

namespace UE::Net
{

// ------------------------------------------------------------------------------------------
// FIrisObjectReferencePackageMapReadScope
// ------------------------------------------------------------------------------------------
FIrisObjectReferencePackageMapReadScope::FIrisObjectReferencePackageMapReadScope(UIrisObjectReferencePackageMap* InPackageMap, const UE::Net::FIrisPackageMapExports* PackageMapExports, const UE::Net::FNetTokenResolveContext* NetTokenResolveContext)
: PackageMap(InPackageMap)
{
	if (PackageMap)
	{
		PackageMap->InitForRead(PackageMapExports, *NetTokenResolveContext);
	}
}

FIrisObjectReferencePackageMapReadScope::~FIrisObjectReferencePackageMapReadScope()
{
	if (PackageMap)
	{
		// 只清"读侧"的指针；不要顺带清"写侧"，那是另一段 Scope 的职责。
		PackageMap->PackageMapExportsForReading = nullptr;
	}
}

// ------------------------------------------------------------------------------------------
// FIrisObjectReferencePackageMapWriteScope
// ------------------------------------------------------------------------------------------
// 严格防御：构造时如果发现 PackageMap 当前的"读 Exports == 写 Exports"——这意味着同一个
// buffer 既要充当读源又要充当写槽，逻辑上一定错了，直接 ensure 并把写槽置空。
// ------------------------------------------------------------------------------------------
FIrisObjectReferencePackageMapWriteScope::FIrisObjectReferencePackageMapWriteScope(UIrisObjectReferencePackageMap* InPackageMap, UE::Net::FIrisPackageMapExports* PackageMapExports)
: PackageMap(InPackageMap)
{
	if (!PackageMap)
	{
		return;
	}

	if (PackageMapExports && ensureMsgf(PackageMap->PackageMapExportsForReading != PackageMapExports, TEXT("FIrisObjectReferencePackageMapWriteScope cannot read and write from the same FIrisPackageMapExports as we are reading from.")))
	{
		// 写槽进入前先 Reset，确保索引从 0 开始。
		PackageMapExports->Reset();
		PackageMap->PackageMapExportsForWriting = PackageMapExports;
	}
	else
	{
		// 校验失败时把写槽置空——后续 SerializeObject/SerializeName 会通过 ensure 失败短路。
		PackageMap->PackageMapExportsForWriting = nullptr;
	}
}

FIrisObjectReferencePackageMapWriteScope::~FIrisObjectReferencePackageMapWriteScope()
{
	if (PackageMap)
	{
		// 离开作用域：清写槽指针，避免野指针残留到下一次调用。
		PackageMap->PackageMapExportsForWriting = nullptr;
	}
}

} // UE::Net


