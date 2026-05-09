// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverRegistry.cpp】
// UMassObserverRegistry 的实现：
//   - 构造：检查必须为 CDO（强制单例），订阅模块卸载事件；
//   - RegisterObserver：判断 type 是 fragment 还是 tag，按 op 位标志写入对应 map；
//   - OnModulePackagesUnloaded：清理被卸载模块中的 observer class 引用。
// =============================================================================

#include "MassObserverRegistry.h"
#include "Algo/Find.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassObserverRegistry)

//----------------------------------------------------------------------//
// UMassObserverRegistry
//----------------------------------------------------------------------//
// 【中文注释】构造：强制断言这是 CDO（class default object）。
// 因为我们用 GetDefault<UMassObserverRegistry>() 实现单例，
// 任何非 CDO 实例都是误用，立即崩溃以暴露问题。
// 同时订阅"模块卸载"事件，热更新时清理失效引用。
UMassObserverRegistry::UMassObserverRegistry()
{
	// there can be only one!
	check(HasAnyFlags(RF_ClassDefaultObject));

	if (!ModulesUnloadedHandle.IsValid())
	{
		ModulesUnloadedHandle = FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate.AddUObject(this, &UMassObserverRegistry::OnModulePackagesUnloaded);
	}
}

// 【中文注释】RegisterObserver：把 (type, op_flags, observer_class) 三元组写入注册表。
// 流程：
//   1. 判断 type 是 FMassFragment 还是 FMassTag 的子类型，选择对应的 map 数组；
//   2. 遍历每个 op 位，如果在 OperationFlags 中：
//      把 ObserverClass 的 SoftClassPath 加入到 ObserversMap[op][type] 数组（去重）；
// 注意：这里只存 SoftClassPath，class 还未实例化为 processor —— 实例化推迟到 ObserverManager.Initialize。
void UMassObserverRegistry::RegisterObserver(TNotNull<const UScriptStruct*> ObservedType, const uint8 OperationFlags, TSubclassOf<UMassProcessor> ObserverClass)
{
	check(ObserverClass);
	const bool bIsFragment = UE::Mass::IsA<FMassFragment>(ObservedType);
	checkSlow(bIsFragment || UE::Mass::IsA<FMassTag>(ObservedType));

	FObserverClassesMap* ObserversMap = bIsFragment
		? FragmentObserverMaps
		: TagObserverMaps; 

	for (uint8 OperationIndex = 0; OperationIndex < static_cast<uint8>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		if ((OperationFlags & (1 << OperationIndex)))
		{
			// 【中文注释】FindOrAdd 拿到 type 对应的 class 数组（不存在则新建空数组），AddUnique 去重追加。
			ObserversMap[OperationIndex].FindOrAdd(ObservedType).AddUnique(FSoftClassPath(ObserverClass));
		}
	}
}

// 【中文注释】单一 op 的便捷重载：把 EMassObservedOperation 转成对应 bit 位后调用主版本。
void UMassObserverRegistry::RegisterObserver(const UScriptStruct& ObservedType, const EMassObservedOperation Operation, TSubclassOf<UMassProcessor> ObserverClass)
{
	RegisterObserver(&ObservedType, static_cast<uint8>(1 << static_cast<uint8>(Operation)), ObserverClass);
}

// 【中文注释】OnModulePackagesUnloaded：模块热更新时清理失效 observer class。
// 流程（双层嵌套清理）：
//   1. 外层：遍历每个 op × {fragment, tag} 维度的 map；
//   2. 中层：遍历 map 中的每个 type → [class paths] 条目；
//   3. 内层：遍历 class paths 数组，检查每个 path 的 package 是否在 Packages 列表里，是则删除；
//   4. 如果某 type 的 class paths 数组变空，连同整个 map 条目一起删除。
//
// 注意：此函数处理的是 SoftClassPath 引用，不涉及实际 processor 实例
// （实例由 FMassObserverManager.OnModulePackagesUnloaded 单独清理）。
void UMassObserverRegistry::OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMassObserverRegistry::OnModulePackagesUnloaded);

	const auto ProcessObserversMap = [&Packages](FObserverClassesMap& ObserversMap)
	{
		for (auto MapIt = ObserversMap.CreateIterator(); MapIt; ++MapIt)
		{
			TArray<FSoftClassPath>& ObserverClasses = MapIt->Value;

			for (auto ObserverIt = ObserverClasses.CreateIterator(); ObserverIt; ++ObserverIt)
			{
				const FSoftClassPath& ObservedClass = *ObserverIt;
				const FName PackageName = ObservedClass.GetLongPackageFName();

				// 【中文注释】用 Algo::FindByPredicate 在 Packages 中查找匹配的 PackageName。
				// !! 是把指针转成 bool（找到非空则 true）。
				bool bRemove = !!Algo::FindByPredicate(Packages, [PackageName](const UPackage* Package)
				{
					return PackageName == Package->GetFName();
				});

				if (bRemove)
				{
					UE_LOG(LogMass, Log, TEXT("%hs: removed observer %s (%s)"), __FUNCTION__, *ObservedClass.ToString(), *PackageName.ToString());
					ObserverIt.RemoveCurrent();
				}
			}

			// 【中文注释】type 对应的 class paths 已空 → 移除整个 map 条目，避免 map 中残留无效 type。
			if (ObserverClasses.Num() == 0)
			{
				MapIt.RemoveCurrent();
			}
		}
	};

	for (uint8 OperationIndex = 0; OperationIndex < static_cast<uint8>(EMassObservedOperation::MAX); ++OperationIndex)
	{
		ProcessObserversMap(FragmentObserverMaps[OperationIndex]);
		ProcessObserversMap(TagObserverMaps[OperationIndex]);
	}
}
