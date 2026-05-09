// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   FObjectReferenceCache 是 Iris 复制系统中“UObject 网络引用”的中央缓存 / 注册中心,
//   定位与旧 NetGUIDCache + ClientPackageMap 等价, 但是基于:
//     - FNetRefHandle  (动态对象 / 服务器分配的稳定 ID)
//     - FNetToken      (字符串相对路径, 由 FStringTokenStore 压缩)
//     - 一个 OuterNetRefHandle 链 (路径相对于 outer 表达, 类似 UObject 的 outer 链)
//
//   职责概览:
//     1) 服务端 (Authority):
//        - 为任意可复制 UObject 分配 FNetRefHandle (动态 ID 自增)。
//        - 当对象 IsFullNameStableForNetworking()/IsNameStableForNetworking() 时, 把 名字 / 路径
//          注册成 NetToken, 让客户端可以按路径 Find/Load。
//        - 提供 WriteFullReference / WritePendingExports 把"未导出"引用对客户端正向导出。
//
//     2) 客户端 (Non-authority):
//        - ReadFullReference / ReadExports 读到的引用先入缓存(FCachedNetObjectReference),
//          但暂不立即 Resolve 真实 UObject。
//        - 实际 Resolve 时调用 ResolveObjectReferenceHandleInternal, 包含:
//             a. 递归解析 outer chain (path 必须依赖 outer 已加载)
//             b. PIE 重映射 (RenamePathForPie)
//             c. 异步加载触发 (StartAsyncLoadingPackage), 加载完成后 AsyncPackageCallback
//                统一回填 bIsPending = false 并解锁等待该包的所有 NetRefHandle
//             d. 处理同名重复条目 (level streaming 重新加载同名包) 的对象映射策略 (取较大 Id)
//
//     3) 与 NetExports 协作的导出状态:
//        - 每条 batch 写入时, NetExportContext 记录 “此连接已导出过哪些 RefHandle / NetToken”,
//          ACK 后由 NetExports 模块判定可以从 PendingExports 中清除。
//        - 客户端必须严格按导出顺序读到 outer 才能 Resolve 子对象, 否则进入 PendingBatch 队列。
//
//   关键内存数据结构:
//     - ObjectToNetReferenceHandle : 原生指针 -> 当前 RefHandle (反查)
//     - ReferenceHandleToCachedReference: RefHandle -> 元数据 (包括 outer/path/标志位)
//     - HandleToDynamicOuter       : 跟踪 “动态 outer 被销毁时一并清理的子引用”
//     - QueuedBatchObjectReferences: 在异步加载等待期间, 阻止 GC 释放已绑定对象
//     - PendingAsyncLoadRequests   : 包名 -> 当前正在异步加载的请求 (合并多 RefHandle)
//
//   核心 API:
//     - GetOrCreateObjectReference(Object/Path, Outer)
//     - ResolveObjectReference()        (服务/客户端都用)
//     - WriteFullReference / ReadFullReference (跨连接初次导出)
//     - WriteReference     / ReadReference     (已导出后的轻量写读)
//     - WritePendingExports / ReadExports      (per-batch 导出/接收)
//
//   与 ReplicationSystem.md §2.7 (对象引用与弱引用) 一致, 是 FObjectNetSerializer 的底层支持。
// =====================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Map.h"
#include "ObjectReferenceCacheFwd.h"
#include "UObject/ObjectPtr.h"

enum class EIrisAsyncLoadingPriority : uint8;

namespace UE::Net
{
	class FStringTokenStore;
	class FNetTokenStore;
	class FNetSerializationContext;
	struct FObjectNetSerializerQuantizedReferenceStorage;
	namespace Private
	{
		class FNetRefHandleManager;
		class FNetExportContext;
		struct FPendingBatchHolder;
		typedef uint32 FInternalNetRefIndex;
	}
}

namespace UE::Net::Private
{

// A lot of code in this class is extracted from the re-factored GUIDCache/ClientPackageMap and must be kept up to sync once they are submitted
// Hopefully we can merge parts of this back together later on

/**
 * Iris 复制系统的“对象引用缓存”。
 *
 * 设计要点 (中文):
 *   - 服务端 (bIsAuthority = true): 唯一可以创建新 FNetRefHandle 的角色; 客户端只能通过
 *     接收 batch 中的 ExportData 被动注册引用 (BindRemoteReference / ReadFullReferenceInternal)。
 *   - 静态对象 (路径稳定): 通过 NetToken 把字符串路径压缩传输, 客户端按路径 Find/Load
 *     (异步 or 同步, 见 ShouldAsyncLoad)。
 *   - 动态对象 (运行时创建): 仅依赖 FNetRefHandle, 由服务端 Spawn 时分配, 客户端通过
 *     ObjectReplicationBridge::OnInstantiated 等回调 Bind 到真实 UObject。
 *   - PIE 重映射: 编辑器多实例 PIE 时同一资源会属于不同 UWorld, 所有路径在 Read/Write 时
 *     都需经过 RenamePathForPie() 修正。
 *   - 异步加载: 接收方读到 must-be-mapped exports 后, 若该包尚未加载, 进入异步加载队列;
 *     在 AsyncPackageCallback 完成前对应的 cached reference 会被标记 bIsPending = true,
 *     依赖该 RefHandle 的 batch 也会被 NetPendingBatches 暂缓应用。
 */
class FObjectReferenceCache
{
public:

	FObjectReferenceCache();

	/** 绑定到 ReplicationSystem, 抓取 NetTokenStore / Bridge / NetRefHandleManager 引用; bIsAuthority = ReplicationSystem->IsServer()。*/
	void Init(UReplicationSystem* ReplicationSystem);

	/** 判断对象是否为动态对象(非稳定网络名), 等同于旧 NetGUIDCache 中的 IsDynamicObject。*/
	// Determine if the object is dynamic
	bool IsDynamicObject(const UObject* Object) const;

	/** 是否拥有“创建新 NetRefHandle 的权威”——只有服务端为 true。*/
	// Are we allowed to create new NetHandles to reference objects?
	bool IsAuthority() const;

	/** 服务端: 为对象分配 (或返回已有的) NetRefHandle。客户端调用会失败。*/
	// Create and assign a new NetHandle to the object
	FNetRefHandle CreateObjectReferenceHandle(const UObject* Object);

	/**
	 * 服务端: 在对象正式被加入复制系统之前“预注册” NetRefHandle, 设置 bIsPreRegistered = true。
	 * 用于一些必须先有稳定 ID 再走完整 BeginReplication 流程的情况(如显式提前广播 RPC)。
	 */
	// Create and assign a new pre-registered NetHandle to the object
	FNetRefHandle PreRegisterObjectReferenceHandle(const UObject* Object);

	/**
	 * 反查: 根据原生 UObject* 找到当前缓存的 RefHandle。
	 * 缓存中可能存在“同对象指针被复用”导致的脏条目, 函数会顺手清理掉与 weak ptr 不一致的项。
	 */
	// Get existing handle for object
	FNetRefHandle GetObjectReferenceHandleFromObject(const UObject* Object, EGetRefHandleFlags GetRefHandleFlags = EGetRefHandleFlags::None) const;

	/** 仅在缓存命中时返回对象, 不会触发 Find/Load。等价旧实现的非阻塞 GetObjectFromNetGUID。*/
	// Get object from handle, only if the object is in the cache.
	UObject* GetObjectFromReferenceHandle(FNetRefHandle RefHandle);

	/** 同上, 但仅当条目被 PreRegister 标记时才返回对象, 用于 “尚未 BeginReplication” 阶段的查询。*/
	// Get object from handle, only if the object is in the cache and only if it was registered via PreregisterObjectReferenceHandle or AddPreregisteredReference.
	UObject* GetPreRegisteredObjectFromReferenceHandle(FNetRefHandle RefHandle);

	/**
	 * 解析: 若对象已驻留则直接返回; 否则按需 Find/异步 Load (取决于 bNoLoad / ShouldAsyncLoad / bForceSyncLoad)。
	 * 失败时返回 nullptr, 并把 bIsBroken/bIsPending 等状态写回缓存以避免反复刷屏日志。
	 */
	// Try to resolve the object reference and try to load it if the object cannot be found
	UObject* ResolveObjectReferenceHandle(FNetRefHandle RefHandle, const FNetObjectResolveContext& ResolveContext);

	/** 解析 FNetObjectReference (含 path-token + handle 组合 / 客户端→服务端反向引用); 重载: 一次性给出结果与“是否含未解析”状态码。*/
	UObject* ResolveObjectReference(const FNetObjectReference& ObjectRef, const FNetObjectResolveContext& ResolveContext);
	ENetObjectReferenceResolveResult ResolveObjectReference(const FNetObjectReference& ObjectRef, const FNetObjectResolveContext& ResolveContext, UObject*& OutResolvedObject);

	/** 是否标记为 broken (上次 Resolve 已失败且不期望恢复)。bMustBeRegistered = true 时未注册 == broken。*/
	// Returns true of this NetRefHandle is marked as broken
	bool IsNetRefHandleBroken(FNetRefHandle Handle, bool bMustBeRegistered) const;

	/**
	 * 沿 outer 链向上查找, 只要任意一层处于 bIsPending(异步加载等待)或在 PendingBatchHolder 队列中, 则视为 pending。
	 * 用于 NetPendingBatches 决定是否还要继续等。
	 */
	// Returns true of the provided NetRefHandle or one of its outers is pending async loading.
	bool IsNetRefHandlePending(FNetRefHandle NetRefHandle, const FPendingBatchHolder& PendingBatchHolder) const;

	/** 是否经由 PreRegisterObjectReferenceHandle / AddPreregisteredReference 注册。*/
	// Returns true of the provided NetRefHandle was assigned via PreRegisterObjectReferenceHandle or AddPreregisteredReference.
	bool IsNetRefHandlePreRegistered(FNetRefHandle NetRefHandle) const;

	/** 沿 outer 链向上找到第一个“正在被复制”的祖先对象的引用 (用于 NetSerializer 的 outer 关联)。*/
	// Find replicated outer
	FNetObjectReference GetReplicatedOuter(const FNetObjectReference& Reference) const;

	/** 服务端常用入口: 为 Instance 创建/返回 NetObjectReference (带 path 与 outer)。*/
	// Get or create a NetObjectReference from the object
	FNetObjectReference GetOrCreateObjectReference(const UObject* Instance);

	/**
	 * 仅服务端: 由 “相对于 Outer 的字符串路径” 创建一个 path-only 的 reference (不含真实对象)。
	 * 适用于“引用尚未实例化但路径可寻址的对象”(如 sublevel 中的可能未加载对象)。
	 */
	// Get or create a NetObjectReference from the object identifed by path relative to outer
	FNetObjectReference GetOrCreateObjectReference(const FString& ObjectPath, const UObject* Outer);

	/** 客户端: 把接收到的 RefHandle 与刚 Spawn 完成的本地 UObject 绑定, 同时刷新缓存条目。*/
	// Bind a nethandle and the object reference cache on the client
	void BindRemoteReference(FNetRefHandle RefHandle, const UObject* Object);
	
	/** 客户端: 接收到的 PreRegistered 引用进行绑定 (见 PreRegisterObjectReferenceHandle)。*/
	// Add reference for pre-registered object (client)
	void AddPreRegisteredReference(FNetRefHandle RefHandle, const UObject* Object);

	/** 释放对动态对象的缓存条目, 静态对象为了支持销毁信息序列化只清空指针不删除条目。*/
	// Remove references to dynamic objects
	void RemoveReference(FNetRefHandle RefHandle, const UObject* Object);

	/** 写入“完整”引用 (递归写出 outer + path + handle), 用于初次导出, 客户端可据此独立解析。*/
	// Write full chain of object references for RefHandle
	void WriteFullReference(FNetSerializationContext& Context, FNetObjectReference Ref) const;

	/** 读取 WriteFullReference 写入的数据, 仅在缓存中登记元数据, 不立即 Resolve 真实对象。*/
	// Read/load full reference data, this will populate the cache on the receiving end, but will not try to resolve the actual objects
	void ReadFullReference(FNetSerializationContext& Context, FNetObjectReference& OutRef);

	/** 写入“轻量”引用 (只有 RefHandle, 假定已被导出)。*/
	// Write reference, the reference must already be exported
	void WriteReference(FNetSerializationContext& Context, FNetObjectReference Ref) const;
	
	/** 与 WriteReference 配对的读取。*/
	// Read reference, as written by WriteReference
	void ReadReference(FNetSerializationContext& Context, FNetObjectReference& OutRef);

	/** 把一组待导出的引用追加到当前 batch 的 pending exports 集合中 (供 WritePendingExports 处理)。*/
	// Add exports to the set of pending exports for the current batch being written
	void AddPendingExports(FNetSerializationContext& Context, TArrayView<const FNetObjectReference> ExportsView) const;

	/** 同上, 但接收量化后的 FObjectNetSerializer 存储格式 (FQuantizedObjectReference)。*/
	// Add exports to the set of pending exports for the current batch being written
	void AddPendingExports(FNetSerializationContext& Context, TArrayView<const FObjectNetSerializerQuantizedReferenceStorage> ExportsView) const;

	/** 单条引用版本。*/
	// Add export to the set of pending exports for the current batch being written
	void AddPendingExport(FNetExportContext& ExportContext, const FNetObjectReference& Reference) const;

	/** WritePendingExports 的写入结果。*/
	enum class EWriteExportsResult : unsigned
	{
		// 成功写入了至少一条 export
		// We did write exports
		WroteExports,

		// 比特流已溢出, 调用方需要回滚已写入数据 + 待导出列表
		// BitStream overflow.
		BitStreamOverflow,

		// 没有任何 export 需要写入
		// Some error occurred while serializing the object.
		NoExports,
	};

	/**
	 * 在 batch 头部写入“本帧新增导出 + must-be-mapped exports”。
	 * 调用者必须自行处理 BitStreamOverflow: 因为 export 是 batch 数据的一部分, 一旦溢出整个 batch 都要重发。
	 */
	// Exports are expected to be part of the written state, so if the result is a BitStreamOverflow
	// it is up to the caller to roll back written data and pending exports
	EWriteExportsResult WritePendingExports(FNetSerializationContext& Context, FInternalNetRefIndex ObjectIndex);

	/** 接收端读取 WritePendingExports 内容: NetToken 数据 + 完整引用导出 + must-be-mapped 列表。*/
	bool ReadExports(const FNetRefHandle& NetObjectHandle, FNetSerializationContext& Context, TArray<FNetRefHandle>* MustBeMappedExports, EIrisAsyncLoadingPriority& OutIrisAsyncLoadingPriority);

	/** 工具: 由 RefHandle 构造一个最小的 FNetObjectReference (无 path token)。*/
	static FNetObjectReference MakeNetObjectReference(FNetRefHandle Handle);

	/** Iris 异步加载模式, 与 PackageMapClient 等价。*/
	// Async interface, kept as close to possible to FNetGuidCache/PackageMapClient
	enum class EAsyncLoadMode : uint8
	{
		UseCVar			= 0,		// 使用 net.AllowAsyncLoading CVar 决定 / Use CVar (net.AllowAsyncLoading) to determine if we should async load
		ForceDisable	= 1,		// 强制关闭异步加载 (退化到阻塞 LoadPackage) / Disable async loading
		ForceEnable		= 2,		// 强制开启异步加载 (即便 CVar 关闭) / Force enable async loading
	};

	/** 设置异步加载模式; 内部会刷新 net.AllowAsyncLoading CVar 缓存。*/
	void SetAsyncLoadMode(const EAsyncLoadMode NewMode);
	/** 是否应该走异步加载路径 (综合 EAsyncLoadMode + bIrisAllowAsyncLoading + 调试 CVar)。*/
	bool ShouldAsyncLoad() const;


	/**
	 * GC 防护: 异步加载等待期间, 已经解析出来的对象可能没有别处引用; 这里把它们登记为
	 * “Iris 持有”, 让 GC 不会回收, 直到所有等待的 batch 处理完毕。
	 */
	// While async loading of pending must be mapped references we need to maintain references to already resolved objects as there will be no instance referencing them
	void AddReferencedObjects(FReferenceCollector& ReferenceCollector);
	/** Pending batch 期间为某 RefHandle 登记一个对象 (RefCount++)。*/
	void AddTrackedQueuedBatchObjectReference(const FNetRefHandle InHandle, const UObject* InObject);
	/** 异步加载完成后用最新对象指针刷新 QueuedBatchObjectReferences 中已登记条目。*/
	void UpdateTrackedQueuedBatchObjectReference(const FNetRefHandle InHandle, const UObject* NewObject);
	/** RefCount-- 至 0 时移除对应条目, 解除 GC 防护。*/
	void RemoveTrackedQueuedBatchObjectReference(const FNetRefHandle InHandle);

	/** 调试: 把 outer 链 + path 拼成可读字符串。*/
	FString DescribeObjectReference(const FNetObjectReference Ref, const FNetObjectResolveContext& ResolveContext);

	/** 返回缓存中存放的“相对路径字符串”(经 NetToken 解析); 找不到则返回 nullptr。*/
	/** Return the stored relative path of a replicated object if it's in the cache. */
	const TCHAR* GetObjectRelativePath(FNetRefHandle NetRefHandle) const;

private:
	/** 异步加载完成回调 (LoadPackageAsync 的 delegate)。 */
	void AsyncPackageCallback(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result);
	/** 调试用: 强制让一个加载请求失败 (CVar net.Iris.AsyncLoading.FailNextLoad / FailAllLoads / FailPackageName)。*/
	void AsyncPackageForcedFailCallback(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result);

	/**
	 * 单条引用缓存条目。
	 * 同时持有: 弱指针(防止 GC) + 原始指针快照 (作为查重 key) + outer 链 + 路径 NetToken + 状态标志。
	 *
	 * 静态对象 (RelativePath.IsValid() == true): 客户端可按路径 Find/Load。
	 * 动态对象 (RelativePath 通常无效): 客户端必须等待 Bridge 创建并 BindRemoteReference。
	 */
	struct FCachedNetObjectReference
	{
		// 真正的对象指针 (弱引用, 不阻止 GC)
		TWeakObjectPtr<UObject> Object;
		// 原始裸指针快照, 作为 ObjectToNetReferenceHandle 的反查 key, 也用于检测“对象被复用”
		const UObject* ObjectKey = nullptr;

		// 当前条目对应的 NetRefHandle
		// NetRefHandle
		FNetRefHandle NetRefHandle;

		// 相对于 OuterNetRefHandle 的路径(NetToken 索引)
		// RelativePath to outer
		FNetToken RelativePath;

		// 父对象的 NetRefHandle (递归构成 outer 链)
		// Ref to outer
		FNetRefHandle OuterNetRefHandle;

		// 状态标志位 (用 bit-field 节省内存)
		// Flags
		uint8 bNoLoad : 1 = false;				// 仅 Find 不 Load (例如 sublevel 内对象需等地图加载完毕) / Don't load this, only do a find
		uint8 bIgnoreWhenMissing : 1 = false;	// 解析失败不报警告(动态对象/不允许 client load 的对象)
		uint8 bIsPackage : 1 = false;			// 顶层 UPackage (没有 outer 的 static 对象)
		uint8 bIsBroken : 1 = false;			// 已确认无法解析, 不再尝试, 避免日志刷屏
		uint8 bIsPending : 1 = false;			// 正在异步加载中, 等待 AsyncPackageCallback
		uint8 bIsPreRegistered : 1 = false;		// 经 PreRegisterObjectReferenceHandle/AddPreregisteredReference 注册 / True if assigned via PreregisterObjectReferenceHandle or AddPreregisteredReference
	};

	/**
	 * 异步加载等待期对“已解析对象”的 GC 防护条目。
	 * 多次入队同一 RefHandle 时通过 RefCount 计数。
	 */
	struct FQueuedBatchObjectReference
	{
		TObjectPtr<const UObject> Object = nullptr;
		uint32 RefCount = 0U;
	};

	/**
	 * 单个异步加载请求 (一个包名对应一个请求, 但可能有多个 NetRefHandle 引用了同一个包,
	 * 通过 Merge() 合并到同一请求, 完成回调时统一处理)。
	 */
	struct FPendingAsyncLoadRequest
	{
		FPendingAsyncLoadRequest(FNetRefHandle InNetRefHandle, double InRequestStartTime);
		void Merge(const FPendingAsyncLoadRequest& Other);
		void Merge(FNetRefHandle InNetRefHandle);

		// NetRefHandles that requested loading for the same UPackage
		TArray<FNetRefHandle, TInlineAllocator<4>> NetRefHandles;
		// 发起请求的时间戳 (秒, ReplicationSystem->GetElapsedTime), 可用于统计等待时间
		double RequestStartTime;
	};

	/** 真正的引用创建实现 (供 CreateObjectReferenceHandle / GetOrCreateObjectReference 调用)。*/
	bool CreateObjectReferenceInternal(const UObject* Object, FNetObjectReference& OutReference);


	/** 递归读取完整引用 (含 outer 链)。RecursionCount 防御恶意构造的深度链导致栈溢出 (上限 16)。*/
	void ReadFullReferenceInternal(FNetSerializationContext& Context, FNetObjectReference& OutRef, uint32 RecursionCount);
	/** 与 ReadFullReferenceInternal 对应的写入。*/
	void WriteFullReferenceInternal(FNetSerializationContext& Context, const FNetObjectReference& Ref) const;

	/** ResolveObjectReferenceHandle 的真正实现, 额外输出“是否为 must-be-mapped” 给上层判断。*/
	UObject* ResolveObjectReferenceHandleInternal(FNetRefHandle RefHandle, const FNetObjectResolveContext& ResolveContext, bool& bOutMustBeMapped);
	bool IsDynamicInternal(const UObject* Object) const;
	/** 是否被复制系统支持 (网络稳定名 或 IsSupportedForNetworking)。*/
	bool SupportsObjectInternal(const UObject* Object) const;
	/** 客户端是否允许 Load 这个对象 (动态对象 / 包内 ContainsMap 的 level 不允许)。*/
	bool CanClientLoadObjectInternal(const UObject* Object, bool bIsDynamic) const;
	/** 是否在 Resolve 失败时静默处理 (主要给动态引用 + outer 仍在 pending 时减少噪声警告)。*/
	bool ShouldIgnoreWhenMissing(FNetRefHandle RefHandle) const;
	/**
	 * PIE 重映射: 在 PIE 多实例中同一资源会属于不同 UWorld, 写出/读入路径前都要把 PIE prefix
	 * 修正成对端可识别的形式; 仅当 GetPlayInEditorID() != -1 时才会调用 Bridge::RemapPathForPIE。
	 */
	bool RenamePathForPie(uint32 ConnectionId, FString& Str, bool bReading);

	/** 用于日志/调试: 把 RefHandle 的 outer 链拼成 "[h1](Name).[h2](Name)..." 形式。*/
	// Get the string path of RefHandle
	FString FullPath(FNetRefHandle RefHandle, const FNetObjectResolveContext& ResolveContext) const;
	void GenerateFullPath_r(FNetRefHandle RefHandle, const FNetObjectResolveContext& ResolveContext, FString& OutFullPath) const;

	/** 沿 outer 链向上查找最顶层的动态根 (用于 dynamic outer 销毁时清理子引用)。*/
	// Find dynamic root
	FNetRefHandle GetDynamicRoot(const FNetRefHandle Handle) const;

	static FNetObjectReference MakeNetObjectReference(FNetRefHandle RefHandle, FNetToken RelativePath);
	static FNetObjectReference MakeNetObjectReference(const FCachedNetObjectReference& CachedReference);

	/**
	 * 为每个写入的 batch 输出 “must be mapped” 列表(必须解析后才能应用 batch 的引用)。
	 * 接收端在 ReadMustBeMappedExports 中解析这些 RefHandle, 若无法立即解析则 batch 被
	 * 暂存到 NetPendingBatches, 等待异步加载完成。
	 */
	// Must be mapped exports are written for each batch that serializes object references, if async loading is enabled the client
	// will defer application of data contained in the batch until the must be mapped exports are resolvable.
	bool WriteMustBeMappedExports(FNetSerializationContext& Context, FInternalNetRefIndex ObjectIndex, TArrayView<const FNetObjectReference> ExportsView) const;
	void ReadMustBeMappedExports(const FNetRefHandle& NetObjectHandle, FNetSerializationContext& Context, TArray<FNetRefHandle>* MustBeMappedExports, EIrisAsyncLoadingPriority& OutIrisAsyncLoadingPriority);

	/** 启动一次新的异步包加载 (LoadPackageAsync), 把 RefHandle 加入 PendingAsyncLoadRequests。*/
	void StartAsyncLoadingPackage(FCachedNetObjectReference& Object, FName PackagePath, TAsyncLoadPriority AsyncLoadingPriority, const FNetRefHandle RefHandle, const bool bWasAlreadyAsyncLoading);
	/** 同名包已经在加载中: 把当前 RefHandle 合并到已有请求, 等待统一回调即可。*/
	void ValidateAsyncLoadingPackage(FCachedNetObjectReference& Object, FName PackagePath, const FNetRefHandle RefHandle);

private:

	// 反查表: 原生指针 -> 当前 RefHandle
	// 注意此处仅作快速查找, 真实有效性以 ReferenceHandleToCachedReference 中的 weak ptr 为准。
	// Map raw UObject pointer -> Handle
	// To verify that the reference is valid we need to check the weakpointer stored in the cache
	TMap<const UObject*, FNetRefHandle> ObjectToNetReferenceHandle;

	// 主缓存: RefHandle -> 元数据 (path/outer/标志位/弱指针)
	// Map ReferenceHandle -> CachedReference
	TMap<FNetRefHandle, FCachedNetObjectReference> ReferenceHandleToCachedReference;

	// 动态 outer -> 其下子引用 (动态 outer 销毁时一次性清理子引用以减少缓存污染)
	// To properly clean up stale references referencing dynamic objects we need to track them
	TMultiMap<FNetRefHandle, FNetRefHandle> HandleToDynamicOuter;
	
	UReplicationSystem* ReplicationSystem;
	UObjectReplicationBridge* ReplicationBridge;
	FNetTokenStore* NetTokenStore;
	FStringTokenStore* StringTokenStore;
	FNetRefHandleManager* NetRefHandleManager;
	
	// 当前是否为权威端 (服务端=true, 客户端=false)
	// Do we have authority to create references?
	uint32 bIsAuthority : 1;

	/**
	 * 异步加载等待期间维持对所有“已被引用”对象的 GC 防护; 否则 LoadPackageAsync 完成后
	 * 等待绑定的对象有可能被 GC 回收, 导致 batch 应用失败。
	 */
	/**
	 * Set of all current Objects that we've been requested to be referenced while we are doing async loading.
	 * This is used to prevent objects (especially async load objects,
	 * which may have no other references) from being GC'd while a the object is waiting for more
	 * pending references
	 */
	TMap<FNetRefHandle, FQueuedBatchObjectReference> QueuedBatchObjectReferences;

	// 当前异步加载策略 (UseCVar / ForceEnable / ForceDisable)
	EAsyncLoadMode AsyncLoadMode;
	// 缓存自 net.AllowAsyncLoading 的最近一次取值, 避免每帧查询 ConsoleManager
	bool bCachedCVarAllowAsyncLoading;

	/** 包名 -> 异步加载请求(可能聚合多个 RefHandle)。 */
	/** Set of packages that are currently pending async loads, referenced by package name. */
	TMap<FName, FPendingAsyncLoadRequest> PendingAsyncLoadRequests;

	// $TODO: $IRIS: Stats support
#if 0
	/** Store all GUIDs that caused the sync loading of a package, for debugging & logging with LogNetSyncLoads */
	//TArray<FNetRefHandle> SyncLoadedGUIDs;
	//FNetAsyncLoadDelinquencyAnalytics DelinquentAsyncLoads;
	//void ConsumeAsyncLoadDelinquencyAnalytics(FNetAsyncLoadDelinquencyAnalytics& Out);
	//const FNetAsyncLoadDelinquencyAnalytics& GetAsyncLoadDelinquencyAnalytics() const;
	//void ResetAsyncLoadDelinquencyAnalytics();	
	//bool WasGUIDSyncLoaded(FNetworkGUID NetGUID) const { return SyncLoadedGUIDs.Contains(NetGUID); }
	//void ClearSyncLoadedGUID(FNetworkGUID NetGUID) { SyncLoadedGUIDs.Remove(NetGUID); }
	/**
	 * If LogNetSyncLoads is enabled, log all objects that caused a sync load that haven't been otherwise reported
	 * by the package map yet, and clear that list.
	 */
	//void ReportSyncLoadedGUIDs();
#endif

};

inline UObject* FObjectReferenceCache::ResolveObjectReferenceHandle(FNetRefHandle RefHandle, const FNetObjectResolveContext& ResolveContext)
{
	bool bMustBeMapped;
	return ResolveObjectReferenceHandleInternal(RefHandle, ResolveContext, bMustBeMapped);
}

inline UObject* FObjectReferenceCache::ResolveObjectReference(const FNetObjectReference& ObjectRef, const FNetObjectResolveContext& ResolveContext)
{
	UObject* ResolvedObject = nullptr;
	ResolveObjectReference(ObjectRef, ResolveContext, ResolvedObject);
	return ResolvedObject;
}

inline FNetObjectReference FObjectReferenceCache::MakeNetObjectReference(FNetRefHandle Handle)
{
	return FNetObjectReference(Handle);
}

inline FNetObjectReference FObjectReferenceCache::MakeNetObjectReference(FNetRefHandle RefHandle, FNetToken RelativePath)
{
	const ENetObjectReferenceTraits Traits = RelativePath.IsValid() ? ENetObjectReferenceTraits::CanBeExported : ENetObjectReferenceTraits::None;
	return FNetObjectReference(RefHandle, RelativePath, Traits);
}

inline FNetObjectReference FObjectReferenceCache::MakeNetObjectReference(const FCachedNetObjectReference& CachedReference)
{
	const ENetObjectReferenceTraits Traits = CachedReference.RelativePath.IsValid() ? ENetObjectReferenceTraits::CanBeExported : ENetObjectReferenceTraits::None;
	return FNetObjectReference(CachedReference.NetRefHandle, FNetToken(), Traits);
}

}
