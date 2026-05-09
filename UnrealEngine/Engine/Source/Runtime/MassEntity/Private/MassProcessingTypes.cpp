// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// MassProcessingTypes.cpp
// 实现 MassProcessingTypes.h 中声明的类型，以及 LogMass 日志类别的定义。
// 关键点：
//   - LogMass 必须在本模块内 DEFINE_LOG_CATEGORY（对应 MassEntityTypes.h 中的 DECLARE_LOG_CATEGORY_EXTERN）。
//   - FMassRuntimePipeline 的大多数 Append* / InitializeFrom* 函数都会通过
//     UE::Mass::Utils::DetermineProcessorExecutionFlags 过滤 Processor 是否应在当前 World 下执行。
//   - "运行期副本" 的创建模式：NewObject(Outer=InOwner, Class=Proc->GetClass(), Template=Proc)，
//     以 Proc（通常是 CDO 或配置 Asset 上的实例）作为模板拷贝所有属性。
// =====================================================================================================================

#include "MassProcessingTypes.h"
#include "MassProcessor.h"
#include "MassEntityUtils.h"
#include "VisualLogger/VisualLogger.h"
#include "MassDebugger.h"
#include "Misc/CoreDelegates.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassProcessingTypes)

// 定义 LogMass 日志类别的静态存储（对应 MassEntityTypes.h 中的 DECLARE_LOG_CATEGORY_EXTERN）。
// 放在本 TU 而非 MassEntityTypes.cpp，因为 MassProcessingTypes 被更早包含，保证 LogMass 的唯一定义位置。
DEFINE_LOG_CATEGORY(LogMass);

//----------------------------------------------------------------------//
//  FMassRuntimePipeline
//----------------------------------------------------------------------//
// 构造：用一组已有 TObjectPtr<UMassProcessor> 直接填充 Processors 数组（浅拷贝指针）。
FMassRuntimePipeline::FMassRuntimePipeline(TConstArrayView<TObjectPtr<UMassProcessor>> SeedProcessors, const EProcessorExecutionFlags WorldExecutionFlags)
	: Processors(SeedProcessors)
	, ExecutionFlags(WorldExecutionFlags)
{
	
}

// 构造：裸指针数组版本，通过 ObjectPtrWrap 包装为 TObjectPtr 以满足 GC 追踪要求。
FMassRuntimePipeline::FMassRuntimePipeline(TConstArrayView<UMassProcessor*> SeedProcessors, const EProcessorExecutionFlags WorldExecutionFlags)
	: ExecutionFlags(WorldExecutionFlags)
{
	Processors = ObjectPtrWrap(SeedProcessors);
}

// 清空当前 Processor 列表；ExecutionFlags 保留不变。
void FMassRuntimePipeline::Reset()
{
	Processors.Reset();
}

// 对尚未初始化的 Processor 逐一调用 CallInitialize，并在最后清理掉数组里偶然混入的 nullptr。
// 使用 bNullsFound 标记以避免空 Processors 情况下无谓的 RemoveAll 扫描。
void FMassRuntimePipeline::Initialize(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	// having nulls in Processors should be rare so we run the "remove all nulls" operation below only if we know 
	// for sure that there are any nulls to be removed
	// Processors 中出现 nullptr 属罕见情况；仅当确认存在空指针时才执行 RemoveAll，避免无谓开销。
	bool bNullsFound = false;

	for (UMassProcessor* Proc : Processors)
	{
		if (Proc)
		{
			if (Proc->IsInitialized() == false)
			{
				// Visual Logger 重定向：让 Processor 的 VLog 输出挂到 Owner 的时间线上。
				REDIRECT_OBJECT_TO_VLOG(Proc, &Owner);
				Proc->CallInitialize(&Owner, EntityManager);
			}
		}
		else
		{
			bNullsFound = true;
		}
	}

	if (bNullsFound)
	{
		Processors.RemoveAll([](const UMassProcessor* Proc) { return Proc == nullptr; });
	}
}

// SetProcessors 重载：Reset 后直接拷贝或 move 新数组，作用是"整体替换"现有列表。
void FMassRuntimePipeline::SetProcessors(TArrayView<UMassProcessor*> InProcessors)
{
	Reset();
	Processors = InProcessors;
}

void FMassRuntimePipeline::SetProcessors(TArray<TObjectPtr<UMassProcessor>>&& InProcessors)
{
	Reset();
	Processors = MoveTemp(InProcessors);
}

// 从现有 Processor 数组创建运行期副本。清空列表后委托给 AppendOrOverrideRuntimeProcessorCopies。
void FMassRuntimePipeline::CreateFromArray(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner)
{
	Reset();
	AppendOrOverrideRuntimeProcessorCopies(InProcessors, InOwner);
}

// 复合操作：CreateFromArray + Initialize，一步到位地构造并初始化所有 Processor。
void FMassRuntimePipeline::InitializeFromArray(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	CreateFromArray(InProcessors, InOwner);
	Initialize(InOwner, EntityManager);
}

// 由 Processor 类列表构造实例：
//   1. 取 Class 的 CDO 以调用 ShouldExecute 做 NetMode 过滤（避免 Server-only Processor 跑在 Client）
//   2. 通过 NewObject 创建运行期实例并加入 Processors
//   3. 最后统一 Initialize
void FMassRuntimePipeline::InitializeFromClassArray(TConstArrayView<TSubclassOf<UMassProcessor>> InProcessorClasses, UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	Reset();

	const UWorld* World = InOwner.GetWorld();
	// 将"管线声明的 ExecutionFlags"与"当前 World 的 NetMode"交叉得到最终过滤掩码。
	const EProcessorExecutionFlags WorldExecutionFlags = UE::Mass::Utils::DetermineProcessorExecutionFlags(World, ExecutionFlags);

	for (const TSubclassOf<UMassProcessor>& ProcessorClass : InProcessorClasses)
	{
		if (ProcessorClass)
		{
			UMassProcessor* CDO = ProcessorClass.GetDefaultObject();
			if (CDO && CDO->ShouldExecute(WorldExecutionFlags))
			{
				UMassProcessor* ProcInstance = NewObject<UMassProcessor>(&InOwner, ProcessorClass);
				Processors.Add(ProcInstance);
			}
			else
			{
				// VLog：记录被 ExecutionFlags 过滤掉的 Processor，方便调试"我的 Processor 怎么不跑"。
				UE_CVLOG(CDO, &InOwner, LogMass, Log, TEXT("Skipping %s due to ExecutionFlags"), *CDO->GetName());
			}
		}
	}

	Initialize(InOwner, EntityManager);
}

// 严格同 Class 匹配（非 IsA）：避免派生类意外被视为"已存在"而跳过添加。
bool FMassRuntimePipeline::HasProcessorOfExactClass(TSubclassOf<UMassProcessor> InClass) const
{
	UClass* TestClass = InClass.Get();
	return Processors.FindByPredicate([TestClass](const UMassProcessor* Proc){ return Proc != nullptr && Proc->GetClass() == TestClass; }) != nullptr;
}

// 去重追加：遍历 InProcessors，若通过 ExecutionFlags 校验且本管线无同 Class Processor，则创建副本。
// 所有新追加的 Processor 在末尾通过下半段的 for 循环完成 Initialize。
void FMassRuntimePipeline::AppendUniqueRuntimeProcessorCopies(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	const UWorld* World = InOwner.GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = UE::Mass::Utils::DetermineProcessorExecutionFlags(World, ExecutionFlags);
	// 记录"起始位置"，后面只对新追加的 Processor 做 Initialize。
	const int32 StartingCount = Processors.Num();
		
	for (const UMassProcessor* Proc : InProcessors)
	{
		if (Proc && Proc->ShouldExecute(WorldExecutionFlags)
			&& HasProcessorOfExactClass(Proc->GetClass()) == false)
		{
			// unfortunately the const cast is required since NewObject doesn't support const Template object
			// NewObject 的 Template 参数类型为 UObject*，无 const 重载，故此处必须 const_cast。
			// 这里使用 Proc 作为模板，克隆其 UPROPERTY 字段到新实例。
			UMassProcessor* ProcCopy = NewObject<UMassProcessor>(&InOwner, Proc->GetClass(), FName(), RF_NoFlags, const_cast<UMassProcessor*>(Proc));
			Processors.Add(ProcCopy);
		}
#if WITH_MASSENTITY_DEBUG
		// 只在 DEBUG 构建下给出"为什么跳过"的详细 VLog，非 shipping 开销。
		else if (Proc)
		{
			if (Proc->ShouldExecute(WorldExecutionFlags) == false)
			{
				UE_VLOG(&InOwner, LogMass, Log, TEXT("Skipping %s due to ExecutionFlags"), *Proc->GetName());
			}
			else if (Proc->ShouldAllowMultipleInstances() == false)
			{
				// 注：5.6 起不再为 ShouldAllowMultipleInstances()==true 的 Processor 创建重复副本，
				// 但原代码的日志文案仍是"due to it being a duplicate"；逻辑分支与文案匹配（被当作重复跳过）。
				UE_VLOG(&InOwner, LogMass, Log, TEXT("Skipping %s due to it being a duplicate"), *Proc->GetName());
			}
		}
#endif // WITH_MASSENTITY_DEBUG
	}

	// 对刚刚追加的尾部片段执行 Initialize；前半段（原有 Processor）不再重复 Initialize。
	for (int32 NewProcIndex = StartingCount; NewProcIndex < Processors.Num(); ++NewProcIndex)
	{
		UMassProcessor* Proc = Processors[NewProcIndex];
		check(Proc);
		
		if (Proc->IsInitialized() == false)
		{
			REDIRECT_OBJECT_TO_VLOG(Proc, &InOwner);
 			Proc->CallInitialize(&InOwner, EntityManager);
		}
	}
}

// 追加 or 覆盖：
//   - ShouldAllowMultipleInstances()==true → 允许同类多实例，直接 Add
//   - 否则查找是否已有同类 Processor；有则覆盖（*PrevProcessor = ProcCopy），无则追加
// 本函数不执行 Initialize，仅创建对象。上游会在需要时再统一 Initialize。
void FMassRuntimePipeline::AppendOrOverrideRuntimeProcessorCopies(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner)
{
	const UWorld* World = InOwner.GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = UE::Mass::Utils::DetermineProcessorExecutionFlags(World, ExecutionFlags);

	for (const UMassProcessor* Proc : InProcessors)
	{
		if (Proc && Proc->ShouldExecute(WorldExecutionFlags))
		{
			// unfortunately the const cast is required since NewObject doesn't support const Template object
			UMassProcessor* ProcCopy = NewObject<UMassProcessor>(&InOwner, Proc->GetClass(), FName(), RF_NoFlags, const_cast<UMassProcessor*>(Proc));
			check(ProcCopy);

			if (ProcCopy->ShouldAllowMultipleInstances())
			{
				// we don't care if there are instances of this class in Processors already
				// 允许多实例：直接追加，不查重。
				Processors.Add(ProcCopy);
			}
			else 
			{
				const UClass* TestClass = Proc->GetClass();
				// 查找是否已有同 Class 的 Processor。注意：FindByPredicate 返回的是元素指针（即 TObjectPtr<UMassProcessor>*）。
				TObjectPtr<UMassProcessor>* PrevProcessor = Processors.FindByPredicate([TestClass, ProcCopy](const UMassProcessor* Proc) {
					return Proc != nullptr && Proc->GetClass() == TestClass;
				});

				if (PrevProcessor)
				{
					// 覆盖：原 Processor 的 TObjectPtr 将被 GC 回收（假如没有其它引用）。
					*PrevProcessor = ProcCopy;
				}
				else
				{
					Processors.Add(ProcCopy);
				}
			}
		}
		else
		{
			UE_CVLOG(Proc, &InOwner, LogMass, Log, TEXT("Skipping %s due to ExecutionFlags"), *Proc->GetName());
		}
	}
}

// 直接追加一个实例，不做任何检查。调用者自行负责类型与去重。
void FMassRuntimePipeline::AppendProcessor(UMassProcessor& InProcessor)
{
	Processors.Add(&InProcessor);
}

// 批量追加 TObjectPtr 视图。
void FMassRuntimePipeline::AppendProcessors(TArrayView<TObjectPtr<UMassProcessor>> InProcessors)
{
	Processors.Append(InProcessors);
}

// 批量追加（move 语义）。若当前为空则直接夺取存储，避免 Append 带来的元素级拷贝。
void FMassRuntimePipeline::AppendProcessors(TArray<TObjectPtr<UMassProcessor>>&& InProcessors)
{
	if (Processors.Num())
	{
		Processors.Append(MoveTemp(InProcessors));
	}
	else
	{
		Processors = MoveTemp(InProcessors);
	}
}

// 按指针唯一性追加（TArray::AddUnique 基于 operator==）。返回 true 表示确实发生了新增。
bool FMassRuntimePipeline::AppendUniqueProcessor(UMassProcessor& Processor)
{
	const int32 PreviousCount = Processors.Num();
	Processors.AddUnique(&Processor);
	return PreviousCount != Processors.Num();
}

// 由 Class 新建实例并直接追加。调用者需确保 ProcessorClass 非空（有 check）。
void FMassRuntimePipeline::AppendProcessor(TSubclassOf<UMassProcessor> ProcessorClass, UObject& InOwner)
{
	check(ProcessorClass);
	UMassProcessor* ProcInstance = NewObject<UMassProcessor>(&InOwner, ProcessorClass);
	AppendProcessor(*ProcInstance);
}

// 删除"同一对象指针"的 Processor（非同 Class）。通常配合 AppendProcessor 使用。
bool FMassRuntimePipeline::RemoveProcessor(const UMassProcessor& InProcessor)
{
	return Processors.RemoveAll([Processor = &InProcessor](const TObjectPtr<UMassProcessor>& Element)
		{
			return Element == Processor;	
		}) > 0;
}

// 仅在顶层 Processor 中查找 GroupName 匹配的 UMassCompositeProcessor（不递归进入其子管线）。
UMassCompositeProcessor* FMassRuntimePipeline::FindTopLevelGroupByName(FName GroupName)
{
	for (UMassProcessor* Processor : Processors)
	{
		UMassCompositeProcessor* CompositeProcessor = Cast<UMassCompositeProcessor>(Processor);
		if (CompositeProcessor && CompositeProcessor->GetGroupName() == GroupName)
		{
			return CompositeProcessor;
		}
	}
	return nullptr;
}

// 按 ExecutionPriority 降序排序；排序前 swap-remove 所有 nullptr（O(n) 而非 O(n²)）。
// 注意：此排序不解析依赖关系，仅根据单一数值字段；正式调度仍由 DependencySolver 做。
void FMassRuntimePipeline::SortByExecutionPriority()
{
	if (Processors.IsEmpty())
	{
		return;
	}

	Processors.RemoveAllSwap([](const UMassProcessor* Processor)
	{ 
		return Processor == nullptr; 
	});
	Processors.Sort([](const UMassProcessor& ProcessorA, const UMassProcessor& ProcessorB)
	{
		return ProcessorA.GetExecutionPriority() > ProcessorB.GetExecutionPriority();
	});
}

// 哈希：合并每个 Processor 指针的哈希值。要求两个 Pipeline 仅当 Processor 集合与顺序完全一致才相等。
uint32 GetTypeHash(const FMassRuntimePipeline& Instance)
{ 
	uint32 Hash = 0;
	for (const UMassProcessor* Proc : Instance.Processors)
	{
		Hash = HashCombine(Hash, PointerHash(Proc));
	}
	return Hash;
}

//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------
// 下述过时重载通过 UE::Mass::Utils::GetEntityManager(World/Owner) 兜底查找 EntityManager，
// 再转调用带 TSharedRef 参数的新版函数。新代码应直接传递 EntityManager，避免隐式 Subsystem 查找开销。
void FMassRuntimePipeline::Initialize(UObject& Owner)
{
	FMassEntityManager* EntityManager = UE::Mass::Utils::GetEntityManager(Owner.GetWorld());
	if (ensureMsgf(EntityManager, TEXT("Unable to determine the current MassEntityManager")))
	{
		Initialize(Owner, EntityManager->AsShared());
	}
}

void FMassRuntimePipeline::InitializeFromArray(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner)
{
	FMassEntityManager* EntityManager = UE::Mass::Utils::GetEntityManager(&InOwner);
	if (ensureMsgf(EntityManager, TEXT("Unable to determine the current MassEntityManager")))
	{
		InitializeFromArray(InProcessors, InOwner, EntityManager->AsShared());
	}
}

void FMassRuntimePipeline::InitializeFromClassArray(TConstArrayView<TSubclassOf<UMassProcessor>> InProcessorClasses, UObject& InOwner)
{
	FMassEntityManager* EntityManager = UE::Mass::Utils::GetEntityManager(&InOwner);
	if (ensureMsgf(EntityManager, TEXT("Unable to determine the current MassEntityManager")))
	{
		InitializeFromClassArray(InProcessorClasses,  InOwner, EntityManager->AsShared());
	}	
}

void FMassRuntimePipeline::AppendUniqueRuntimeProcessorCopies(TConstArrayView<const UMassProcessor*> InProcessors, UObject& InOwner)
{
	FMassEntityManager* EntityManager = UE::Mass::Utils::GetEntityManager(&InOwner);
	if (ensureMsgf(EntityManager, TEXT("Unable to determine the current MassEntityManager")))
	{
		AppendUniqueRuntimeProcessorCopies(InProcessors, InOwner, EntityManager->AsShared());
	}	
}

void FMassRuntimePipeline::SetProcessors(TArray<UMassProcessor*>&& InProcessors)
{
	SetProcessors(MakeArrayView(InProcessors.GetData(), InProcessors.Num()));
}
