// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "UObject/ObjectKey.h"
#include "Misc/NotNull.h"
#include "Misc/TVariant.h"
#include "Templates/SharedPointer.h"
#include "MassEntityConcepts.h"
#include "Subsystems/Subsystem.h"
#include "MassEntityRelations.h"
#include "MassExternalSubsystemTraits.h"

#define UE_API MASSENTITY_API


struct FMassEntityManager;

// =============================================================================
// 【模块定位 - FTypeManager（类型管理器）】
// -----------------------------------------------------------------------------
// 这是 Mass 框架的"运行时类型注册表"。
//
// 【核心问题】
//   Mass 中的很多关键信息（例如某个 USubsystem 是否只能在 GameThread 上访问、
//   某个 SharedFragment 是否线程安全等）原本是通过 `TMassExternalSubsystemTraits<T>`
//   等"编译期模板特化"提供的。但是 DependencySolver、Processor 调度、命令缓冲、
//   Observer 等运行时模块只能拿到 UStruct*/UClass*——它们没办法在运行时
//   实例化模板。因此需要把"编译期 traits"在初始化阶段"导出"成"运行时数据"，
//   这个导出/查询的中央仓库就是 FTypeManager。
//
// 【设计原则】
//   1. FTypeHandle 是基于 TObjectKey 的轻量"类型 ID"——不持有强引用、可作为 hash key。
//   2. FTypeInfo 用 TVariant 而不是继承+多态：避免堆分配、避免虚表、内存连续、
//      "类型即开关"在 switch 时类型清晰，符合 ECS 的数据导向哲学。
//   3. 把"什么类型应该被注册"和"具体怎么注册"分离：通过 OnRegisterBuiltInTypes
//      多播委托让外部模块（如 MassActors、Mass framework 内置 relations 等）
//      在 RegisterBuiltInTypes() 阶段挂入自己关心的类型。
//   4. 运行时新增 relation 类型时会通知 EntityManager（见 OnNewTypeRegistered），
//      让其创建对应的存储桶（FRelationData）和 destruction observer。
//
// 【调用方】
//   - FMassEntityManager 持有 TSharedRef<FTypeManager>。
//   - DependencySolver 通过 MakeSubsystemIterator 枚举所有 subsystem 类型，
//     再用 GetTypeInfo 获取 GameThreadOnly/ThreadSafeWrite 信息来排序 processor。
//   - FRelationManager 通过 GetRelationTypeChecked 在创建关系实体时获取该
//     relation 的 fragment 类型、destruction policy、archetype group type。
// =============================================================================

namespace UE::Mass
{
	using namespace Relations;
	struct FRelationData;

	/**
	 * Handle for identifying and managing types in the type manager
	 * 【FTypeHandle】轻量的"类型句柄"：内部仅持有一个 TObjectKey<const UStruct>，
	 *   它可作为 TMap 的 key、可被拷贝/比较，但不会阻止 UObject GC（弱引用语义）。
	 *   注意：本框架假设这些"代表类型的 UStruct/UClass" 不会在运行期被卸载，
	 *   所以使用 TObjectKey 是安全的。
	 */
	struct FTypeHandle
	{
		FTypeHandle() = default;
		FTypeHandle(const FTypeHandle&) = default;

		bool operator==(const FTypeHandle&) const = default;

		/** 是否为有效句柄（内部 TypeKey 是否已设置过）。注意 IsValid() 不验证类型是否仍存活。 */
		bool IsValid() const;

		/** 解析为 UClass*——若该句柄实际指向 USubsystem 子类等 UClass 时使用。 */
		const UClass* GetClass() const
		{
			return Cast<const UClass>(TypeKey.ResolveObjectPtr());
		}

		/** 解析为 UScriptStruct*——若该句柄实际指向 USTRUCT (例如 FMassRelation 子类、FMassSharedFragment) 时使用。 */
		const UScriptStruct* GetScriptStruct() const
		{
			return Cast<UScriptStruct>(TypeKey.ResolveObjectPtr());
		}

		/** 用于将 FTypeHandle 作为 TMap/TSet 的 key。哈希直接委托给 TObjectKey。 */
		friend inline uint32 GetTypeHash(const FTypeHandle& InHandle)
		{
			return GetTypeHash(InHandle.TypeKey);
		}

		/** 获取代表类型的 FName——用于日志/调试/序列化展示。 */
		FName GetFName() const
		{
			const UStruct* RepresentedType = TypeKey.ResolveObjectPtr();
			return GetFNameSafe(RepresentedType);
		}

		/** 安全版字符串名（即便底层 UStruct 已 GC 也不会 crash）。 */
		FString ToString() const
		{
			const UStruct* RepresentedType = TypeKey.ResolveObjectPtr();
			return GetNameSafe(RepresentedType);
		}

	private:
		friend struct FTypeManager;
		// 私有构造：只有 FTypeManager（friend）和 MakeTypeHandle 工厂能从一个 UStruct* 直接造出 FTypeHandle，
		// 防止外部代码"伪造"未注册的句柄。
		UE_API explicit FTypeHandle(TObjectKey<const UStruct> InTypeKey);

		/**
		 * 真正的类型 key。
		 * 用 TObjectKey 而非裸指针：
		 *   - 内部存储为 (ObjectIndex, SerialNumber) 对，可在 UObject GC 后保持稳定的 hash；
		 *   - 不会触发 UObject GC，对生命周期透明。
		 */
		TObjectKey<const UStruct> TypeKey;
	};

	/** 
	 * Placeholder to be used when no traits have been specified nor the type is known 
	 * 【FEmptyTypeTraits】TVariant 的"未设置"占位类型。
	 *   FTypeInfo::Traits 的默认 alternate 即为此空类型；
	 *   方便 ensure 时识别"忘记填 traits"的错误。
	 */
	struct FEmptyTypeTraits
	{
	};

	/** 
	 * Traits of USubsystem-based types
	 * 【FSubsystemTypeTraits】Subsystem 的运行时元数据。
	 *   对应编译期模板 TMassExternalSubsystemTraits<T> 的"导出版"——
	 *   DependencySolver 根据这些 bool 排序 processor 调度（GameThread-only 的 subsystem
	 *   只能在 GameThread 上访问；写线程安全的 subsystem 多个 processor 可并行写）。
	 */
	struct FSubsystemTypeTraits
	{
		FSubsystemTypeTraits() = default;

		/** 
		 * Factory function for creating traits specific to a given subsystem type
		 * 【模板工厂】把编译期模板特化"快照"为运行时数据。
		 *   注意这里没有依赖虚函数或运行时反射，纯靠模板偏特化在编译期决议。
		 *   typical 用法：FSubsystemTypeTraits::Make<UMyGameSubsystem>()
		 */
		template <typename T>
		static FSubsystemTypeTraits Make()
		{
			FSubsystemTypeTraits Traits;
			Traits.bGameThreadOnly = TMassExternalSubsystemTraits<T>::GameThreadOnly;
			Traits.bThreadSafeWrite = TMassExternalSubsystemTraits<T>::ThreadSafeWrite;
			return MoveTemp(Traits);
		}

		/** Whether the subsystem must  be run on the Game Thread 是否只能在 GameThread 上访问。默认保守为 true。 */
		bool bGameThreadOnly = true;
		/** Whether the subsystem supports thread-safe write operations 是否支持多线程并发写入；为 true 时多个 processor 可并发写。 */
		bool bThreadSafeWrite = false;
	};

	/** 
	 * Traits of Shared Fragment types
	 * 【FSharedFragmentTypeTraits】SharedFragment（同一份数据被多个 entity 共享）的运行时元数据。
	 *   目前只有 GameThreadOnly 一项；以后可能扩展。
	 */
	struct FSharedFragmentTypeTraits
	{
		FSharedFragmentTypeTraits() = default;

		/**
		 * Factory function for creating traits specific to a given shared fragment type
		 * 【模板工厂】使用 concept CSharedFragment 限定模板形参，要求 T 派生自 FMassSharedFragment。
		 */
		template<CSharedFragment T>
		static FSharedFragmentTypeTraits Make()
		{
			FSharedFragmentTypeTraits Traits;
			Traits.bGameThreadOnly = TMassSharedFragmentTraits<T>::GameThreadOnly;
			return MoveTemp(Traits);
		}

		/** Whether the shared fragment has to be used only on the Game Thread 是否只能在 GameThread 上访问。 */
		bool bGameThreadOnly = true;
	};

	/**
	 * Wrapper for metadata and traits about specific types. The type is used by the TypeManager
	 * to uniformly store information for all types.
	 *
	 * 【FTypeInfo】TypeManager 中每个类型的"元信息条目"。
	 *   关键设计：使用 TVariant 装载 4 种 traits 之一。
	 *   为什么用 TVariant 而不是继承+多态？
	 *     1. 避免堆分配：TVariant 在栈上原地存储，整个 FTypeInfo 是 POD-ish 的，
	 *        放进 TMap<FTypeHandle, FTypeInfo> 时移动成本极低。
	 *     2. 避免虚表：所有 traits 都是 plain struct，无 virtual。
	 *     3. 类型清晰：每次访问都要明确指定 alternate（GetAsSystemTraits / GetAsRelationTraits 等），
	 *        编译期就排除了"误用其它 traits"的可能。
	 *     4. 易扩展：新增一种 traits 只需在 TVariant 里追加一个 alternate。
	 */
	struct FTypeInfo
	{
		/** 4-way variant：空 / Subsystem / SharedFragment / Relation。新增类别时此处需扩展。 */
		using FTypeTraits = TVariant<FEmptyTypeTraits
			, FSubsystemTypeTraits
			, FSharedFragmentTypeTraits
			, FRelationTypeTraits>;

		/** 注册时取自 UStruct->GetFName()，用作日志/调试展示。 */
		FName TypeName;
		/** 实际的 traits 数据，调用者据此判断该类型属于何种"类别"。 */
		FTypeTraits Traits;

		/** Fetches stored data as FSubsystemTypeTraits, if applicable, or null otherwise 安全 try-cast；非 subsystem 类型返回 null。 */
		const FSubsystemTypeTraits* GetAsSystemTraits() const;

		/** Fetches stored data as FSharedFragmentTypeTraits, if applicable, or null otherwise 安全 try-cast；非 shared fragment 返回 null。 */
		const FSharedFragmentTypeTraits* GetAsSharedFragmentTraits() const;

		/** Fetches stored data as FRelationTypeTraits, if applicable, or null otherwise 安全 try-cast；非 relation 返回 null。 */
		const FRelationTypeTraits* GetAsRelationTraits() const;

		/** Fetches stored data as FRelationTypeTraits. Will complain if stored data is not of FRelationTypeTraits type 强制 cast，类型不匹配时 check 失败。 */
		const FRelationTypeTraits& GetAsRelationTraitsChecked() const;
	};

	/**
	 * 【FTypeManager】每个 EntityManager 持有一个，集中存储所有"已注册类型"的运行时元数据。
	 *
	 * 【生命周期】
	 *   1. EntityManager 构造时通过 MakeShared 创建 FTypeManager。
	 *   2. EntityManager Initialize 阶段调用 RegisterBuiltInTypes()，触发 OnRegisterBuiltInTypes
	 *      多播——内置 relation 类型（如 ChildOf）和外部模块在此期间注册。
	 *   3. 运行时仍可通过 RegisterType 追加新 relation 类型；TypeManager 会通知 EntityManager
	 *      创建对应的 destruction observer。
	 *
	 * 【为什么继承 TSharedFromThis】
	 *   - 运行时其它系统（observer、command buffer 回调）可能持有对 TypeManager 的弱引用；
	 *   - 通过 AsShared/AsWeak 安全地获得共享所有权，避免悬挂引用。
	 */
	struct FTypeManager : TSharedFromThis<FTypeManager>
	{
		/** 多播委托：在 RegisterBuiltInTypes 阶段广播，让外部模块注册自己关心的内置类型。 */
		DECLARE_MULTICAST_DELEGATE_OneParam(FOnRegisterBuiltInTypes, FTypeManager& /*Instance*/);

		/** 构造：保存所属 EntityManager 的引用（不持有所有权）。 */
		UE_API explicit FTypeManager(FMassEntityManager& InEntityManager);

		/**
		 * 触发 OnRegisterBuiltInTypes 广播；之后 bBuiltInTypesRegistered=true，
		 * 此后再注册 relation 类型时会立即通知 EntityManager 创建 observer。
		 */
		void RegisterBuiltInTypes();

		/**
		 * @return whether the type manager instance has any types registered at all.
		 *  是否一个类型都没注册——主要用于早期初始化校验。
		 */
		bool IsEmpty() const;

		/** 
		 * Register traits for given subsystem type 
		 * 注册一个 USubsystem 类型；InType 必须是 UClass*；TypeTraits 通常用 FSubsystemTypeTraits::Make<T>() 生成。
		 * @return 注册成功后的 FTypeHandle；若同一类型已注册过且非内置注册阶段，则覆写并返回原句柄。
		 */
		UE_API FTypeHandle RegisterType(TNotNull<const UStruct*> InType, FSubsystemTypeTraits&&);
		/** 
		 * Register traits for given shared fragments type 
		 * 注册一个 SharedFragment 类型；InType 必须派生自 FMassSharedFragment。
		 */
		UE_API FTypeHandle RegisterType(TNotNull<const UStruct*> InType, FSharedFragmentTypeTraits&&);
		/** 
		 * Register traits for given relation type 
		 * 注册一个 relation 类型；与上面两个不同——relation tag 类型已编码在 FRelationTypeTraits.RelationTagType 内。
		 * 若 RelationName 为空，会自动使用 RelationTagType 的 FName。
		 * 注意：内置类型注册完成后再注册 relation 会触发 EntityManager.OnNewTypeRegistered 创建 observer。
		 */
		UE_API FTypeHandle RegisterType(FRelationTypeTraits&&);

		/** 
		 * Registration helper for shared fragments 
		 * 模板糖：自动从 T::StaticStruct() + FSharedFragmentTypeTraits::Make<T>() 注册。
		 */
		template<CSharedFragment T>
		FTypeHandle RegisterType();

		/** 
		 * Registration helper for subsystems 
		 * 模板糖：要求 T 派生自 USubsystem；自动从 T::StaticClass() + FSubsystemTypeTraits::Make<T>() 注册。
		 */
		template<typename T> requires TIsDerivedFrom<typename TRemoveReference<T>::Type, USubsystem>::Value
		FTypeHandle RegisterType();

		/**
		 * @return stored traits for the type represented by TypeHandle, or null if the type is unknown
		 * 通过 FTypeHandle 查询元数据；类型未注册时返回 null。
		 */
		const FTypeInfo* GetTypeInfo(FTypeHandle TypeHandle) const;

		/**
		 * @return stored traits for the type represented by TypeKey, or null if the type is unknown
		 * 通过原生 TObjectKey 查询元数据；用于已经持有 TObjectKey 但未构造 FTypeHandle 的场景。
		 */
		const FTypeInfo* GetTypeInfo(TObjectKey<const UStruct> TypeKey) const;

		/** 通过 FTypeHandle 取出 relation traits（必须是已注册的 relation 类型，否则 check 失败）。 */
		const FRelationTypeTraits& GetRelationTypeChecked(const FTypeHandle TypeHandle) const;
		/** 通过 UScriptStruct* 取 relation traits；内部先转 FTypeHandle。 */
		const FRelationTypeTraits& GetRelationTypeChecked(const TNotNull<const UScriptStruct*> RelationOrElementType) const;
		/** 把一个 UScriptStruct*（必须派生自 FMassRelation）转换为 FTypeHandle。非 relation 时 ensure 失败并返回空句柄。 */
		UE_API FTypeHandle GetRelationTypeHandle(const TNotNull<const UScriptStruct*> RelationOrElementType) const;
		/** 验证给定类型是否为已注册的 relation 类型——比 GetRelationTypeHandle 多了"是否在 TypeDataMap 中"的二次校验。 */
		UE_API bool IsValidRelationType(const TNotNull<const UScriptStruct*> RelationOrElementType) const;

		/** Alias for the iterator type that can be used to iterate all of stored type traits information 遍历所有 (FTypeHandle, FTypeInfo) 对的迭代器类型。 */
		using FTypeInfoConstIterator = TMap<FTypeHandle, FTypeInfo>::TConstIterator;

		/**
		 * Alias for the iterator type that can be used to iterate over stored subsystem types.
		 * Note that this iterator will only point to types, not type trait data. Contents of this iterator
		 * need to be used with GetTypeInfo to get actual traits data.
		 * 仅遍历 subsystem 类型的迭代器；本身只产出 FTypeHandle，需配合 GetTypeInfo 获取详细数据。
		 * 使用场景：DependencySolver 只关心 subsystem 列表，不必遍历整个 TypeDataMap。
		 */
		using FSubsystemTypeConstIterator = TSet<FTypeHandle>::TConstIterator;

		/** 创建遍历所有已注册类型的迭代器。 */
		FTypeInfoConstIterator MakeIterator() const;
		/** 创建仅遍历 subsystem 类型的迭代器。 */
		FSubsystemTypeConstIterator MakeSubsystemIterator() const;

		/** 获取所属 EntityManager（构造时绑定，不可变）。 */
		FMassEntityManager& GetEntityManager();

		/** 静态工厂：从 UStruct* 构造 FTypeHandle（绕过 friend 限制供外部使用）。 */
		static FTypeHandle MakeTypeHandle(TNotNull<const UStruct*> InTypeKey);

		/**
		 * Broadcasts as part of FTypeManager::RegisterBuiltInTypes call, giving a chance to the external code to
		 * register additional types that are supposed to be available from the very start.
		 *
		 * 【全局静态委托】所有 FTypeManager 实例共享同一个 OnRegisterBuiltInTypes。
		 * 这是各模块（关系系统、Mass 内置 fragment、外部 gameplay 模块）"声明自己内置类型"的入口点。
		 * 注意：这是 static——一旦在某个模块 StartupModule 中绑定了回调，所有 FTypeManager 在
		 * RegisterBuiltInTypes 时都会触发它，所以注册函数本身要幂等且只用入参 Instance 上下文。
		 */
		UE_API static FOnRegisterBuiltInTypes OnRegisterBuiltInTypes;

	private:
		/** 
		 * Register traits for given type
		 * 内部统一注册路径：负责 SubsystemTypes 集合维护、TypeDataMap 写入、覆写检测。
		 */
		UE_API FTypeHandle RegisterTypeInternal(TNotNull<const UStruct*> InType, FTypeInfo&&);

		/** 所属 EntityManager 引用——TypeManager 是 EntityManager 的"成员设施"，生命周期严格内嵌。 */
		FMassEntityManager& OuterEntityManager; 

		/** Mapping of types to their info 主存储：类型句柄 → 元数据。 */
		TMap<FTypeHandle, FTypeInfo> TypeDataMap;

		/** 
		 * Contains all registered subsystem types. Can be used to filter access to TypeDataMap 
		 * 二级索引：仅 subsystem 类型的句柄集合。让 DependencySolver 不必扫描整个 TypeDataMap。 
		 */
		TSet<FTypeHandle> SubsystemTypes;

		/** 是否已经完成内置类型注册阶段——区分"正常注册"和"运行时新增"，影响 observer 是否立即创建。 */
		bool bBuiltInTypesRegistered = false;
	};

	//----------------------------------------------------------------------------
	// INLINES
	//----------------------------------------------------------------------------
	inline bool FTypeHandle::IsValid() const
	{
		// using this strange contraption since TObjectKey doesn't supply a way to check
		// whether it's set, while comparison and construction is trivial.
		// Note: we don't care to what the key's been set, we don't expect types to go away
		// 【实现说明】TObjectKey 没有提供 IsValid()，但默认构造的 TObjectKey 在比较时与已设置的不同，
		//   故借此 trick 判断"是否设置过"。注意此处不要求 ResolveObjectPtr() 仍然能解析到——
		//   Mass 假设代表类型的 UStruct 在框架生命周期内不会被卸载。
		return TypeKey != TObjectKey<const UStruct>();
	}

	inline const FSubsystemTypeTraits* FTypeInfo::GetAsSystemTraits() const
	{
		// TVariant::TryGet 在 alternate 不匹配时返回 nullptr，提供 try-cast 语义。
		return Traits.TryGet<FSubsystemTypeTraits>();
	}

	inline const FSharedFragmentTypeTraits* FTypeInfo::GetAsSharedFragmentTraits() const
	{
		return Traits.TryGet<FSharedFragmentTypeTraits>();
	}

	inline const FRelationTypeTraits* FTypeInfo::GetAsRelationTraits() const
	{
		return Traits.TryGet<FRelationTypeTraits>();
	}

	inline const FRelationTypeTraits& FTypeInfo::GetAsRelationTraitsChecked() const
	{
		// TVariant::Get 在 alternate 不匹配时 check 失败——调用方必须确认是 relation。
		return Traits.Get<FRelationTypeTraits>();
	}

	inline bool FTypeManager::IsEmpty() const
	{
		return TypeDataMap.IsEmpty();
	}

	inline FTypeManager::FTypeInfoConstIterator FTypeManager::MakeIterator() const
	{
		return FTypeManager::FTypeInfoConstIterator(TypeDataMap);
	}

	inline FTypeManager::FSubsystemTypeConstIterator FTypeManager::MakeSubsystemIterator() const
	{
		return FTypeManager::FSubsystemTypeConstIterator(SubsystemTypes);
	}

	template<CSharedFragment T>
	FTypeHandle FTypeManager::RegisterType()
	{
		// 模板糖：交由编译期决议 T::StaticStruct 与 traits Make<T>。
		return RegisterType(T::StaticStruct(), FSharedFragmentTypeTraits::Make<T>());
	}

	template<typename T>
	requires TIsDerivedFrom<typename TRemoveReference<T>::Type, USubsystem>::Value
	FTypeHandle FTypeManager::RegisterType()
	{
		// 注意：subsystem 用 StaticClass()（UClass*），其它类型用 StaticStruct()（UScriptStruct*）。
		return RegisterType(T::StaticClass(), FSubsystemTypeTraits::Make<T>());
	}

	inline const FTypeInfo* FTypeManager::GetTypeInfo(FTypeHandle TypeHandle) const
	{
		return TypeDataMap.Find(TypeHandle);
	}

	inline const FTypeInfo* FTypeManager::GetTypeInfo(TObjectKey<const UStruct> TypeKey) const
	{
		return TypeDataMap.Find(FTypeHandle(TypeKey));
	}

	inline const FRelationTypeTraits& FTypeManager::GetRelationTypeChecked(const FTypeHandle RelationHandle) const
	{
		const FTypeInfo* RelationInfo = GetTypeInfo(RelationHandle);
		check(RelationInfo);
		return RelationInfo->GetAsRelationTraitsChecked();
	}

	inline const FRelationTypeTraits& FTypeManager::GetRelationTypeChecked(const TNotNull<const UScriptStruct*> RelationOrElementType) const
	{
		// 两步：先从 UScriptStruct* 拿到 FTypeHandle，再走 handle 重载。
		return GetRelationTypeChecked(GetRelationTypeHandle(RelationOrElementType));
	}

	inline FMassEntityManager& FTypeManager::GetEntityManager()
	{
		return OuterEntityManager;
	}

	inline FTypeHandle FTypeManager::MakeTypeHandle(TNotNull<const UStruct*> InTypeKey)
	{
		return FTypeHandle(InTypeKey);
	}
}

#undef UE_API
