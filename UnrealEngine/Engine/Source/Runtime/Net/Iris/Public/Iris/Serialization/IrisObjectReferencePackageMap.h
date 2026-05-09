// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =============================================================================================
// IrisObjectReferencePackageMap.h
// ---------------------------------------------------------------------------------------------
// UIrisObjectReferencePackageMap：劫持 FArchive 老式 NetSerialize 路径用的"假 PackageMap"。
//
// 解决的问题：
//   - 引擎里大量旧代码（USTRUCT 的 NetSerialize、第三方插件的 NetSerialize 等）通过
//     FArchive::SerializeObject / SerializeName 这种"老 API"来发对象引用和 FName。
//   - 而 Iris 已经迁到了"集中导出 + 索引引用"的新模型，这些老代码并不知道 Iris 的体系。
//   - 我们让这些老代码继续用老 API，但底层接的 PackageMap 换成本类——它会把
//     UObject*、FName 都"捕获"到一个外部的 FIrisPackageMapExports 容器，仅在 BitStream
//     里写入数组下标（IntPacked），等 NetSerializer 路径退出后再让 Iris 自己去发实际数据。
//   - 反序列化对偶：BitStream 里读到下标，再从 FIrisPackageMapExports 反查出真实的对象/Name。
//
// FIrisPackageMapExports 数据结构：
//   - References：捕获到的 UObject* 列表（TInlineAllocator<4> 性能友好）；
//   - Names     ：捕获到的 FName 列表（同上）。
//
// FIrisObjectReferencePackageMapReadScope / WriteScope：
//   - 必须成对使用且不可嵌套同一 PackageMap：进入作用域时把 PackageMap 与"读侧 / 写侧"
//     的 FIrisPackageMapExports 绑定，离开时清空，防止跨 batch 数据污染。
//   - 写侧 Scope 还会 Reset() 一遍 Exports（保证开始时是空的）；
//   - 写侧 Scope 在构造时 ensure "读用的 Exports 不等于写用的 Exports"——杜绝同一 buffer
//     同时既读又写。
//
// 与文档对照：Docs/Modules/Serialization.md §1.4「引用/PackageMap」"PackageMap 劫持"。
// =============================================================================================

#include "UObject/CoreNet.h"
#include "Net/Core/NetToken/NetToken.h"
#include "IrisObjectReferencePackageMap.generated.h"

// Forward declarations
class FNetworkGUID;
class UIrisObjectReferencePackageMap;

namespace UE::Net
{
	class FIrisObjectReferencePackageMapWriteScope;
	class FIrisObjectReferencePackageMapReadScope;

	// In order to properly capture exported data when calling in to old style NetSerialize methods
	// we need to capture and inject certain types.
	/**
	 * "捕获容器"：在调用老式 NetSerialize 时由 PackageMap 写入或读取，
	 * 由上层 IrisPackageMapExportUtil 进一步 Quantize/Serialize。
	 */
	struct FIrisPackageMapExports
	{
		/** 捕获到的 UObject* 列表类型。TInlineAllocator<4> 减少堆分配（多数引用 ≤4）。 */
		typedef TArray<TObjectPtr<UObject>, TInlineAllocator<4>> FObjectReferenceArray;
		/** 捕获到的 FName 列表类型。 */
		typedef TArray<FName, TInlineAllocator<4>> FNameArray;

		/** 是否完全为空（无引用、无 Name）。空时整个 Exports 段在位流上可被压缩跳过。 */
		bool IsEmpty() const
		{
			return References.IsEmpty() && Names.IsEmpty();
		}

		/** 清空两个数组。每次开始一段新的捕获前必须调用。 */
		void Reset()
		{
			References.Reset();
			Names.Reset();
		}

		/** 捕获到的对象引用。下标即位流里写入的 IntPacked 值。 */
		FObjectReferenceArray References;
		/** 捕获到的 FName。下标即位流里写入的 IntPacked 值。 */
		FNameArray Names;
	};

	// Scope that calls InitForRead on target PackageMap and invalidates set PackageMapExports on scope exit.
	/**
	 * RAII：读侧作用域。构造时把目标 PackageMap 与"只读 Exports"绑定，析构时清除绑定。
	 * 在作用域内的所有 NetSerialize 反序列化调用，老 API 都会走到本 PackageMap，
	 * 把"读到的索引"映射回真实的 UObject* / FName。
	 */
	class FIrisObjectReferencePackageMapReadScope
	{
	public:
		IRISCORE_API FIrisObjectReferencePackageMapReadScope(UIrisObjectReferencePackageMap* PackageMap, const UE::Net::FIrisPackageMapExports* PackageMapExports, const UE::Net::FNetTokenResolveContext* NetTokenResolveContext);
		IRISCORE_API ~FIrisObjectReferencePackageMapReadScope();
		/** 暴露被托管的 PackageMap 给调用方传给 FArchive。 */
		UIrisObjectReferencePackageMap* GetPackageMap() { return PackageMap; }

	private:
		UIrisObjectReferencePackageMap* PackageMap = nullptr;
	};

	// Scope that calls InitForWrite on target PackageMap and invalidates set PackageMapExports on scope exit.
	/**
	 * RAII：写侧作用域。构造时把"可写 Exports"挂到 PackageMap 上并 Reset()，
	 * 析构时清除绑定。注意：构造时会 ensure "PackageMap 当前的读 Exports != 写 Exports"——
	 * 不允许同一 buffer 同时充当读源与写槽。
	 */
	class FIrisObjectReferencePackageMapWriteScope
	{
	public:
		IRISCORE_API FIrisObjectReferencePackageMapWriteScope(UIrisObjectReferencePackageMap* PackageMap, UE::Net::FIrisPackageMapExports* PackageMapExports);
		IRISCORE_API ~FIrisObjectReferencePackageMapWriteScope();
		/** 暴露被托管的 PackageMap。 */
		UIrisObjectReferencePackageMap* GetPackageMap() { return PackageMap; }

	private:
		UIrisObjectReferencePackageMap* PackageMap = nullptr;
	};
}

/**
 * Custom packagemap implementation used to be able to capture exports such as UObject* references, names and NetTokens from external serialization.
 * Exports written when using this packagemap will be captured in an array and serialized as an index.
 * When reading using this packagemap exports will be read as an index and resolved by picking the corresponding entry from the provided array containing the data associated with the export.
 *
 * 自定义 UPackageMap：用于在调用老式 NetSerialize 时捕获 UObject* 引用、FName 和 NetToken 等导出数据。
 * 写侧：捕获到的导出会被存入数组，位流上只写入索引（IntPacked）；
 * 读侧：从位流读取索引，再到调用方提供的 Exports 数组里取对应数据完成重组。
 */
UCLASS(transient, MinimalAPI)
class UIrisObjectReferencePackageMap : public UPackageMap
{
public:
	GENERATED_BODY()

	// We override SerializeObject in order to be able to capture object references
	/** 重写：劫持对象引用序列化。写时把 Obj 加入 Exports.References 并写入下标；
	 *  读时按下标从 Exports.References 里取出 Obj。 */
	virtual bool SerializeObject(FArchive& Ar, UClass* InClass, UObject*& Obj, FNetworkGUID* OutNetGUID) override;

	// Override SerializeName in order to be able to capture name and serialize them with iris instead.
	/** 重写：劫持 FName 序列化。可由 CVar `net.iris.EnableIrisPackageMapNameExports` 关闭，
	 *  关闭后退化为 Super 的旧字符串实现。 */
	virtual bool SerializeName(FArchive& Ar, FName& InName);

	/** NetToken 解析上下文：内部 NetTokenStore 在解 RemoteToken 时使用。 */
	virtual const UE::Net::FNetTokenResolveContext* GetNetTokenResolveContext() const  override { return &NetTokenResolveContext; }

	// Init for read, we need to set the exports from which we are going to read our data.
	/** 进入"读"模式：设置只读 Exports 容器以及 NetTokenResolve 上下文。 */
	IRISCORE_API void InitForRead(const UE::Net::FIrisPackageMapExports* PackageMapExports, const UE::Net::FNetTokenResolveContext& InNetTokenResolveContext);

	// Init for write, all captured exports will be serialized as in index and added to the PackageMapExports for later export using iris.
	/** 进入"写"模式：所有捕获到的导出都会以索引方式写入位流，并加入到该 Exports 容器，
	 *  供后续 Iris 路径真正发出。会同时 Reset() 一遍。 */
	IRISCORE_API void InitForWrite(UE::Net::FIrisPackageMapExports* PackageMapExports);

private:
	// 只允许配套的 RAII Scope 触碰内部字段，保证生命周期不被外部错误地长期持有。
	friend UE::Net::FIrisObjectReferencePackageMapReadScope;
	friend UE::Net::FIrisObjectReferencePackageMapWriteScope;

	/** 只读模式下的 Exports 源（需要去掉 const 因为同一接口要兼容写）。 */
	UE::Net::FIrisPackageMapExports* PackageMapExportsForReading = nullptr;
	/** 写入模式下的 Exports 槽。 */
	UE::Net::FIrisPackageMapExports* PackageMapExportsForWriting = nullptr;
	/** 当前关联的 NetToken 解析上下文（由 InitForRead 写入）。 */
	UE::Net::FNetTokenResolveContext NetTokenResolveContext;
};
