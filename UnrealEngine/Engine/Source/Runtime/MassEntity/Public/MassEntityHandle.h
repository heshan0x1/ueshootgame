// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityHandle.generated.h"

/**
 * A handle to a Mass entity. An entity is used in conjunction with the FMassEntityManager
 * for the current world and can contain lightweight fragments.
 *
 * 中文说明：
 *   FMassEntityHandle 是 Mass ECS 体系中"实体身份"的唯一对外表达。它本身不包含任何
 *   业务数据 —— 实体的所有 fragment/tag 都挂在 FMassEntityManager 维护的 archetype
 *   列式存储中，这里只用 (Index, SerialNumber) 两个 int32 组成的 8 字节 POD 值来定位。
 *
 *   设计要点：
 *     - Index：实体在 FMassEntityManager::Entities 数组里的槽位下标。槽位会被回收复用。
 *     - SerialNumber：每次槽位被重新分配时递增，用于检测"悬垂句柄"（旧的 handle 指向的
 *       槽位已经被新实体占用）。只有 Index 与 SerialNumber 同时匹配，句柄才指向合法实体。
 *     - 0 被保留为"未设置"状态。Index==0 且 SerialNumber==0 表示空句柄。
 *     - 严格 8 字节对齐，且大小刚好 8 字节，可直接当 uint64 传递（见 AsNumber/FromNumber）。
 *       这对跨线程发送、哈希表键、网络同步都很友好。
 *
 *   与 Actor/Component 模型的最大差异：它不是 UObject 指针，不参与 GC，也无法直接解引用；
 *   访问实体数据必须通过 FMassEntityManager 查询。
 */
USTRUCT()
struct alignas(8) FMassEntityHandle
{
	GENERATED_BODY()

	// 默认构造：Index=SerialNumber=0，表示未设置/无效的实体句柄。
	FMassEntityHandle() = default;
	// 以显式 Index/Serial 构造，一般由 FMassEntityManager 分配实体时内部使用，
	// 外部代码通常不应直接构造 handle，而是通过 EntityManager 的分配接口拿到。
	FMassEntityHandle(const int32 InIndex, const int32 InSerialNumber)
		: Index(InIndex), SerialNumber(InSerialNumber)
	{
	}
	
	// 实体槽位下标。EntityManager 内部数组中的位置，槽位会被回收复用，所以单靠 Index 不能唯一定位实体。
	UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
	int32 Index = 0;
	
	// 版本号/序列号。槽位每次被新实体占用时递增，用来区分"同一个 Index 被不同实体复用"的情况。
	UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
	int32 SerialNumber = 0;

	// 相等判断：Index 与 SerialNumber 必须全部一致。仅此才能认为是"同一个实体"的句柄。
	bool operator==(const FMassEntityHandle Other) const
	{
		return Index == Other.Index && SerialNumber == Other.SerialNumber;
	}

	bool operator!=(const FMassEntityHandle Other) const
	{
		return !operator==(Other);
	}

	/** Has meaning only for sorting purposes */
	// 严格弱序，仅用于将 handle 放进有序容器（TSet/TMap 的排序、调试输出等）。
	// 注意：只比较 Index，不参与业务语义，两个"槽位相同但 Serial 不同"的句柄会被视为等价。
	bool operator<(const FMassEntityHandle Other) const { return Index < Other.Index; }

	/** Note that this function is merely checking if Index and SerialNumber are set. There's no way to validate if 
	 *  these indicate a valid entity in an EntitySubsystem without asking the system. */
	// IsSet 只判断 handle 本身是否被赋值过（非零），不能保证它指向当前仍然存活的实体。
	// 要验证实体是否真实存在，必须通过 FMassEntityManager::IsEntityValid() 之类接口去查询。
	bool IsSet() const
	{
		return Index != 0 && SerialNumber != 0;
	}

	// IsValid 当前等价于 IsSet。保留作为更符合 UE 习惯的命名。
	inline bool IsValid() const
	{
		return IsSet();
	}

	// 清空为"未设置"状态。
	void Reset()
	{
		Index = SerialNumber = 0;
	}

	/** Allows the entity handle to be shared anonymously. */
	// AsNumber 把 handle 打包成一个 uint64，方便在不依赖 FMassEntityHandle 类型的地方传递
	// （蓝图脚本、网络序列化、线程任务参数等）。依赖 struct 恰好占 8 字节 + 8 字节对齐的事实，
	// 因此下面有两条 static_assert 兜底。
	uint64 AsNumber() const { return *reinterpret_cast<const uint64*>(this); } // Relying on the fact that this struct only stores 2 integers and is aligned correctly.
	/** Reconstruct the entity handle from an anonymously shared integer. */
	// 与 AsNumber 配对，将匿名的 uint64 还原回 handle。
	static FMassEntityHandle FromNumber(uint64 Value) 
	{ 
		FMassEntityHandle Result;
		*reinterpret_cast<uint64_t*>(&Result) = Value;
		return Result;
	}

	// 哈希函数：把 Index 和 SerialNumber 合并，使 handle 可直接作为 TMap/TSet 键。
	friend uint32 GetTypeHash(const FMassEntityHandle Entity)
	{
		return HashCombine(Entity.Index, Entity.SerialNumber);
	}

	// LexToString 让 handle 可直接被 UE_LOG、FString::Printf("%s") 等使用。
	friend FString LexToString(const FMassEntityHandle Entity)
	{
		return Entity.DebugGetDescription();
	}

	// 以 "i: <Index> sn: <Serial>" 的形式返回调试字符串。
	FString DebugGetDescription() const
	{
		return FString::Printf(TEXT("i: %d sn: %d"), Index, SerialNumber);
	}
};

// 兜底断言：保证 handle 永远能安全地当成 uint64 看待。
// 如果有人往 FMassEntityHandle 里加了新成员，这两条会立即在编译期报错，防止 AsNumber/FromNumber 悄悄出错。
static_assert(sizeof(FMassEntityHandle) == sizeof(uint64), "Expected FMassEntityHandle to be convertible to a 64-bit integer value, so size needs to be 8 bytes.");
static_assert(alignof(FMassEntityHandle) == sizeof(uint64), "Expected FMassEntityHandle to be convertible to a 64-bit integer value, so alignment needs to be 8 bytes.");
