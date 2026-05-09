// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSubsystemAccess.h"


namespace UE::Mass::Private
{
	/**
	 * 把一个 TSubclassOf<USubsystem> 重新解释为 TSubclassOf<T>（其中 T 是 USubsystem 的某个具体派生）。
	 *
	 * 为什么用 reinterpret_cast 而不是普通赋值/构造？
	 *  - TSubclassOf<X> 内部只是一个 UClass*；TSubclassOf<X> 与 TSubclassOf<Y>
	 *    的二进制布局完全相同。
	 *  - 但 TSubclassOf<X> 的构造函数会做静态类型校验（要求源类型派生自 X），
	 *    在这里我们已经通过 IsChildOf 在调用前手动验证过派生关系，
	 *    希望绕过模板的派生约束直接传给 GetEngineSubsystemBase / GetSubsystemBase 等
	 *    需要"具体基类型 TSubclassOf"的 API。
	 *  - 这是 UE 内部惯用法（仅安全于布局完全相同的 TSubclassOf 模板间）。
	 */
	template<typename T>
	TSubclassOf<T> ConvertToSubsystemClass(TSubclassOf<USubsystem> SubsystemClass)
	{
		return *(reinterpret_cast<TSubclassOf<T>*>(&SubsystemClass));
	}
}

//-----------------------------------------------------------------------------
// FMassSubsystemAccess
//-----------------------------------------------------------------------------
/**
 * 构造时把 Subsystems 数组按 FMassExternalSubsystemBitSet 当前已知类型最大数量预分配。
 * 这样后续按 SystemIndex 直接 `[]` 访问通常都不需要扩容；只有运行时新注册了类型才会触发扩容（罕见）。
 */
FMassSubsystemAccess::FMassSubsystemAccess(UWorld* InWorld)
	: World(InWorld)
{
	Subsystems.AddZeroed(FMassExternalSubsystemBitSet::GetMaxNum());
}

/**
 * 运行时类型版的 fetch 实现：
 *  - 根据 SubsystemClass 沿继承链判断属于 5 类中哪一种，调对应的 GetSubsystemBase。
 *  - World 缺失或 GameInstance/LocalPlayer 链路断开时，返回 nullptr 而非崩溃，
 *    这是为了让 PIE/编辑器初始化等 World 尚未就绪的场景能容忍性失败。
 *  - 由于此函数会被 query 第一次执行时多次调用，这里加了一个 QUICK_SCOPE_CYCLE_COUNTER 用于性能埋点。
 */
USubsystem* FMassSubsystemAccess::FetchSubsystemInstance(UWorld* World, TSubclassOf<USubsystem> SubsystemClass)
{
	QUICK_SCOPE_CYCLE_COUNTER(Mass_FetchSubsystemInstance);

	check(SubsystemClass);
	if (SubsystemClass->IsChildOf<UWorldSubsystem>())
	{
		return World 
			? World->GetSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UWorldSubsystem>(SubsystemClass)) 
			: nullptr;
	}
	if (SubsystemClass->IsChildOf<UEngineSubsystem>())
	{
		// UEngineSubsystem 不依赖 World，直接走 GEngine。
		return GEngine->GetEngineSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UEngineSubsystem>(SubsystemClass));
	}
	if (SubsystemClass->IsChildOf<UGameInstanceSubsystem>())
	{
		return (World && World->GetGameInstance())
			? World->GetGameInstance()->GetSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UGameInstanceSubsystem>(SubsystemClass))
			: nullptr;
	}
	if (SubsystemClass->IsChildOf<ULocalPlayerSubsystem>())
	{
		// 与模板版 FetchSubsystemInstance 一致：只取第一个 LocalPlayer。
		// 多人本地分屏需求请走自定义路径。
		const ULocalPlayer* LocalPlayer = World ? World->GetFirstLocalPlayerFromController() : nullptr;
		return LocalPlayer
			? LocalPlayer->GetSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<ULocalPlayerSubsystem>(SubsystemClass))
			: nullptr;
	}
#if WITH_EDITOR
	if (SubsystemClass->IsChildOf<UEditorSubsystem>())
	{
		// 仅编辑器：UEditorSubsystem 由 GEditor 持有。
		return GEditor->GetEditorSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UEditorSubsystem>(SubsystemClass));
	}
#endif // WITH_EDITOR
	// 所有 5 种基类都未命中：可能是非法 SubsystemClass，返回 nullptr 让上层 ensure。
	return nullptr;
}

/**
 * 把 query 声明的"我会用到这些 subsystem"一次性翻成实际指针：
 *  1) 遍历 ConstSubsystems 位 → CacheSubsystem(SystemIndex)
 *  2) 遍历 MutableSubsystems 位 → CacheSubsystem(SystemIndex)
 *  3) 全部 fetch 成功后才更新自身的 BitSet 副本（保证语义原子化：要么都准备好了，要么 BitSet 不变）。
 *
 * @return 是否全部 subsystem 都成功 fetch；若任意一个失败立即停止后续 fetch 并返回 false。
 *
 * 调用约束：通常在 query 第一次执行（或 query 切换）时调一次；
 * 而后整段 chunk 迭代过程都直接读 cached 指针。
 */
bool FMassSubsystemAccess::CacheSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
{
	bool bResult = true;

	if (SubsystemRequirements.IsEmpty() == false)
	{
		// 注意：循环条件中带 && bResult，第一次 fetch 失败就会短路退出。
		for (FMassExternalSubsystemBitSet::FIndexIterator It = SubsystemRequirements.GetRequiredConstSubsystems().GetIndexIterator(); It && bResult; ++It)
		{
			bResult = bResult && CacheSubsystem(*It);
		}

		for (FMassExternalSubsystemBitSet::FIndexIterator It = SubsystemRequirements.GetRequiredMutableSubsystems().GetIndexIterator(); It && bResult; ++It)
		{
			bResult = bResult && CacheSubsystem(*It);
		}
	}

	if (bResult)
	{
		// 仅在全部 fetch 成功时才更新位掩码；失败情况下保持上一次状态以避免错位访问。
		ConstSubsystemsBitSet = SubsystemRequirements.GetRequiredConstSubsystems();
		MutableSubsystemsBitSet = SubsystemRequirements.GetRequiredMutableSubsystems();
	}

	return bResult;
}

/**
 * 单个 SystemIndex 的 fetch + 缓存：
 *  - 如已缓存，直接复用；
 *  - 否则用 FMassExternalSubsystemBitSet 的反查 API 取 UClass*，再走运行时版 FetchSubsystemInstance。
 *
 * @return 是否 fetch 到非空实例。
 *
 * @note 与模板版 GetSubsystemInternal 不同：那里有 DoesRequireWorld<T>() 的编译期分支，
 *       这里只能走运行时多分支判断（参考 FetchSubsystemInstance(UWorld*, TSubclassOf)）。
 */
bool FMassSubsystemAccess::CacheSubsystem(const uint32 SystemIndex)
{
	if (UNLIKELY(Subsystems.IsValidIndex(SystemIndex) == false))
	{
		Subsystems.AddZeroed(Subsystems.Num() - SystemIndex + 1);
	}

	if (Subsystems[SystemIndex])
	{
		// 已 cache，跳过。
		return true;
	}

	// 通过位集类型表反查得到 UClass*。
	const UClass* SubsystemClass = FMassExternalSubsystemBitSet::GetTypeAtIndex(SystemIndex);
	checkSlow(SubsystemClass);

	// const_cast：FMassExternalSubsystemBitSet 出于不可变查表的设计返回 const UClass*；
	// 而 TSubclassOf 需要 UClass*。这里只用作类型 key，不会修改 UClass，因此安全。
	TSubclassOf<USubsystem> SubsystemSubclass(const_cast<UClass*>(SubsystemClass));
	checkSlow(*SubsystemSubclass);

	if (SubsystemSubclass)
	{
		USubsystem* SystemInstance = FMassSubsystemAccess::FetchSubsystemInstance(World.Get(), SubsystemSubclass);
		Subsystems[SystemIndex] = SystemInstance;
		return SystemInstance != nullptr;
	}

	return false;
}

/**
 * 仅设置位掩码（不真正 fetch）。
 * 用于：当外层已经知道所需 subsystem 的指针在本对象内已 cached（如父 context 已经执行过 cache），
 * 只是需要切换"当前 query 关心的 subsystem 集合"时。
 */
void FMassSubsystemAccess::SetSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
{
	ConstSubsystemsBitSet = SubsystemRequirements.GetRequiredConstSubsystems();
	MutableSubsystemsBitSet = SubsystemRequirements.GetRequiredMutableSubsystems();
}
