// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetRefHandle.h
// 模块：Iris ReplicationSystem（L4 业务核心）
// 角色：Iris 体系中所有"网络对象"的底层身份标识（ID 类型）。
//
// 设计要点：
//   * 64-bit 紧凑布局：低 54 bit 存 Id，高 10 bit 存 ReplicationSystemId（支持 PIE 多实例）。
//   * Id 的最低位（StaticIdMask=1）用作 static / dynamic 区分位：
//       - 奇数 Id  -> static handle  （由蓝图/关卡放置等"稳定命名"对象使用）
//       - 偶数 Id  -> dynamic handle （由运行时动态生成对象使用）
//   * ReplicationSystemId 内部从 1 开始保存，0 表示"未绑定到任何 RS"，
//     这样 Value==0 即可作为 InvalidValue 的快速判定。
//   * 对应旧体系 FNetworkGUID，但是 64-bit 且能区分 ReplicationSystem 来源，
//     与 NetCore 的 FNetHandle（更通用，跨子系统）保持互补关系。
// 与之相关：
//   * FNetRefHandleManager（私有）通过 RefHandleToInternalIndex 将外部 Handle 映射到
//     内部紧凑索引 FInternalNetRefIndex；FNetRefHandle 在跨连接 / 序列化 / 引用缓存中流转。
//   * FArchive 序列化只携带 Id（不带 ReplicationSystemId），由接收端在
//     Private::FNetRefHandleManager::MakeNetRefHandleFromId 重建。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Iris/IrisConfig.h"
#include "Containers/StringFwd.h"
#include "Templates/TypeHash.h"

// Forward declarations
class FString;

namespace UE::Net::Private
{
	class FNetRefHandleManager;
}

namespace UE::Net
{

/**
 * FNetRefHandle
 *
 * Iris 中表征一个"已注册到某个 ReplicationSystem 的可复制对象"的不透明 ID。
 *
 * 字段布局（union）：
 *   位 [0..53]   Id                    -- 54 bit
 *   位 [54..63]  ReplicationSystemId+1 -- 10 bit（保存时为 RS Id+1，0 留作"未绑定"占位）
 *
 * 状态判定：
 *   IsValid()           : Value != 0，即至少有 Id 部分有效
 *   IsCompleteHandle()  : 同时持有 Id 与 RS Id（可在多 RS 环境下精确路由）
 *   IsStatic()/IsDynamic(): 通过最低位区分
 *
 * 相等比较：仅比较 Id（因为同一 Id 在不同 RS 内不会冲突，
 *           但 FullCompare 比较完整 Value 以严格区分 RS）。
 */
class FNetRefHandle
{
public:
	
	/** 返回一个全 0 的非法句柄；常用于初始化 / 哨兵值。 */
	inline static FNetRefHandle GetInvalid() { return FNetRefHandle(); }

private:
	enum { InvalidValue = 0 };                  // 整个 64-bit 全 0 视为无效
	enum { IdBits = 54 };                       // Id 占用的位数
	enum { ReplicationSystemIdBits = 10 };      // ReplicationSystemId 占用的位数（最多 1024 个 RS）

public:

	/** 单台进程允许的最大 RS Id（受 10 bit 限制，再扣掉 0 这个保留占位）。 */
	static constexpr uint64 MaxReplicationSystemId = (1ULL << ReplicationSystemIdBits) - 1;
	/** 单台进程允许同时存在的 ReplicationSystem 数量上限。 */
	static constexpr uint64 MaxReplicationSystemCount = MaxReplicationSystemId + 1;

	/** 默认构造产生无效句柄。 */
	FNetRefHandle() : Value(InvalidValue) {}

	/** 取得 54-bit Id 部分（用于序列化、Hash、Trace）。 */
	uint64 GetId() const { return Id; }
	/** 取得 0-based 的 ReplicationSystemId（内部存储的是 RS Id+1）。 */
	uint32 GetReplicationSystemId() const { check(ReplicationSystemId != 0); return (uint32)(ReplicationSystemId - 1); }
	/** 是否为有效句柄（Id 部分至少非零）。 */
	bool IsValid() const { return Value != InvalidValue; }

	/** Does the handle know which ReplicationSystem it is related to. */
	/** 是否为"完整句柄"——既有 Id 又关联到具体的 ReplicationSystem。 */
	bool IsCompleteHandle() const { return Value != InvalidValue && ReplicationSystemId != 0U; }

	/** Static handles have ODD Id's */
	/** 静态句柄：Id 最低位为 1（关卡放置、Asset 引用等稳定命名对象）。 */
	bool IsStatic() const { return Id & StaticIdMask; }

	/** Dynamic handles have EVEN Id's */
	/** 动态句柄：有效且最低位为 0（运行时动态生成 Actor / Component 等）。 */
	bool IsDynamic() const { return IsValid() && !IsStatic(); }

	// 仅比较 Id（足以唯一定位对象，跨 RS 时通常借助 IsCompleteHandle 路由）。
	bool operator==(const FNetRefHandle& Other)const { return Id == Other.Id; }
	bool operator<(const FNetRefHandle& Other)const { return Id < Other.Id; }
	bool operator!=(const FNetRefHandle& Other)const { return Id != Other.Id; }

	/** 输出 "NetRefHandle (Id=...):(RepSystemId=...)" 详细字串。 */
	IRISCORE_API FString ToString() const;
	/** 输出紧凑字串 "Id:RepSystemId"（日志友好）。 */
	IRISCORE_API FString ToCompactString() const;

	/** 严格全字段比较（包含 RS Id），用于诊断/精准匹配。 */
	static bool FullCompare(FNetRefHandle A, FNetRefHandle B) { return A.Value == B.Value; }
	
private:
	friend uint32 GetTypeHash(const FNetRefHandle& Handle);
	friend Private::FNetRefHandleManager;

	static constexpr uint64 StaticIdMask = 1;                 // 最低位 = static 标志
	static constexpr uint64 IdMask = (1ULL << IdBits) - 1;    // 54-bit Id 掩码

	// union 实现紧凑 64-bit 视图：可按位段访问 Id/RsId，也可整体比较。
	union 
	{
		struct
		{
			uint64 Id : IdBits;										// Id, lowest bit indicates if the handle is static or dynamic
			uint64 ReplicationSystemId : ReplicationSystemIdBits;	// ReplicationSystemId, when running in pie, we track the owning instance
		};
		uint64 Value;  // 整体 64-bit 视图，便于 Invalid 判定与 FullCompare
	};
};

/** TMap/TSet 友元 Hash：仅基于 Id（Hash 不区分 RS）。 */
inline uint32 GetTypeHash(const FNetRefHandle& Handle)
{
	return ::GetTypeHash(Handle.GetId());
}

/** NetTrace 用：将 FNetRefHandle 编码为 64-bit 对象 ID。 */
inline uint64 GetObjectIdForNetTrace(const FNetRefHandle& Handle)
{
	return Handle.GetId();
}

}

// 字符串 Builder / Archive 序列化重载（详见 NetRefHandle.cpp）。
// FArchive 重载只持久化 IsValid + Id，反序列化时通过 FNetRefHandleManager::MakeNetRefHandleFromId 重建。
IRISCORE_API FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const UE::Net::FNetRefHandle& NetHandle);
IRISCORE_API FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const UE::Net::FNetRefHandle& NetHandle);
IRISCORE_API FArchive& operator<<(FArchive& Ar, UE::Net::FNetRefHandle& RefHandle);
