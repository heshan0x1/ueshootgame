// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntitySettings.h"
#include "MassProcessingPhaseManager.h"
#include "VisualLogger/VisualLogger.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CoreDelegates.h"
#include "MassArchetypeData.h"
#include "Algo/RemoveIf.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntitySettings)

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#include "CoreGlobals.h"
#endif // WITH_EDITOR

// =============================================================================================
// MassEntitySettings.cpp —— UMassEntitySettings 的实现
// ---------------------------------------------------------------------------------------------
// 本文件涵盖：
//   1) 构造时对 ProcessingPhasesConfig 各 phase 名做默认填充，并订阅 OnPostEngineInit。
//   2) BuildProcessorList()    —— 扫描所有 UMassProcessor 派生 CDO 的算法。
//   3) BuildPhases()           —— 编辑器下：把 phase 内的 processor CDO 包装为 PhaseProcessor 用于 UI 展示。
//   4) Hot-unload 处理         —— GameFeaturePlugin 卸载时同步清理失效的 CDO 引用。
//   5) PostEditChangeProperty —— 用户改了 UI 之后的传播：重建 → 广播。
// =============================================================================================


//----------------------------------------------------------------------//
//  UMassEntitySettings
//----------------------------------------------------------------------//
UMassEntitySettings::UMassEntitySettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 给每个 phase 设置默认显示名（来自 EMassProcessingPhase 枚举的 DisplayName meta）。
	// 这样即使用户从未在 ini 中配置过 ProcessingPhasesConfig，UI 上也能看到正确的 phase 名。
	for (int i = 0; i < (int)EMassProcessingPhase::MAX; ++i)
	{
		ProcessingPhasesConfig[i].PhaseName = *UEnum::GetDisplayValueAsText(EMassProcessingPhase(i)).ToString();
	}

	// 订阅"引擎初始化完成"事件 —— 我们必须等所有 UClass 反射注册完才能扫描派生类。
	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UMassEntitySettings::OnPostEngineInit);

	// we need to get notified about modules being unloaded (like Game Feature Plugins) so that we can remove
	// stored CDOs originating from the modules being removed.
	// 中文：订阅"模块卸载"事件 —— 当 Game Feature Plugin 之类的模块被卸下时，
	//      其包内的 UMassProcessor CDO 也会跟着销毁；我们必须从 ProcessorCDOs / PhaseConfig
	//      中清理掉这些悬空指针，否则后续遍历将访问已释放对象。
	FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate.AddUObject(this, &UMassEntitySettings::OnModulePackagesUnloaded);
}

void UMassEntitySettings::PostInitProperties()
{
	// 注意：Super 是 UMassModuleSettings，它会触发"自注册到 UMassSettings"的逻辑（见 MassSettings.cpp）。
	Super::PostInitProperties();

	// 校验 ChunkMemorySize：保证它满足 archetype 内存对齐与最小/最大区间约束。
	// SanitizeChunkMemorySize 在不合法时会日志警告并修正成合法值（见 MassArchetypeData.cpp:27）。
	ChunkMemorySize = UE::Mass::SanitizeChunkMemorySize(ChunkMemorySize);
}

void UMassEntitySettings::BeginDestroy()
{
	// 解除所有委托订阅，防止 CDO 销毁后被回调到悬空 this。
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FCoreUObjectDelegates::CompiledInUObjectsRemovedDelegate.RemoveAll(this);
	Super::BeginDestroy();
}

void UMassEntitySettings::OnPostEngineInit()
{
	// 引擎已就绪：所有 UClass 反射数据可用，可以安全地枚举 UMassProcessor 派生。
	bEngineInitialized = true;
	BuildProcessorListAndPhases();
}

void UMassEntitySettings::OnModulePackagesUnloaded(TConstArrayView<UPackage*> Packages)
{
	const int32 InitialNum = ProcessorCDOs.Num();

	// Algo::RemoveIf 是 std::remove_if 风格：返回新尾迭代器位置（== 保留元素的数量）。
	// 然后调用 SetNum 截断尾部 —— 一步完成"移除所有匹配元素"。
	// 移除条件：CDO 为空 或 其所属 Package 在被卸载列表中。
	ProcessorCDOs.SetNum(Algo::RemoveIf(ProcessorCDOs,
		[&Packages](const TObjectPtr<UMassProcessor>& CDO)
		{
			return !CDO || Packages.Contains(CDO->GetPackage());
		})
	);

	if (ProcessorCDOs.Num() != InitialNum)
	{
		// 仅当确有 processor 被移除时才重建 phase 配置（避免不必要的工作）。

		// rebuild the phase configs
		// 步骤 1：先清空所有 phase 的 ProcessorCDOs 列表，避免悬空引用。
		for (FMassProcessingPhaseConfig& PhaseConfig : ProcessingPhasesConfig)
		{
			PhaseConfig.ProcessorCDOs.Reset();
		}

		// 步骤 2：把仍然存活的 CDO 按其 GetProcessingPhase() 重新分桶。
		// 注意只放 ShouldAutoAddToGlobalList()==true 的 processor —— 项目自定义启用关系靠 ini。
		for (TObjectPtr<UMassProcessor> ProcessorCDO : ProcessorCDOs)
		{
			if (ProcessorCDO->ShouldAutoAddToGlobalList())
			{
				ProcessingPhasesConfig[static_cast<int32>(ProcessorCDO->GetProcessingPhase())].ProcessorCDOs.Add(ProcessorCDO);
			}
		}

		// 步骤 3：编辑器下重新生成可视化 PhaseProcessor。
		BuildPhases();
	}
}

void UMassEntitySettings::BuildProcessorListAndPhases()
{
	// 幂等保护：
	//   - 已初始化过则直接返回（重复调用无副作用）
	//   - Engine 还没初始化完则提前返回，等 OnPostEngineInit 来再触发。
	if (bInitialized == true || bEngineInitialized == false)
	{
		return;
	}

	BuildProcessorList(); // 扫描派生类 → 填充 ProcessorCDOs / PhaseConfig.ProcessorCDOs
	BuildPhases();        // 编辑器：实例化 PhaseProcessor 用于 UI
	bInitialized = true;

	// 通知监听者（PhaseManager 等）配置已就绪，可以开始构建运行时 pipeline。
	OnInitializedEvent.Broadcast();
}

void UMassEntitySettings::BuildPhases()
{
#if WITH_EDITOR
	// 仅在编辑器（且确实是编辑器进程）中需要构造 PhaseProcessor 这一可视化对象。
	// runtime/dedicated server 不需要 —— 它们会通过 PhaseManager 自己构造运行时 pipeline。
	if (GIsEditor)
	{
		for (int i = 0; i < int(EMassProcessingPhase::MAX); ++i)
		{
			FMassProcessingPhaseConfig& PhaseConfig = ProcessingPhasesConfig[i];

			// 用配置中的 PhaseGroupClass（一般是 UMassCompositeProcessor 或其派生）实例化一个
			// "phase 容器"。命名为 "ProcessingPhase_<PhaseName>" 以便在 Outliner / 日志中识别。
			PhaseConfig.PhaseProcessor = NewObject<UMassCompositeProcessor>(this, PhaseConfig.PhaseGroupClass
				, *FString::Printf(TEXT("ProcessingPhase_%s"), *PhaseConfig.PhaseName.ToString()));
			PhaseConfig.PhaseProcessor->SetGroupName(PhaseConfig.PhaseName);
			PhaseConfig.PhaseProcessor->SetProcessingPhase(EMassProcessingPhase(i));

			// 若用户配置了 DumpDependencyGraphFileName，则给每个 phase 单独生成一个 .dot 文件名。
			const FString PhaseDumpDependencyGraphFileName = !DumpDependencyGraphFileName.IsEmpty() ? DumpDependencyGraphFileName + TEXT("_") + PhaseConfig.PhaseName.ToString() : FString();

			// 通过临时 RuntimePipeline 把 CDO 列表"实例化"到 PhaseProcessor 内部。
			// EProcessorExecutionFlags::All 表示编辑器视图中显示所有 ExecutionFlags 的 processor。
			FMassRuntimePipeline TmpPipeline(EProcessorExecutionFlags::All);
			TmpPipeline.CreateFromArray(PhaseConfig.ProcessorCDOs, *PhaseConfig.PhaseProcessor);
			PhaseConfig.PhaseProcessor->SetChildProcessors(TmpPipeline.MoveProcessorsArray());

			// 把 PhaseProcessor 的依赖关系/执行顺序文本化，作为 UI 上 "Description" 字段的内容。
			// 用户在 Project Settings 中能直接看到该 phase 的 processor 执行图谱。
			FStringOutputDevice Ar;
			PhaseConfig.PhaseProcessor->DebugOutputDescription(Ar);
			PhaseConfig.Description = FText::FromString(Ar);
		}
	}
#endif // WITH_EDITOR
}

void UMassEntitySettings::BuildProcessorList()
{
	// 步骤 1：清空"全集列表"和每个 phase 的"分桶列表"，准备从零开始扫描。
	ProcessorCDOs.Reset();
	for (FMassProcessingPhaseConfig& PhaseConfig : ProcessingPhasesConfig)
	{
		PhaseConfig.ProcessorCDOs.Reset();
	}

	// 步骤 2：用反射枚举 UMassProcessor 的所有派生类（含 plugin）。
	// 此调用要求所有 UClass 都已 RegisterCompiledInClasses —— 这就是为什么必须等 OnPostEngineInit。
	TArray<UClass*> SubClassess;
	GetDerivedClasses(UMassProcessor::StaticClass(), SubClassess);

	// 倒序遍历是为了在循环内若需 Remove 也安全；当前实现没用 Remove，仅是惯用写法。
	for (int i = SubClassess.Num() - 1; i >= 0; --i)
	{
		// 跳过抽象类：它们没有可用 CDO，也不应作为运行时 processor 出现。
		if (SubClassess[i]->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		UMassProcessor* ProcessorCDO = GetMutableDefault<UMassProcessor>(SubClassess[i]);
		// we explicitly restrict adding UMassCompositeProcessor. If needed by specific project a derived class can be added
		// 中文：明确不收录 UMassCompositeProcessor 自身（它是个"容器"，不该当作普通 processor）。
		//      若项目有特殊需求，应派生一个具名的 CompositeProcessor 子类。
		if (ProcessorCDO && SubClassess[i] != UMassCompositeProcessor::StaticClass()
#if WITH_EDITOR
			// ShouldShowUpInSettings：编辑器开关，允许某些"内部 processor"对用户隐身。
			&& ProcessorCDO->ShouldShowUpInSettings()
#endif // WITH_EDITOR
		)
		{
			// Observers might register later than the GC disregard window, causing a GC mismatch between this early-initialized class and them.
			// 中文：Disregard For GC（DFG）窗口是 UE 启动早期的优化机制 —— 在该窗口内创建的对象
			//      被永久排除于 GC，免去标记成本。但 UMassEntitySettings CDO 在 DFG 内创建，
			//      而某些 Observer/Processor 可能在窗口外才注册。如果 settings CDO 直接持有窗口外
			//      对象的强引用，GC 会因"DFG 对象引用非 DFG 对象"报错。
			//      解决方案：检测到这种情况时，把 ProcessorCDO 加入 Root（永不 GC），从而抹平差异。
			const bool bIsDisregardForGC = GUObjectArray.IsDisregardForGC(this);
			if (bIsDisregardForGC && !(GUObjectArray.IsDisregardForGC(ProcessorCDO) || ProcessorCDO->HasAnyFlags(RF_MarkAsRootSet)))
			{
				ProcessorCDO->AddToRoot();
			}

			// 加入"全集"列表（始终）。
			ProcessorCDOs.Add(ProcessorCDO);

			// 仅当 processor 设置了 bAutoRegisterWithProcessingPhases==true，才默认放进对应 phase。
			// 否则需要项目代码显式 AddToActiveProcessorsList() 才会启用。
			if (ProcessorCDO->ShouldAutoAddToGlobalList())
			{
				ProcessingPhasesConfig[int(ProcessorCDO->GetProcessingPhase())].ProcessorCDOs.Add(ProcessorCDO);
			}
		}
	}

	// 步骤 3：按名字排序，让 UI 列表稳定可读。
	ProcessorCDOs.Sort([](UMassProcessor& LHS, UMassProcessor& RHS) {
		return LHS.GetName().Compare(RHS.GetName()) < 0;
	});
}

void UMassEntitySettings::AddToActiveProcessorsList(TSubclassOf<UMassProcessor> ProcessorClass)
{
	// 防御式校验链：拿到 CDO → 排除非法情况 → 再添加。
	if (UMassProcessor* ProcessorCDO = GetMutableDefault<UMassProcessor>(ProcessorClass))
	{
		if (ProcessorClass == UMassCompositeProcessor::StaticClass())
		{
			// 拒绝 CompositeProcessor 自身（与 BuildProcessorList 中保持一致）。
			UE_VLOG_UELOG(this, LogMass, Log, TEXT("%s adding MassCompositeProcessor to the global processor list is unsupported"), ANSI_TO_TCHAR(__FUNCTION__));
		}
		else if (ProcessorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			// 抽象类没有可用 CDO。
			UE_VLOG_UELOG(this, LogMass, Log, TEXT("%s unable to add %s due to it being an abstract class"), ANSI_TO_TCHAR(__FUNCTION__), *ProcessorClass->GetName());
		}
		else if (ProcessorCDOs.Find(ProcessorCDO) != INDEX_NONE)
		{
			// 已在列表中 —— 直接日志提醒，不重复添加。
			UE_VLOG_UELOG(this, LogMass, Log, TEXT("%s already in global processor list"), *ProcessorCDO->GetName());
		}
		else 
		{
			// 走到这里说明：(1) 该 processor 之前 ShouldAutoAddToGlobalList()==false（所以 BuildProcessorList 没收它），
			// (2) 而项目代码现在又显式要求启用它。所以下面那个 ensure 就是状态一致性检查。
			ensureMsgf(ProcessorCDO->ShouldAutoAddToGlobalList() == false, TEXT("%s missing from the global list while it's already marked to be auto-added"), *ProcessorCDO->GetName());
			ProcessorCDOs.Add(ProcessorCDO);
			// 翻转标志，让后续的 BuildProcessorList / 其他扫描也认它在册。
			ProcessorCDO->SetShouldAutoRegisterWithGlobalList(true);
		}
	}
}

TConstArrayView<FMassProcessingPhaseConfig> UMassEntitySettings::GetProcessingPhasesConfig()
{
	// 惰性初始化：第一次访问时才扫描。这样即使被在 OnPostEngineInit 之前调用，
	// 也只是返回空配置，等真正初始化完后再次调用即可拿到完整数据。
	BuildProcessorListAndPhases();
	return MakeArrayView(ProcessingPhasesConfig, int32(EMassProcessingPhase::MAX));
}

#if WITH_EDITOR
void UMassEntitySettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// GET_MEMBER_NAME_CHECKED 在编译期校验字段名拼写正确，比裸字符串安全。
	static const FName ProcessorCDOsName = GET_MEMBER_NAME_CHECKED(UMassEntitySettings, ProcessorCDOs);
	static const FName ChunkMemorySizeName = GET_MEMBER_NAME_CHECKED(UMassEntitySettings, ChunkMemorySize);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
	{
		// ignore adding elements to arrays since it would be 'None' at first
		// 中文：用户在 UI 上点 "+" 按钮新增数组元素时，新元素的指针默认是 None；
		//      此时无需重建（等用户填好真实值再说）。
		return;
	}

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == ProcessorCDOsName)
		{
			// 用户改了 ProcessorCDOs 列表 → 重新扫描整套全集与 phase 分桶。
			BuildProcessorList();
		}
		else if (PropName == ChunkMemorySizeName)
		{
			// 用户改了 chunk 大小 → 立即校验/对齐到合法值。
			ChunkMemorySize = UE::Mass::SanitizeChunkMemorySize(ChunkMemorySize);
		}

		// 任何属性变更后都重建可视化 phase（即重新生成 PhaseProcessor 与 Description 文本）。
		BuildPhases();
		// 广播给 PhaseManager 等监听者：你们可以重建运行时 pipeline 了。
		OnSettingsChange.Broadcast(PropertyChangedEvent);
	}
}

void UMassEntitySettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// 这里关注的是嵌套属性 —— 例如用户在某个 Processor CDO 内部把 bAutoRegisterWithProcessingPhases 翻转。
	// 这种"内部属性"的变更不会触发上面的 PostEditChangeProperty（因为最外层 property 是 ProcessorCDOs 数组指针），
	// 必须靠 ChainProperty 沿链路递归到末端 property 才能识别。
	static const FName AutoRegisterName = TEXT("bAutoRegisterWithProcessingPhases");

	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;

	// 沿 PropertyChain 走到最末节点 —— 那才是真正被修改的 leaf property。
	FEditPropertyChain::TDoubleLinkedListNode* LastPropertyNode = PropertyChangedEvent.PropertyChain.GetActiveMemberNode();
	while (LastPropertyNode && LastPropertyNode->GetNextNode())
	{
		LastPropertyNode = LastPropertyNode->GetNextNode();
	}

	if (LastPropertyNode)
	{
		MemberProperty = LastPropertyNode->GetValue();
	}

	if (MemberProperty && MemberProperty->GetFName() == AutoRegisterName)
	{
		// 自动注册标志被翻转 → 重新执行扫描，让该 processor 进入 / 退出 phase 列表。
		BuildProcessorList();
	}
}
#endif // WITH_EDITOR
