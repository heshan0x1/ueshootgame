// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassRequirements.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"

#if WITH_EDITOR
// 编辑器场景：需要 GEditor 才能取到 UEditorSubsystem。
#include "Editor.h"
#include "EditorSubsystem.h"
#else
// 运行时（Game/Server/Client）：只需 GEngine 即可访问 UEngineSubsystem，
// 不存在 UEditorSubsystem。
#include "Engine/Engine.h"
#endif // WITH_EDITOR


/**
 * FMassSubsystemAccess —— Mass 框架中"按位集索引访问 USubsystem 派生实例"的辅助器。
 *
 * 设计动机：
 *  - Mass 的 query 会预先声明它需要哪些 USubsystem（见 FMassSubsystemRequirements）；
 *    这些声明在 query 第一次执行时被解析为一组 FMassExternalSubsystemBitSet 位。
 *  - 但是 USubsystem 派生有 5 种、获取它们的 API 各不相同（见下表），不能用统一调用搞定：
 *      * UEngineSubsystem        —— GEngine->GetEngineSubsystem<T>()
 *      * UEditorSubsystem        —— GEditor->GetEditorSubsystem<T>()  (仅 WITH_EDITOR)
 *      * UWorldSubsystem         —— UWorld::GetSubsystem<T>(World)
 *      * UGameInstanceSubsystem  —— UGameInstance::GetSubsystem<T>(World->GetGameInstance())
 *      * ULocalPlayerSubsystem   —— ULocalPlayer::GetSubsystem<T>(World->GetFirstLocalPlayer())
 *    因此本类提供 FetchSubsystemInstance 的多个 constexpr-if 分支统一封装这一查询。
 *  - 查询每帧/每 chunk 都需访问 subsystem，每次去走 UWorld::GetSubsystem 这种线性查找开销不小，
 *    所以一旦 fetch 成功就把指针缓存进 Subsystems[SystemIndex]，后续命中缓存即可。
 *
 * 索引方式：
 *  - 所有 USubsystem 的子类都被注册进 FMassExternalSubsystemBitSet 的全局类型表，
 *    每个具体子类有唯一 SystemIndex；本类内部用 TArray<USubsystem*> Subsystems
 *    以 SystemIndex 直接定位（稀疏，可能会有 nullptr 槽位）。
 *  - 同时维护两个 BitSet：ConstSubsystemsBitSet / MutableSubsystemsBitSet，
 *    用来在 GetSubsystem/GetMutableSubsystem 调用时校验"此 subsystem 是否在 query 声明中"，
 *    若未声明而被访问会触发 ensure 报警（这是 Mass 的访问契约）。
 *
 * 生命周期：
 *  - 通常作为 FMassExecutionContext 的成员存在；当 ExecutionContext 切换 query 时，
 *    BitSet 会被相应替换；缓存的指针保留（同一个 World 内的 Subsystem 不会变）。
 */
struct FMassSubsystemAccess
{
	/**
	 * @param InWorld 关联的 UWorld；用于解析"需要 World 的"那 3 类 Subsystem。
	 *                若仅访问 UEngineSubsystem / UEditorSubsystem，可传 nullptr。
	 */
	MASSENTITY_API explicit FMassSubsystemAccess(UWorld* InWorld = nullptr);

	//-----------------------------------------------------------------------------
	// Statically-typed subsystems
	// 静态类型版本：模板参数 T 直接给出具体 Subsystem 类，编译期决定 fetch 路径。
	//-----------------------------------------------------------------------------
	/**
	 * 取一个允许"可写"访问的 subsystem 指针。
	 * - SFINAE 限制：T 必须派生自 USubsystem。
	 * - 通过 query 声明的 MutableSubsystemsBitSet 校验访问权限；未声明会 ensure 报警并返回 nullptr。
	 * - 第一次访问会触发 fetch 并缓存到 Subsystems[SystemIndex]，后续直接命中。
	 * @return 该 subsystem 的实例；如果不存在或访问未声明则返回 nullptr。
	 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T* GetMutableSubsystem()
	{
		// 由静态注册表中得到该具体 subsystem 类型的索引（编译期常量级别）
		const uint32 SystemIndex = FMassExternalSubsystemBitSet::GetTypeIndex<T>();
		// 校验 query 是否声明过对该 subsystem 的可写需求
		if (ensureMsgf(MutableSubsystemsBitSet.IsBitSet(SystemIndex)
			, TEXT("Missing 'template<> struct TMassExternalSubsystemTraits<U%s>' for this subsystem"), *GetNameSafe(T::StaticClass())))
		{
			return GetSubsystemInternal<T>(SystemIndex);
		}

		return nullptr;
	}

	/**
	 * 与 GetMutableSubsystem 类似，但断言指针非空（命中失败会 check 崩溃）。
	 * 适用于"调用者已确认该 subsystem 一定存在"的 hotpath。
	 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T& GetMutableSubsystemChecked()
	{
		T* InstancePtr = GetMutableSubsystem<T>();
		check(InstancePtr);
		return *InstancePtr;
	}

	/**
	 * 取一个只读访问的 subsystem 指针。
	 * - 既允许 ConstSubsystemsBitSet 命中也允许 MutableSubsystemsBitSet 命中
	 *   （Mutable 包含 Const 权限）。
	 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T* GetSubsystem()
	{
		const uint32 SystemIndex = FMassExternalSubsystemBitSet::GetTypeIndex<T>();
		if (ensureMsgf(ConstSubsystemsBitSet.IsBitSet(SystemIndex) || MutableSubsystemsBitSet.IsBitSet(SystemIndex)
			, TEXT("Missing 'template<> struct TMassExternalSubsystemTraits<U%s>' for this subsystem"), *GetNameSafe(T::StaticClass())))
		{
			return GetSubsystemInternal<T>(SystemIndex);
		}
		return nullptr;
	}

	/** 与 GetSubsystem 同语义，但断言非空。 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T& GetSubsystemChecked()
	{
		const T* InstancePtr = GetSubsystem<T>();
		check(InstancePtr);
		return *InstancePtr;
	}

	//-----------------------------------------------------------------------------
	// UClass-provided subsystems
	// 运行时类型版本：模板参数 T 给出基类（如 UWorldSubsystem），
	// 真实子类由 SubsystemClass 在运行时给出。常用于蓝图/数据驱动场景，
	// 无法在编译期确定 SystemIndex，因此走 GetTypeIndex(UClass) 的运行时映射。
	//-----------------------------------------------------------------------------
	/** 运行时版可写 Get：通过 SubsystemClass 在 BitSet 类型表中查 SystemIndex。 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T* GetMutableSubsystem(const TSubclassOf<USubsystem> SubsystemClass)
	{
		// **SubsystemClass：先 *(取 UClass*) 再 *(deref) 得到 UClass& —— 用作类型表 key。
		const uint32 SystemIndex = FMassExternalSubsystemBitSet::GetTypeIndex(**SubsystemClass);
		if (ensureMsgf(MutableSubsystemsBitSet.IsBitSet(SystemIndex)
			, TEXT("Missing 'template<> struct TMassExternalSubsystemTraits<U%s>' for this subsystem"), *GetNameSafe(SubsystemClass)))
		{
			return GetSubsystemInternal<T>(SystemIndex, SubsystemClass);
		}

		return nullptr;
	}

	/** 运行时版可写 Checked。 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T& GetMutableSubsystemChecked(const TSubclassOf<USubsystem> SubsystemClass)
	{
		T* InstancePtr = GetMutableSubsystem<T>(SubsystemClass);
		check(InstancePtr);
		return *InstancePtr;
	}

	/** 运行时版只读 Get。 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T* GetSubsystem(const TSubclassOf<USubsystem> SubsystemClass)
	{
		const uint32 SystemIndex = FMassExternalSubsystemBitSet::GetTypeIndex(**SubsystemClass);
		if (ensureMsgf(ConstSubsystemsBitSet.IsBitSet(SystemIndex) || MutableSubsystemsBitSet.IsBitSet(SystemIndex)
			, TEXT("Missing 'template<> struct TMassExternalSubsystemTraits<U%s>' for this subsystem"), *GetNameSafe(SubsystemClass)))
		{
			return GetSubsystemInternal<T>(SystemIndex, SubsystemClass);
		}
		return nullptr;
	}

	/** 运行时版只读 Checked。 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T& GetSubsystemChecked(const TSubclassOf<USubsystem> SubsystemClass)
	{
		const T* InstancePtr = GetSubsystem<T>(SubsystemClass);
		check(InstancePtr);
		return *InstancePtr;
	}

	//-----------------------------------------------------------------------------
	// remaining API
	//-----------------------------------------------------------------------------
	/**
	 * 根据 query 的 SubsystemRequirements 一次性 fetch 并缓存所有需要的 subsystem 指针。
	 * 通常在 query 第一次执行时调用一次，后续整段 query 执行期间直接命中缓存。
	 * @return 是否所有声明的 subsystem 都成功拿到了实例（任意一个失败即返回 false）。
	 */
	MASSENTITY_API bool CacheSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements);

	/**
	 * 仅复制 BitSet 声明，不真正 fetch 实例。
	 * 用在"已经知道实例缓存可复用，但需切换访问权限位掩码"的场景。
	 */
	MASSENTITY_API void SetSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements);

	/** 输出当前的 Const/Mutable 位掩码副本。供 ExecutionContext 在 push/pop query 时保存现场。 */
	void GetSubsystemRequirementBits(FMassExternalSubsystemBitSet& OutConstSubsystemsBitSet, FMassExternalSubsystemBitSet& OutMutableSubsystemsBitSet)
	{
		OutConstSubsystemsBitSet = ConstSubsystemsBitSet;
		OutMutableSubsystemsBitSet = MutableSubsystemsBitSet;
	}

	/** 直接覆盖 Const/Mutable 位掩码。供 ExecutionContext 在 pop query 时恢复现场。 */
	void SetSubsystemRequirementBits(const FMassExternalSubsystemBitSet& InConstSubsystemsBitSet, const FMassExternalSubsystemBitSet& InMutableSubsystemsBitSet)
	{
		ConstSubsystemsBitSet = InConstSubsystemsBitSet;
		MutableSubsystemsBitSet = InMutableSubsystemsBitSet;
	}

	/**
	 * 编译期判断：T 是否需要 UWorld 才能 fetch。
	 * - UWorldSubsystem / UGameInstanceSubsystem / ULocalPlayerSubsystem 都依赖 World 链路；
	 * - 反之，UEngineSubsystem / UEditorSubsystem 只需 GEngine / GEditor 全局，不需要 World。
	 * 该 constexpr 函数用于 GetSubsystemInternal 的两条 fetch 分支选择。
	 */
	template<typename T>
	static constexpr bool DoesRequireWorld()
	{
		constexpr bool bIsWorldSubsystem = TIsDerivedFrom<T, UWorldSubsystem>::IsDerived;
		constexpr bool bIsGameInstanceSubsystem = TIsDerivedFrom<T, UGameInstanceSubsystem>::IsDerived;
		constexpr bool bIsLocalPlayerSubsystem = TIsDerivedFrom<T, ULocalPlayerSubsystem>::IsDerived;

		return (bIsWorldSubsystem || bIsGameInstanceSubsystem || bIsLocalPlayerSubsystem);
	}

	/**
	 * 静态 fetch 入口（需要 World 的那 3 类）：根据 T 静态选择正确的 GetSubsystem 系列 API。
	 * - 注意 ULocalPlayerSubsystem 的默认实现只取 World 的"第一个 LocalPlayer"，
	 *   多人本地分屏游戏需要为目标 T 模板特化此函数以指明 player 索引。
	 * - 不需要 World 的 subsystem 类应改用无参重载；落入 else 分支会 checkf 崩溃。
	 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	static T* FetchSubsystemInstance(UWorld* World)
	{
		check(World);
		if constexpr (TIsDerivedFrom<T, UWorldSubsystem>::IsDerived)
		{
			return UWorld::GetSubsystem<T>(World);
		}
		else if constexpr (TIsDerivedFrom<T, UGameInstanceSubsystem>::IsDerived)
		{
			return UGameInstance::GetSubsystem<T>(World->GetGameInstance());
		}
		else if constexpr (TIsDerivedFrom<T, ULocalPlayerSubsystem>::IsDerived)
		{
			// note that this default implementation will work only for the first player in a local-coop game
			// to customize this behavior specialize the FetchSubsystemInstance template function for the type you need. 
			// 注：此默认实现只对本地合作中的"第一个 LocalPlayer"有效；
			// 如需为其他 player 索引使用，请对该 T 特化本模板函数。
			return ULocalPlayer::GetSubsystem<T>(World->GetFirstLocalPlayerFromController());
		}
		else
		{
			// 不应到达：意味着 T 既不属于 World/GameInstance/LocalPlayer 三类，又调了带 World 的版本
			checkf(false, TEXT("FMassSubsystemAccess::FetchSubsystemInstance: Unhandled world-related USubsystem class %s"), *T::StaticClass()->GetName());
		}
	}
	
	/**
	 * 静态 fetch 入口（无 World 的那 2 类）：用于 UEngineSubsystem / UEditorSubsystem。
	 * UEditorSubsystem 分支仅在 WITH_EDITOR 下才编译进来。
	 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	static T* FetchSubsystemInstance()
	{
		if constexpr (TIsDerivedFrom<T, UEngineSubsystem>::IsDerived)
		{
			return GEngine->GetEngineSubsystem<T>();
		}
#if WITH_EDITOR
		else if constexpr (TIsDerivedFrom<T, UEditorSubsystem>::IsDerived)
		{
			return GEditor->GetEditorSubsystem<T>();
		}
#endif // WITH_EDITOR
		else
		{
			checkf(false, TEXT("FMassSubsystemAccess::FetchSubsystemInstance: Unhandled world-less USubsystem class %s"), *T::StaticClass()->GetName());
		}
	}

	/**
	 * 运行时版 fetch（基于 UClass）：根据 SubsystemClass 的实际派生关系，
	 * 选择对应 GetSubsystemBase API。实现在 .cpp 中。
	 * 这是给"运行时类型未知（如蓝图）"的调用路径用的回退方案。
	 */
	static MASSENTITY_API USubsystem* FetchSubsystemInstance(UWorld* World, TSubclassOf<USubsystem> SubsystemClass);

protected:
	/**
	 * 内部 fetch + 缓存：
	 * - 若 Subsystems 数组装不下 SystemIndex，原地扩容（很少触发）。
	 * - 命中缓存直接返回；未命中则按 DoesRequireWorld<T>() 走静态 fetch 路径。
	 * @note std::remove_const_t 是为了支持 GetSubsystem<const T> 路径，
	 *       FetchSubsystemInstance 的模板参数需要去 const 才能匹配静态 API。
	 */
	template<typename T>
	T* GetSubsystemInternal(const uint32 SystemIndex)
	{
		if (UNLIKELY(Subsystems.IsValidIndex(SystemIndex) == false))
		{
			Subsystems.AddZeroed(Subsystems.Num() - SystemIndex + 1);
		}

		T* SystemInstance = (T*)Subsystems[SystemIndex];
		if (SystemInstance == nullptr)
		{
			if constexpr (DoesRequireWorld<T>())
			{
				SystemInstance = FetchSubsystemInstance<std::remove_const_t<T>>(World.Get());
			}
			else
			{
				SystemInstance = FetchSubsystemInstance<std::remove_const_t<T>>();
			}
			Subsystems[SystemIndex] = SystemInstance;
		}
		return SystemInstance;
	}

	/**
	 * 运行时类型版的内部 fetch + 缓存。
	 * 走的是带 SubsystemClass 参数的运行时 FetchSubsystemInstance 重载。
	 */
	template<typename T>
	T* GetSubsystemInternal(const uint32 SystemIndex, const TSubclassOf<USubsystem> SubsystemClass)
	{
		if (UNLIKELY(Subsystems.IsValidIndex(SystemIndex) == false))
		{
			Subsystems.AddZeroed(Subsystems.Num() - SystemIndex + 1);
		}

		USubsystem* SystemInstance = (T*)Subsystems[SystemIndex];
		if (SystemInstance == nullptr)
		{
			SystemInstance = FetchSubsystemInstance(World.Get(), SubsystemClass);
			Subsystems[SystemIndex] = SystemInstance;
		}
		return Cast<T>(SystemInstance);
	}

	/**
	 * 仅缓存（不返回）：CacheSubsystemRequirements 内部对每一个 SystemIndex 调用此函数。
	 * 实现在 .cpp 中（因为需要从 SystemIndex 反查 UClass*，依赖运行时反射）。
	 * @return fetch 是否成功（拿到了非空实例）。
	 */
	MASSENTITY_API bool CacheSubsystem(const uint32 SystemIndex);

	/** 当前关联 query 声明的"只读访问"subsystem 位掩码。 */
	FMassExternalSubsystemBitSet ConstSubsystemsBitSet;
	/** 当前关联 query 声明的"可写访问"subsystem 位掩码（隐含包含只读权限）。 */
	FMassExternalSubsystemBitSet MutableSubsystemsBitSet;
	/** 缓存槽：以 FMassExternalSubsystemBitSet 的 SystemIndex 为下标存放裸指针，未 fetch 处为 nullptr。 */
	TArray<USubsystem*> Subsystems;
	/** 关联的 World，弱引用避免阻碍 GC；fetch 需要 World 的 subsystem 时使用。 */
	TWeakObjectPtr<UWorld> World;
};
