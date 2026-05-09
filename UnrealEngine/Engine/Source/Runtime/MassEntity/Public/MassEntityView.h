// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityHandle.h"
#include "MassArchetypeTypes.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "StructUtils/InstancedStruct.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityView.generated.h"

#define UE_API MASSENTITY_API

struct FMassEntityManager;
struct FMassArchetypeData;
struct FMassArchetypeHandle;

/** 
 * The type representing a single entity in a single archetype. It's of a very transient nature so we guarantee it's 
 * validity only within the scope it has been created in. Don't store it. 
 *
 * 中文说明：
 *   `FMassEntityView` 是 **单实体的轻量级访问器（"快捷通道"）**，用于在不开启完整 Query 的情况下读写
 *   一个 entity 的 fragment / shared fragment / tag 等数据。它面向以下场景：
 *     1) 调试/诊断工具：拿到一个 handle 想立刻看它的字段。
 *     2) 一次性逻辑：例如 UI 显示、Spawn 后立即填充某个字段、或处理玩家拥有的"特殊实体"。
 *     3) 偶发的跨 archetype 操作：当只有一个 entity 时，写一个 query+iterator 显得过重。
 *
 *   与 `FMassExecutionContext` 的关键差异：
 *     - `FMassExecutionContext` 面向 **整个 chunk** 批量数据，是 processor / iterator 的标配；
 *     - `FMassEntityView` 面向 **单一 entity**，每次访问都要走 ArchetypeData 索引 + 列查找，
 *       因此**不适合在 hot loop 里逐个 entity 调用**——那样还不如直接写 query。
 *
 *   生命周期保证（重要）：
 *     - 构造时会解析出 entity 所在 ArchetypeData 指针 + chunk 内位置 (`FMassEntityInChunkDataHandle`)；
 *     - 一旦 archetype 内部发生 chunk 重排（添加/删除 entity、AddTag 改 archetype 等），
 *       已缓存的 chunk 内位置可能失效。`FMassEntityInChunkDataHandle` 内部带有版本/序列号，
 *       后续 IsValid(Archetype) 检查能识别出过期，但**仍要求调用者只在创建该 view 的临时作用域中使用**。
 *     - 因此：**不要把 view 存为成员变量、不要跨帧持有、不要在写操作（会改 archetype 布局）之后继续使用**。
 */
USTRUCT()
struct FMassEntityView
{
	GENERATED_BODY()

	/** 默认构造产生一个"未设置"(unset) 的 view，IsSet()/IsValid() 返回 false。*/
	FMassEntityView() = default;

	/** 
	 *  Resolves Entity against ArchetypeHandle. Note that this approach requires the caller to ensure that Entity
	 *  indeed belongs to ArchetypeHandle. If not the call will fail a check. As a remedy calling the 
	 *  FMassEntityManager-flavored constructor is recommended since it will first find the appropriate archetype for
	 *  Entity. 
	 *
	 * 中文说明：
	 *   "已知 archetype" 版本的构造函数。**调用者必须保证 Entity 确实属于 ArchetypeHandle 指向的 archetype**，
	 *   否则内部 MakeEntityHandle 会触发 check。
	 *   适合"刚刚 BatchCreateEntities 之后立刻填充"这种场景：archetype 是已知的，免去再查一次的开销。
	 */
	UE_API FMassEntityView(const FMassArchetypeHandle& ArchetypeHandle, FMassEntityHandle Entity);

	/** 
	 *  Finds the archetype Entity belongs to and then resolves against it. The caller is responsible for ensuring
	 *  that the given Entity is in fact a valid ID tied to any of the archetypes 
	 *
	 * 中文说明：
	 *   通用构造：让 EntityManager 先反查 entity 所在 archetype，再解析 chunk 内位置。
	 *   要求 Entity 是有效 handle（已注册过、未销毁）；若 entity 已不存在（"野" handle），
	 *   将在 ArchetypeDataFromHandleChecked 里 check fail。
	 *   不确定 entity 是否还活着时，应改用 `TryMakeView`。
	 */
	UE_API FMassEntityView(const FMassEntityManager& EntityManager, FMassEntityHandle Entity);

	/** 
	 * If the given handle represents a valid entity the function will create a FMassEntityView just like a constructor 
	 * would. If the entity is not valid the produced view will be "unset".
	 *
	 * 中文说明：
	 *   "安全工厂" 版本：先验证 Entity 是否仍然有效，若有效则构造 view，无效则返回默认（未设置）的 view。
	 *   推荐在 entity 寿命不确定的场景使用（例如 entity handle 来自外部缓存、可能被销毁的情况）。
	 *   返回值用 IsSet() / IsValid() 判断后再使用。
	 */
	static UE_API FMassEntityView TryMakeView(const FMassEntityManager& EntityManager, FMassEntityHandle Entity);

	/** 返回此 view 关联的 entity handle（即使 view 未设置也会返回原始 handle）。*/
	FMassEntityHandle GetEntity() const	{ return Entity; }

	/** will fail a check if the viewed entity doesn't have the given fragment */
	/**
	 * 中文说明：
	 *   按类型 T 取 fragment 的"必有"版本：要求 entity 的 archetype 含此 fragment，否则 check fail。
	 *   返回引用，调用方可直接读写（fragment 是 per-entity 的可变数据）。
	 *   编译期约束：T 必须是 FMassFragment 派生（CFragment concept），且不能是 FMassTag。
	 */
	template<typename T>
	T& GetFragmentData() const
	{
		static_assert(!UE::Mass::CTag<T>,
			"Given struct doesn't represent a valid fragment type but a tag. Use HasTag instead.");
		static_assert(UE::Mass::CFragment<T>, MASS_INVALID_FRAGMENT_MSG);

		return *((T*)GetFragmentPtrChecked(*T::StaticStruct()));
	}
		
	/** if the viewed entity doesn't have the given fragment the function will return null */
	/**
	 * 中文说明：
	 *   按类型 T 取 fragment 的"可选"版本：archetype 没有此 fragment 时返回 nullptr，不会触发 check。
	 *   适合可选 fragment 的轻量探测，调用方需自行判空。
	 */
	template<typename T>
	T* GetFragmentDataPtr() const
	{
		static_assert(!UE::Mass::CTag<T>,
			"Given struct doesn't represent a valid fragment type but a tag. Use HasTag instead.");
		static_assert(UE::Mass::CFragment<T>, MASS_INVALID_FRAGMENT_MSG);

		return (T*)GetFragmentPtr(*T::StaticStruct());
	}

	/**
	 * 中文说明：
	 *   按运行时类型 (UScriptStruct*) 取 fragment，返回 FStructView（类型擦除的 (struct*, ptr) 包装）。
	 *   适合泛型代码、蓝图集成、反射驱动的工具。返回的 FStructView 内部指针可能为空（即 entity 没有该 fragment）。
	 */
	FStructView GetFragmentDataStruct(const UScriptStruct* FragmentType) const
	{
		check(FragmentType);
		return FStructView(FragmentType, static_cast<uint8*>(GetFragmentPtr(*FragmentType)));
	}

	/** if the viewed entity doesn't have the given const shared fragment the function will return null */
	/**
	 * 中文说明：
	 *   取 const shared fragment 的"可选"版本：const shared fragment 是只读、由多 entity 共享的数据
	 *   （类似"原型/配置"），从 archetype 上携带的 SharedFragmentValues 中查找。
	 */
	template<typename T>
	const T* GetConstSharedFragmentDataPtr() const
	{
		static_assert(UE::Mass::CConstSharedFragment<T>,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");

		return (const T*)GetConstSharedFragmentPtr(*T::StaticStruct());
	}

	/** will fail a check if the viewed entity doesn't have the given const shared fragment */
	/**
	 * 中文说明：取 const shared fragment 的"必有"版本，缺失时 check fail。
	 */
	template<typename T>
	const T& GetConstSharedFragmentData() const
	{
		static_assert(UE::Mass::CConstSharedFragment<T>,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");

		return *((const T*)GetConstSharedFragmentPtrChecked(*T::StaticStruct()));
	}

	/**
	 * 中文说明：按运行时类型取 const shared fragment，返回 FConstStructView（只读视图）。
	 *   编译期 / 运行期均要求 FragmentType 派生自 FMassConstSharedFragment。
	 */
	FConstStructView GetConstSharedFragmentDataStruct(const UScriptStruct* FragmentType) const
	{
		check(UE::Mass::IsA<FMassConstSharedFragment>(FragmentType));
		return FConstStructView(FragmentType, static_cast<const uint8*>(GetConstSharedFragmentPtr(*FragmentType)));
	}

	/** will fail a check if the viewed entity doesn't have the given shared fragment */
	/**
	 * 中文说明：
	 *   取 (mutable) shared fragment 的"必有"版本：shared fragment 是可变的、由多 entity 共享的数据
	 *   （任何对它的改动会被所有共享方"看到"），缺失时 check fail。
	 *   注意 race：跨多个 processor / 多线程并发改 shared fragment 必须有外部同步。
	 */
	template<UE::Mass::CSharedFragment T>
	T& GetSharedFragmentData() const
	{
		return *((T*)GetSharedFragmentPtrChecked(*T::StaticStruct()));
	}

	/** if the viewed entity doesn't have the given shared fragment the function will return null */
	/** 中文说明：取 (mutable) shared fragment 的"可选"版本，缺失时返回 nullptr。*/
	template<UE::Mass::CSharedFragment T>
	T* GetSharedFragmentDataPtr() const
	{
		return (T*)GetSharedFragmentPtr(*T::StaticStruct());
	}

	/**
	 * 中文说明：
	 *   旧 API 兼容路径：以前 GetSharedFragmentDataPtr 同时支持 const shared 与 shared，5.5 起拆分为两套，
	 *   保留这里的 deprecation overload 是为了让旧代码继续编译，但会出现 deprecation warning。
	 *   新代码请使用 GetConstSharedFragmentDataPtr。
	 */
	template<UE::Mass::CConstSharedFragment T>
	UE_DEPRECATED(5.5, "Using GetSharedFragmentDataPtr with const shared fragments is deprecated. Use GetConstSharedFragmentDataPtr instead")
	T* GetSharedFragmentDataPtr() const
	{
		return const_cast<T*>(GetConstSharedFragmentDataPtr<T>());
	}

	/** 中文说明：同上，5.5 起 const shared 用 GetConstSharedFragmentData。*/
	template<UE::Mass::CConstSharedFragment T>
	UE_DEPRECATED(5.5, "Using GetSharedFragmentDataPtr with const shared fragments is deprecated. Use GetConstSharedFragmentData instead")
	T& GetSharedFragmentData() const
	{
		return const_cast<T&>(GetConstSharedFragmentData<T>());
	}

	/** 中文说明：按运行时类型取 (mutable) shared fragment，返回 FStructView。*/
	FStructView GetSharedFragmentDataStruct(const UScriptStruct* FragmentType) const
	{
		check(UE::Mass::IsA<FMassSharedFragment>(FragmentType));
		return FStructView(FragmentType, static_cast<uint8*>(GetSharedFragmentPtr(*FragmentType)));
	}

	/**
	 * 中文说明：
	 *   按类型 T 检查 archetype 是否带某个 tag。Tag 是零字节标记结构，仅出现在 archetype 的"组成（composition）"里，
	 *   并不存进 chunk 数据列。HasTag 比 GetFragmentData 廉价得多。
	 */
	template<typename T>
	bool HasTag() const
	{
		static_assert(UE::Mass::CTag<T>, "Given struct doesn't represent a valid tag type. Make sure to inherit from FMassTag or one of its child-types.");
		return HasTag(*T::StaticStruct());
	}

	/** 中文说明：HasTag 的运行时类型版本。*/
	UE_API bool HasTag(const UScriptStruct& TagType) const;

	/** 中文说明：view 是否已经成功解析到某个 archetype 的某个 chunk 槽位（构造成功且 handle 仍未失效）。*/
	bool IsSet() const;
	/** 中文说明：IsValid 是 IsSet 的别名，语义同上。*/
	bool IsValid() const;
	/** 中文说明：两个 view 视为相等当且仅当 archetype 指针 + chunk 内位置 handle 都相同。*/
	bool operator==(const FMassEntityView& Other) const;

protected:
	// ----- 受保护的"按运行时 UScriptStruct*"实现层 -----
	// 模板版的 GetFragmentData / GetSharedFragmentData 等都最终落到这几个函数，
	// 拆出来一是减小模板膨胀的代码体积，二是让运行期 (UScriptStruct*) 调用方共享同一份实现。

	/** 中文：按 fragment 类型查 chunk 内地址，缺失返回 nullptr；archetype 未设置时 ensure 提示并返回 nullptr。*/
	UE_API void* GetFragmentPtr(const UScriptStruct& FragmentType) const;
	/** 中文：按 fragment 类型查 chunk 内地址，缺失时 check fail。*/
	UE_API void* GetFragmentPtrChecked(const UScriptStruct& FragmentType) const;
	/** 中文：从 archetype 上的 SharedFragmentValues 查 const shared fragment 内存地址，缺失返回 nullptr。*/
	UE_API const void* GetConstSharedFragmentPtr(const UScriptStruct& FragmentType) const;
	/** 中文：同上，缺失时 check fail。*/
	UE_API const void* GetConstSharedFragmentPtrChecked(const UScriptStruct& FragmentType) const;
	/** 中文：从 archetype 上的 SharedFragmentValues 查 (mutable) shared fragment 内存地址，缺失返回 nullptr。*/
	UE_API void* GetSharedFragmentPtr(const UScriptStruct& FragmentType) const;
	/** 中文：同上，缺失时 check fail。*/
	UE_API void* GetSharedFragmentPtrChecked(const UScriptStruct& FragmentType) const;

private:
	/** 中文：被访问的 entity handle（含 SerialNumber，可用于 IsEntityValid 校验）。*/
	FMassEntityHandle Entity;
	/**
	 * 中文：解析后得到的 chunk 内位置 (chunk index + chunk 内 slot index + 序列号)。
	 * 序列号用于检测 archetype 内部 chunk 重排（其它 entity 的添加/删除/迁移）造成本 handle 失效。
	 * IsValid(Archetype) 会同时校验 archetype 指针与序列号。
	 */
	FMassEntityInChunkDataHandle EntityDataHandle;
	/**
	 * 中文：entity 所属的 archetype 数据指针（构造时解析）。注意：如果 entity 期间被 AddTag/RemoveTag
	 * 等操作迁到别的 archetype，这个指针就过期了——因此 view 必须只在创建它的临时作用域里使用。
	 */
	FMassArchetypeData* Archetype = nullptr;
};

//-----------------------------------------------------------------------------
// INLINES
//-----------------------------------------------------------------------------
// 中文：以下三个 inline 都是 hot path（IsValid 经常被调用），故内联放头文件。

/** 中文：核心校验——chunk handle 必须仍然指向当前 archetype 中合法位置（含序列号一致性检查）。*/
inline bool FMassEntityView::IsSet() const
{
	return EntityDataHandle.IsValid(Archetype);
}

inline bool FMassEntityView::IsValid() const
{
	return IsSet();
}

inline bool FMassEntityView::operator==(const FMassEntityView& Other) const
{
	return Archetype == Other.Archetype && EntityDataHandle == Other.EntityDataHandle;
}

#undef UE_API
