// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotNull.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

#define UE_API MASSENTITY_API

class UMassProcessor;

namespace UE::Mass
{

/** Interface for QueryDefinition templates. Not intended for other direct inheritance.
 *
 *  【FQueryDefinitionBase】QueryDefinition<...> 模板的虚基类。
 *  作用是给 FQueryExecutor 持有一个 type-erased 指针（AccessorsPtr），
 *  在不知道具体 Accessors... 类型的情况下，仍然可以调用 ConfigureQuery 和 SetupForExecute 两个钩子。
 */
struct FQueryDefinitionBase
{
	// 把 QueryDefinition 中所有 accessor 的需求（fragment/tag/subsystem）一次性注入到 EntityQuery / ProcessorRequirements。
	// 调用时机：UMassProcessor::ConfigureQueries 期。
	virtual void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) = 0;
	// 在 Execute 之前为每个 accessor 准备好"Execute 级"的引用（如 subsystem 指针）。
	// 注意：chunk 级的引用（fragment view）由 SetupForChunk 在每个 chunk 入口前再设置。
	virtual void SetupForExecute(FMassExecutionContext& Context) = 0;
};

/**
 * A MassEntityQuery wrapper with type-safe data access.
 *
 * 【FQueryExecutor】MassEntityQuery 的强类型包装。
 *  传统写法是：在 UMassProcessor::Execute 里调 Query.ForEachEntityChunk(... lambda...)，
 *  lambda 内部一行行 Context.GetMutableFragmentView<XxxFragment>() 来取数据。
 *  使用 FQueryExecutor 后：
 *    - 派生类声明一个 FQueryDefinition<MutableAccess<A>, ConstAccess<B>, ...> 成员
 *    - 框架从模板参数自动生成所有 AddRequirement / GetMutableFragmentView 样板
 *    - 派生类只需重写 Execute 取 Definition.Get<A>() 这种类型安全的方式访问
 *  在 UMassProcessor 里把 TSharedPtr<FQueryExecutor> 赋给 AutoExecuteQuery，框架就会自动派发，
 *  无需手写 Execute 的样板代码。
 */
struct FQueryExecutor
{
	// 【创建路径】静态工厂：把派生类与 FMassEntityQuery 关联起来，必要时绑定 Log 归属
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, FQueryExecutor>::IsDerived>::Type>
	static TSharedPtr<T> CreateQuery(FMassEntityQuery& InQuery, UObject* InLogOwner = nullptr)
	{
		TSharedPtr<T> Query = MakeShared<T>();
		Query->BoundQuery = &InQuery;
		Query->LogOwner = InLogOwner;

#if WITH_MASSENTITY_DEBUG
		Query->DebugSize = sizeof(T);
		// 校验 AccessorsPtr 指向派生类的成员（防止悬挂指针）
		Query->ValidateAccessors();
#endif

		return Query;
	}

	template<typename... Ts>
	friend struct FQueryDefinition;

	friend ::UMassProcessor;

	// 公开构造：绑定到一个已有 query；可选指定 log 归属对象
	UE_API explicit FQueryExecutor(FMassEntityQuery& InQuery, UObject* InLogOwner = nullptr);
	virtual ~FQueryExecutor() = default;

	/** Override with logic to perform against the entities returned by this query.
	 *  【派生类必须重写】实际的查询执行逻辑：通过 Definition.Get<XxxFragment>() 访问数据。
	 *  调用约束：由框架（CallExecute）统一驱动，不要直接调用。
	 */
	virtual void Execute(FMassExecutionContext& Context) = 0;

protected:

	inline UObject* GetLogOwner()
	{
		return LogOwner.Get();
	}

	// =============================================================================
	// 内部使用的便捷遍历模板
	// =============================================================================
	// 这些 helper 把 BoundQuery->ForEachEntityChunk 包了一层，自动做了 chunk 级 accessor 绑定：
	//   每个 chunk 入口时，对 Accessors.AccessorTuple 中每一个 accessor 调一次 SetupForChunk(Context)，
	//   也就是给它们更新 fragment view / chunk fragment 指针等。
	// 【变参展开示例】假设 TAccessors 是 FQueryDefinition<FMutableFragmentAccess<A>, FConstFragmentAccess<B>>:
	//   ApplyBefore([](auto&... Accessor){ (Accessor.SetupForChunk(Context), ...); })
	// 等价于（伪代码）：
	//   AccessorTuple.Get<0>().SetupForChunk(Context);   // 对 A 调 GetMutableFragmentView<A>()
	//   AccessorTuple.Get<1>().SetupForChunk(Context);   // 对 B 调 GetFragmentView<B>()

	// 串行 chunk 遍历：最常见路径
	template<typename TAccessors, typename TFunc>
	inline void ForEachEntityChunk(FMassExecutionContext& ExecutionContext, TAccessors& Accessors, const TFunc&& ExecuteFunction)
	{
		BoundQuery->ForEachEntityChunk(ExecutionContext, [&Accessors, &ExecuteFunction](FMassExecutionContext& Context)
		{
			Accessors.AccessorTuple.ApplyBefore([&Context](auto&... Accessor)
			{
				(Accessor.SetupForChunk(Context), ...);
			});
			ExecuteFunction(Context, Accessors);
		});
	}

	// 并行 chunk 遍历：注意为每个 worker 拷贝一份本地 LocalAccessors（线程安全）
	template<typename TAccessors, typename TFunc>
	inline void ParallelForEachEntityChunk(FMassExecutionContext& ExecutionContext, const TAccessors& Accessors, const TFunc&& ExecuteFunction)
	{
		BoundQuery->ParallelForEachEntityChunk(ExecutionContext, [&Accessors, &ExecuteFunction](FMassExecutionContext& Context)
		{
			TAccessors LocalAccessors = TAccessors(Accessors);

			LocalAccessors.AccessorTuple.ApplyBefore([&Context](auto&... Accessor)
			{
				(Accessor.SetupForChunk(Context), ...);
			});

			ExecuteFunction(Context, LocalAccessors);
		});
	}

	// 串行逐实体遍历：在 chunk 回调内再用 CreateEntityIterator 逐个实体调用
	template<typename TAccessors, typename TFunc>
	inline void ForEachEntity(FMassExecutionContext& ExecutionContext, TAccessors& Accessors, const TFunc&& ExecuteFunction)
	{
		BoundQuery->ForEachEntityChunk(ExecutionContext, [&Accessors, &ExecuteFunction](FMassExecutionContext& Context)
		{
			Accessors.AccessorTuple.ApplyBefore([&Context](auto&... Accessor)
			{
				(Accessor.SetupForChunk(Context), ...);
			});

			for (uint32 EntityIndex : Context.CreateEntityIterator())
			{
				ExecuteFunction(Context, Accessors, EntityIndex);
			}
		});
	}

	// 并行逐实体遍历
	template<typename TAccessors, typename TFunc>
	inline void ParallelForEachEntity(FMassExecutionContext& ExecutionContext, TAccessors& Accessors, const TFunc&& ExecuteFunction)
	{
		BoundQuery->ParallelForEachEntityChunk(ExecutionContext, [&Accessors, &ExecuteFunction](FMassExecutionContext& Context)
		{
			TAccessors LocalAccessors = TAccessors(Accessors);
			LocalAccessors.AccessorTuple.ApplyBefore([&Context](auto&... Accessor)
			{
				(Accessor.SetupForChunk(Context), ...);
			});

			for (uint32 EntityIndex : Context.CreateEntityIterator())
			{
				ExecuteFunction(Context, LocalAccessors, EntityIndex);
			}
		});
	}

	// 默认构造：用 DummyQuery 占位，专供\"暂时未绑定\"的边缘情况
	UE_API FQueryExecutor();
private:
	// 全局共享的 dummy query，避免 BoundQuery 出现 nullptr 状态
	static UE_API FMassEntityQuery DummyQuery;

	// 由框架（UMassProcessor）调用：把 QueryDefinition 的需求注入到 BoundQuery
	UE_API void ConfigureQuery(FMassSubsystemRequirements& ProcessorRequirements);
	// 由框架调用：调 SetupForExecute 后再触发用户 Execute
	UE_API void CallExecute(FMassExecutionContext& Context);

	// 【强引用绑定的 query】TNotNull 保证非空（通过 DummyQuery 兜底）
	TNotNull<FMassEntityQuery*> BoundQuery;
	// 可视化日志的归属 UObject（弱引用避免阻止 GC）
	TWeakObjectPtr<UObject> LogOwner;

	/** AccessorsPtr is only allowed to point to a member variable of this struct and will assert in all other cases.
	 *  【一个核心约束】AccessorsPtr 必须指向派生类的成员变量（FQueryDefinition<...>）。
	 *  ValidateAccessors 在 debug 构建中会通过对比指针范围 [this, this+sizeof(T)] 验证此约束。
	 *  违反约束会导致 dangling pointer，因为 FQueryDefinition 通常是栈/heap 上的 object。
	 */
	FQueryDefinitionBase* AccessorsPtr;

#if WITH_MASSENTITY_DEBUG
	UE_API void ValidateAccessors();
	uint32 DebugSize = 0;  // CreateQuery 记下派生类大小，用于 ValidateAccessors 的范围检查
#endif
};

// 【IsUnique 编译期断言】检查 Accessors 列表中没有重复的 fragment 类型。
// 用法：static_assert(IsUnique<U, Us...>) 编译期就报错。
// 如果允许重复，TQueryDefinition::Get<TFragment>() 该返回哪一个就有歧义。
template <typename...>
inline constexpr auto IsUnique = std::true_type{};

template <typename T, typename... Rest>
inline constexpr auto IsUnique<T, Rest...> = std::bool_constant<(!std::is_same_v<typename T::FragmentType, typename Rest::FragmentType> && ...) && IsUnique<Rest...>>{};


// 【在 Accessors... 列表中查找类型 T 对应的下标】
//   伪代码展开（T=B, list=[FMutableAccess<A>, FConstAccess<B>, FConstAccess<C>]）:
//     GetIndexRecursive<B, A_Acc, B_Acc, C_Acc>()
//       → A_Acc::FragmentType == B? 否
//       → 1 + GetIndexRecursive<B, B_Acc, C_Acc>()
//         → B_Acc::FragmentType == B? 是
//         → 0
//     最终返回 1
template <typename T, typename U, typename... Us>
constexpr auto GetIndexRecursive()
{
	if constexpr (std::is_same<T, typename U::FragmentType>::value)
	{
		return 0;
	}
	else
	{
		// 递归终止条件：还有更多 accessor 可继续查；找不到则触发 static_assert
		static_assert(sizeof...(Us) > 0, "Type not found in accessor collection.");
		return 1 + GetIndexRecursive<T, Us...>();
	}
}

// GetAccessorIndex<T, Accessors...> = T 在 Accessors 中的位置；同时强制 Accessors 集合无重复
template <typename T, typename U, typename... Us>
constexpr auto GetAccessorIndex()
{
	static_assert(IsUnique<U, Us...>, "An accessor collection must only contain a single instance of each fragment/subsystem/tag type.");
	return GetIndexRecursive<T, U, Us...>();
}

/**
 * Defines the entity compositions to return in the query and provides type-safe data access to
 * entity and subsystem data. Must be a member variable of a QueryExecutor
 *
 * 【FQueryDefinition<Accessors...>】 把所有 accessor 包成一个 TTuple，提供：
 *   - ConfigureQuery：变参展开，每个 accessor 调一次 ConfigureQuery，把需求注入到 EntityQuery。
 *   - SetupForExecute / SetupForChunk：变参展开，逐 accessor 准备运行时引用。
 *   - Get<TFragment>()：编译期下标查找，类型安全地拿到对应 accessor。
 *
 * 【一个完整的展开例子】
 *   class FMyQuery : public FQueryExecutor {
 *     FQueryDefinition<
 *       FMutableFragmentAccess<FTransformFragment>,
 *       FConstFragmentAccess<FVelocityFragment>
 *     > Definition{*this};
 *     virtual void Execute(...) {
 *       auto& Transforms = Definition.Get<FTransformFragment>();  // 编译期就拿到 tuple[0]
 *       auto& Velocities = Definition.Get<FVelocityFragment>();   // 编译期就拿到 tuple[1]
 *       ...
 *     }
 *   };
 *   // ConfigureQuery 时，编译器自动生成等价代码：
 *   //   EntityQuery.AddRequirement<FTransformFragment>(ReadWrite);
 *   //   EntityQuery.AddRequirement<FVelocityFragment>(ReadOnly);
 *   // SetupForChunk 时，编译器自动生成：
 *   //   Transforms.View = Context.GetMutableFragmentView<FTransformFragment>();
 *   //   Velocities.View = Context.GetFragmentView<FVelocityFragment>();
 */
template<typename... Accessors>
struct FQueryDefinition : public FQueryDefinitionBase
{
	// 【关键】在派生 QueryExecutor 的成员初始化中调用，把自身指针写到 Owner.AccessorsPtr。
	// 这条链让 FQueryExecutor 在不知道具体 Accessors 类型的情况下仍然能调虚函数。
	FQueryDefinition(FQueryExecutor& Owner)
	{
		Owner.AccessorsPtr = this;
	}

	// 用 TTuple 存所有 accessor，编译期就能按 index 拿到。
	TTuple<Accessors...> AccessorTuple{};

	// 【变参展开 1】ConfigureQuery：让每个 accessor 自己声明需求
	//   ApplyBefore 是 TTuple 的工具：把 tuple 里所有元素以左值引用展开成 lambda 的 ... pack。
	//   (Accessor.ConfigureQuery(EntityQuery, ProcessorRequirements), ...) 是 C++17 fold expression，
	//   等价于 A0.ConfigureQuery(...); A1.ConfigureQuery(...); ... ;
	virtual void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) override
	{
		AccessorTuple.ApplyBefore([&EntityQuery, &ProcessorRequirements](auto&... Accessor)
		{
			(Accessor.ConfigureQuery(EntityQuery, ProcessorRequirements), ...);
		});

	}

	// 【变参展开 2】SetupForExecute：在每次 Execute 前给"Execute 级别"的 accessor（如 subsystem）准备引用
	virtual void SetupForExecute(FMassExecutionContext& Context) override
	{
		AccessorTuple.ApplyBefore([&Context](auto&... Accessor)
		{
			(Accessor.SetupForExecute(Context), ...);
		});
	}

	// 【变参展开 3】SetupForChunk：每个 chunk 进入前刷新所有 accessor 的 view（fragment view 等）
	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		AccessorTuple.ApplyBefore([&Context](auto&... Accessor)
		{
			(Accessor.SetupForChunk(Context), ...);
		});
	}

	// 【类型安全下标】编译期通过 GetAccessorIndex 求 TFragment 在 Accessors... 中的位置
	template <typename TFragment>
	inline constexpr auto& Get()
	{
		constexpr std::size_t Index = GetAccessorIndex<TFragment, Accessors...>();
		return AccessorTuple.template Get<Index>();
	}
};

// =============================================================================
// 各种 Accessor —— 每种都对应一种 ECS 数据访问模式
// =============================================================================
// 通用约定：每个 accessor 必须实现：
//   using FragmentType = ...;          // 用于 Get<T>() 索引
//   void ConfigureQuery(query, processor)  // 注入需求
//   void SetupForExecute(context)          // Execute 前准备（如 subsystem）
//   void SetupForChunk(context)            // chunk 前准备（如 fragment view）

// 【可写 entity fragment】对应 EntityQuery.AddRequirement<T>(ReadWrite)
template<typename TFragment>
struct FMutableFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FMutableFragmentAccess() = default;

	// 取整个 chunk 的可写 view（一段连续内存）
	inline TArrayView<TFragment>& Get()
	{
		return View;
	}

	// 直接按下标访问 chunk 中第 Index 个 entity 的 fragment
	inline TFragment& operator[](int32 Index)
	{
		return View[Index];
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements)
	{
		EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadWrite);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	// 每个 chunk 入口刷新 view
	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		View = Context.GetMutableFragmentView<TFragment>();
	}

	TArrayView<TFragment> View;
};

// 【只读 entity fragment】对应 EntityQuery.AddRequirement<T>(ReadOnly)
template<typename TFragment>
struct FConstFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FConstFragmentAccess() = default;

	inline const TConstArrayView<TFragment>& Get() const
	{
		return View;
	}

	inline const TFragment& operator[](int32 Index) const
	{
		return View[Index];
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		View = Context.GetFragmentView<TFragment>();
	}

	TConstArrayView<TFragment> View;
};

// 【可写 + Optional entity fragment】可能不存在：用 Num() 或 operator bool() 判断是否绑到了
template<typename TFragment>
struct FMutableOptionalFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FMutableOptionalFragmentAccess() = default;

	inline TArrayView<TFragment>& Get() const
	{
		return View;
	}

	inline TFragment& operator[](int32 Index) const
	{
		return View[Index];
	}

	inline int32 Num() const
	{
		return View.Num();
	}

	// 隐式 bool 转换：方便 if (FragmentAccess) { ... }
	inline operator bool() const
	{
		return View.Num() > 0;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		View = Context.GetMutableFragmentView<TFragment>();
	}

	TArrayView<TFragment> View;
};

// 【只读 + Optional entity fragment】
// 注意：ConfigureQuery 中调用的是 AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly) —— 没传 Presence 参数。
// 看起来像是\"应当\"传 Optional 的笔误（与 FMutableOptionalFragmentAccess 不一致）。
template<typename TFragment>
struct FConstOptionalFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FConstOptionalFragmentAccess() = default;

	inline const TConstArrayView<TFragment>& Get() const
	{
		return View;
	}

	inline const TFragment& operator[](int32 Index) const
	{
		return View[Index];
	}

	inline int32 Num() const
	{
		return View.Num();
	}

	inline operator bool() const
	{
		return View.Num() > 0;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		// 【疑似 bug】此处应为 AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional)
		// 否则一个声明为 \"const optional\" 的 fragment 实际会变成 \"const required\"。
		EntityQuery.AddRequirement<TFragment>(EMassFragmentAccess::ReadOnly);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		View = Context.GetFragmentView<TFragment>();
	}

	TConstArrayView<TFragment> View;
};

// 【白名单 tag】要求 archetype 一定包含 TTag
template<typename TTag>
struct FMassTagRequired
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TTag;

	FMassTagRequired() = default;

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddTagRequirement<TTag>(EMassFragmentPresence::All);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
	}
};

// 【黑名单 tag】要求 archetype 一定不包含此 tag
template<typename TFragment>
struct FMassTagBlocked
{
	using FragmentType = TFragment;

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddTagRequirement<TFragment>(EMassFragmentPresence::None);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
	}
};

// 【可写 shared fragment】所有持有该 shared fragment 的实体共享一个对象（可读写）
template<typename TFragment>
struct FMutableSharedFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FMutableSharedFragmentAccess() = default;

	inline TFragment& Get() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline TFragment& operator*() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline TFragment* operator->() const
	{
		return Fragment;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddSharedRequirement<TFragment>(EMassFragmentAccess::ReadWrite);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		Fragment = &(Context.GetSharedFragment<TFragment>());
	}

	TFragment* Fragment = nullptr;
};

// 【只读 const shared fragment】常用于"配置型"共享数据（如调速参数）
template<typename TFragment>
struct FConstSharedFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FConstSharedFragmentAccess() = default;

	inline const TFragment& Get() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline const TFragment& operator*() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline const TFragment* operator->() const
	{
		return Fragment;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddConstSharedRequirement<TFragment>();
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		Fragment = &Context.GetConstSharedFragment<TFragment>();
	}

	const TFragment* Fragment = nullptr;
};

// 【可写 chunk fragment】每个 chunk 内所有 entity 共享一个对象（可读写）
template<typename TFragment>
struct FMutableChunkFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FMutableChunkFragmentAccess() = default;

	inline TFragment& Get() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline TFragment& operator*() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline TFragment* operator->() const
	{
		return Fragment;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddChunkRequirement<TFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		Fragment = Context.GetMutableChunkFragmentPtr<TFragment>();
	}

	TFragment* Fragment = nullptr;
};

// 【只读 chunk fragment】
template<typename TFragment>
struct FConstChunkFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FConstChunkFragmentAccess() = default;

	inline const TFragment& Get() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline const TFragment& operator*() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline const TFragment* operator->() const
	{
		return Fragment;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddChunkRequirement<TFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		Fragment = Context.GetChunkFragmentPtr<TFragment>();
	}

	const TFragment* Fragment = nullptr;
};

// 【可写 + Optional chunk fragment】注意 Get() 内 check(Fragment) —— 如果是 optional 且不存在，调 Get() 会断言；
// 应先用 operator bool() 判定存在再读取。
template<typename TFragment>
struct FMutableOptionalChunkFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FMutableOptionalChunkFragmentAccess() = default;

	inline TFragment* Get() const
	{
		check(Fragment);
		return Fragment;
	}

	inline TFragment& operator*() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline TFragment* operator->() const
	{
		return Fragment;
	}

	inline operator bool() { return Fragment != nullptr; }

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddChunkRequirement<TFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		Fragment = Context.GetMutableChunkFragmentPtr<TFragment>();
	}

	TFragment* Fragment = nullptr;
};

// 【只读 + Optional chunk fragment】
template<typename TFragment>
struct FConstOptionalChunkFragmentAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TFragment;

	FConstOptionalChunkFragmentAccess() = default;

	inline const TFragment* Get()
	{
		check(Fragment);
		return Fragment;
	}

	inline const TFragment& operator*() const
	{
		check(Fragment);
		return *Fragment;
	}

	inline const TFragment* operator->() const
	{
		return Fragment;
	}

	inline operator bool() const
	{
		return Fragment != nullptr;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		EntityQuery.AddChunkRequirement<TFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
		Fragment = Context.GetChunkFragmentPtr<TFragment>();
	}

	const TFragment* Fragment = nullptr;
};

// 【可写 subsystem】注意 SetupForExecute 而非 SetupForChunk —— subsystem 在 Execute 级别绑定一次即可
template<typename TSubsystem, typename = typename TEnableIf<TIsDerivedFrom<TSubsystem, USubsystem>::IsDerived>::Type>
struct FMutableSubsystemAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TSubsystem;

	FMutableSubsystemAccess() = default;

	inline TSubsystem* Get() const
	{
		return Subsystem;
	}

	inline TSubsystem& operator*() const
	{
		check(Subsystem);
		return *Subsystem;
	}

	inline TSubsystem* operator->() const
	{
		return Subsystem;
	}

	inline operator bool() const
	{
		return Subsystem != nullptr;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		// 注意：subsystem 需求添加到 ProcessorRequirements 而非 EntityQuery（subsystem 是 query 级共享的）
		ProcessorRequirements.AddSubsystemRequirement<TSubsystem>(EMassFragmentAccess::ReadWrite);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
		Subsystem = Context.GetMutableSubsystem<TSubsystem>();
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
	}

	TSubsystem* Subsystem = nullptr;
};

// 【只读 subsystem】
template<typename TSubsystem, typename = typename TEnableIf<TIsDerivedFrom<TSubsystem, USubsystem>::IsDerived>::Type>
struct FConstSubsystemAccess
{
	template<typename... Ts>
	friend struct FQueryDefinition;

	using FragmentType = TSubsystem;

	FConstSubsystemAccess() = default;

	inline const TSubsystem* Get() const
	{
		return Subsystem;
	}

	inline const TSubsystem& operator*() const
	{
		check(Subsystem);
		return *Subsystem;
	}

	inline const TSubsystem* operator->() const
	{
		return Subsystem;
	}

	inline operator bool() const
	{
		return Subsystem != nullptr;
	}

	inline void ConfigureQuery(FMassEntityQuery& EntityQuery, FMassSubsystemRequirements& ProcessorRequirements) const
	{
		ProcessorRequirements.AddSubsystemRequirement<TSubsystem>(EMassFragmentAccess::ReadOnly);
	}

	inline void SetupForExecute(FMassExecutionContext& Context)
	{
		Subsystem = Context.GetSubsystem(TSubsystem::StaticClass());
	}

	inline void SetupForChunk(FMassExecutionContext& Context)
	{
	}

	const TSubsystem* Subsystem = nullptr;
};


} // namespace UE::Mass

#undef UE_API
