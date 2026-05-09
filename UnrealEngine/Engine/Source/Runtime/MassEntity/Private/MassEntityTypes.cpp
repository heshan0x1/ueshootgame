// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// MassEntityTypes.cpp
// 此 TU 定义 MassEntityTypes.h 中声明的静态存储：
//   - STAT_Mass_Total 的实例定义（DEFINE_STAT）
//   - 6 大 BitSet 的类型注册表静态数据（DEFINE_TYPEBITSET / 对应 DECLARE_*_EXPORTED）
//   - FMassArchetypeCompositionDescriptor / FMassArchetypeSharedFragmentValues / FMassGenericPayloadView / 
//     FMassArchetypeCreationParams 的实现
// =====================================================================================================================

#include "MassEntityTypes.h"

#include "MassArchetypeData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntityTypes)

// 实例化 STAT_Mass_Total 的存储（DECLARE_CYCLE_STAT_EXTERN 只声明，DEFINE_STAT 才生效）。
DEFINE_STAT(STAT_Mass_Total);

// 下列 DEFINE_TYPEBITSET 展开会生成 BitSet 的静态类型注册表（FTypeBitSet 内部的 struct→bit 映射表）。
// 必须在单一 TU 中定义，避免跨模块重复。
DEFINE_TYPEBITSET(FMassFragmentBitSet);
DEFINE_TYPEBITSET(FMassTagBitSet);
DEFINE_TYPEBITSET(FMassChunkFragmentBitSet);
DEFINE_TYPEBITSET(FMassSharedFragmentBitSet);
DEFINE_TYPEBITSET(FMassConstSharedFragmentBitSet);
DEFINE_TYPEBITSET(FMassExternalSubsystemBitSet);


// EMassObservedOperationFlags → 字符串。常用于日志/调试。
// 注意：switch 只覆盖纯单值与常用组合值；任何其它组合（如 AddElement | RemoveElement）都会落到 default 返回 "UNDEFINED"。
// 这限制了它对"任意位组合"的表达能力——若需要更灵活的调试输出，应自行位拆分打印。
FString LexToString(const EMassObservedOperationFlags Value)
{
	switch (Value)
	{
	case EMassObservedOperationFlags::None:
		return ("None");
	case EMassObservedOperationFlags::AddElement:
		return ("AddElement");
	case EMassObservedOperationFlags::RemoveElement:
		return ("RemoveElement");
	case EMassObservedOperationFlags::CreateEntity:
		return ("CreateEntity");
	case EMassObservedOperationFlags::DestroyEntity:
		return ("DestroyEntity");
	case EMassObservedOperationFlags::Add:
		return ("AddElement | CreateEntity");
	case EMassObservedOperationFlags::Remove:
		return ("RemoveElement | DestroyEntity");
	case EMassObservedOperationFlags::All:
		return ("Add | Remove");
	default:
		return "UNDEFINED";
	}
}

//-----------------------------------------------------------------------------
// FMassArchetypeCompositionDescriptor
//-----------------------------------------------------------------------------
// 静态方法：综合 5 个位集的哈希值得到 Descriptor 的最终哈希。
// 实现要点：嵌套 HashCombine 构造"近似均衡"的哈希合并树，避免简单链式合并带来的分布偏差。
uint32 FMassArchetypeCompositionDescriptor::CalculateHash(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags
	, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragmentBitSet
	, const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet)
{
	const uint32 FragmentsHash = GetTypeHash(InFragments);
	const uint32 TagsHash = GetTypeHash(InTags);
	const uint32 ChunkFragmentsHash = GetTypeHash(InChunkFragments);
	const uint32 SharedFragmentsHash = GetTypeHash(InSharedFragmentBitSet);
	const uint32 ConstSharedFragmentsHash = GetTypeHash(InConstSharedFragmentBitSet);

	return HashCombine(
			HashCombine(FragmentsHash, TagsHash)
			, HashCombine(
				HashCombine(ChunkFragmentsHash, SharedFragmentsHash)
				, ConstSharedFragmentsHash));
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
// 统计 Descriptor 中所有位集已点亮类型总数。纯汇总 API。
int32 FMassArchetypeCompositionDescriptor::CountStoredTypes() const
{
	return Fragments.CountStoredTypes()
		+ Tags.CountStoredTypes()
		+ ChunkFragments.CountStoredTypes()
		+ SharedFragments.CountStoredTypes()
		+ ConstSharedFragments.CountStoredTypes();
}

// 调试打印：仅 WITH_MASSENTITY_DEBUG 有效；shipping 构建下函数体为空。
// 行为：
//   - 若 Fragments/Tags/ChunkFragments 全空则输出 "Empty"（注意：此处未计入 Shared/ConstShared 空判断）。
//   - 否则按 5 个分节依次把各位集的类型列表 dump 出来。
//   - 关闭 AutoEmitLineTerminator 以保持自定义换行格式，最后恢复。
// 注：Empty 分支只检查 3 个位集——两个 shared 若非空也不会被当作"非 Empty"，是一个轻微的文案不一致。
void FMassArchetypeCompositionDescriptor::DebugOutputDescription(FOutputDevice& Ar) const
{
#if WITH_MASSENTITY_DEBUG 
	if (Fragments.IsEmpty()
		&& Tags.IsEmpty()
		&& ChunkFragments.IsEmpty())
	{
		Ar.Logf(TEXT("Empty"));
		return;
	}

	// 暂存原始状态，完成后还原——避免改变外部 OutputDevice 的行为。
	const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
	Ar.SetAutoEmitLineTerminator(false);

	if (!Fragments.IsEmpty())
	{
		Ar.Logf(TEXT("Fragments:\n"));
		Fragments.DebugGetStringDesc(Ar);
	}

	if (!Tags.IsEmpty())
	{
		Ar.Logf(TEXT("Tags:\n"));
		Tags.DebugGetStringDesc(Ar);
	}

	if (!ChunkFragments.IsEmpty())
	{
		Ar.Logf(TEXT("ChunkFragments:\n"));
		ChunkFragments.DebugGetStringDesc(Ar);
	}

	if (!SharedFragments.IsEmpty())
	{
		Ar.Logf(TEXT("SharedFragments:\n"));
		SharedFragments.DebugGetStringDesc(Ar);
	}

	if (!ConstSharedFragments.IsEmpty())
	{
		Ar.Logf(TEXT("ConstSharedFragments:\n"));
		ConstSharedFragments.DebugGetStringDesc(Ar);
	}

	Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
#endif // WITH_MASSENTITY_DEBUG
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

//-----------------------------------------------------------------------------
// FMassArchetypeSharedFragmentValues
//-----------------------------------------------------------------------------
// 添加只读 shared fragment（或返回既存）。
// 流程：
//   1. check(Fragment.IsValid()) —— 必须是有效 SharedStruct。
//   2. 若本类型已在 this 中存在（不论是 const 还是 non-const），触发 ensure 并返回既存实例。
//      -- 若类型原本作为 non-const 存在，GetConstSharedFragmentStruct 返回空——返回空 struct + ensure 日志提示"模式错误"。
//   3. 否则在 ConstSharedFragmentBitSet 上点亮类型位，追加到 ConstSharedFragments 数组，置脏哈希。
FConstSharedStruct FMassArchetypeSharedFragmentValues::Add_GetRef(const FConstSharedStruct& Fragment)
{
	check(Fragment.IsValid());
	const UScriptStruct* StructType = Fragment.GetScriptStruct();
	if (ContainsType(StructType))
	{
		FConstSharedStruct ExistingConstSharedStruct = GetConstSharedFragmentStruct(StructType);
		ensureMsgf(false, TEXT("Shared Fragment of type %s already added to FMassArchetypeSharedFragmentValues%s")
			, *GetNameSafe(StructType)
			, ExistingConstSharedStruct.IsValid() ? TEXT("") : TEXT(" as NON-CONST shared struct"));
		return ExistingConstSharedStruct;
	}

	check(StructType);
	ConstSharedFragmentBitSet.Add(*StructType);
	FConstSharedStruct& StructInstance = ConstSharedFragments.Add_GetRef(Fragment);
	DirtyHashCache();
	return StructInstance;
}

// 对称版本：添加可写 shared fragment，逻辑与上类似。
FSharedStruct FMassArchetypeSharedFragmentValues::Add_GetRef(const FSharedStruct& Fragment)
{
	check(Fragment.IsValid());
	const UScriptStruct* StructType = Fragment.GetScriptStruct();
	if (ContainsType(StructType))
	{
		FSharedStruct ExistingSharedStruct = GetSharedFragmentStruct(StructType);
		ensureMsgf(false, TEXT("Shared Fragment of type %s already added to FMassArchetypeSharedFragmentValues%s")
			, *GetNameSafe(StructType)
			, ExistingSharedStruct.IsValid() ? TEXT("") : TEXT(" as CONST shared struct"));
		return ExistingSharedStruct;
	}

	check(StructType);
	SharedFragmentBitSet.Add(*StructType);
	FSharedStruct& StructInstance = SharedFragments.Add_GetRef(Fragment);
	DirtyHashCache();
	return StructInstance;
}

// 替换一批可写 shared fragment 的值。
// 要求：每个传入类型必须已存在于 SharedFragments 中，否则触发 ensure（但函数继续执行，跳过该类型）。
// 不修改位集——因为"替换"不改变 archetype 组合。
void FMassArchetypeSharedFragmentValues::ReplaceSharedFragments(TConstArrayView<FSharedStruct> Fragments)
{
	DirtyHashCache();
	for (const FSharedStruct& NewFragment : Fragments)
	{
		const UScriptStruct* NewFragScriptStruct = NewFragment.GetScriptStruct();
		check(NewFragScriptStruct);

		bool bEntryFound = false;
		for (FSharedStruct& MyFragment : SharedFragments)
		{
			if (MyFragment.GetScriptStruct() == NewFragScriptStruct)
			{
				MyFragment = NewFragment;
				bEntryFound = true;
				break;
			}
		}
		ensureMsgf(bEntryFound, TEXT("Existing fragment of type %s could not be found"), *GetNameSafe(NewFragScriptStruct));
	}
}

// 实际哈希计算。
// 前置条件：数组必须已排序（bSorted==true）；否则不同插入顺序将得到不同哈希值，破坏等价性。
// 策略：对每个 SharedStruct 取 GetMemory()（即实际数据块指针），逐个 PointerHash 累加。
// 注意：并不哈希结构体内容本身——两个不同 FSharedStruct 指向同一块内存则哈希相同，这也是
// Mass 设计的"shared fragment 共享内存"语义：两个引用同一 shared 值的容器视为等价。
uint32 FMassArchetypeSharedFragmentValues::CalculateHash() const
{
	if (!testableEnsureMsgf(bSorted, TEXT("Expecting the containers to be sorted for the hash caluclation to be consistent")))
	{
		return 0;
	}

	// Fragments are not part of the uniqueness 
	// (fragments 指的是 Composition 中的 non-shared fragments，不归此处哈希管辖)
	uint32 Hash = 0;
	for (const FConstSharedStruct& Fragment : ConstSharedFragments)
	{
		Hash = PointerHash(Fragment.GetMemory(), Hash);
	}

	for (const FSharedStruct& Fragment : SharedFragments)
	{
		Hash = PointerHash(Fragment.GetMemory(), Hash);
	}

	return Hash;
}

namespace UE::Mass::Private
{
	// 统计数组中"无效"SharedStruct 的个数（GetScriptStruct()==nullptr）。
	// 出现无效元素的常见原因：Remove 之后 RemoveAllSwap 之前的瞬时"空槽"状态，或外部 Reset。
	template<typename TSharedStruct>
	int32 CountInvalid(const TArray<TSharedStruct>& View)
	{
		int32 Count = 0;
		for (const TSharedStruct& SharedStruct : View)
		{
			const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
			Count += StructType ? 0 : 1;
		}
		return Count;
	}

	/** Note that this function assumes that both ViewA and ViewB do not contain duplicates */
	/** 比较两个 SharedStruct 数组内容是否"等价"（顺序无关）。
	 *  bSkipNulls 控制是否忽略"无效槽位"；调用方通常传 true 以容忍脏残留。
	 *  算法：
	 *    1. 数量预检：若忽略 null 则比较 ValidCount，否则比较 Num
	 *    2. 遍历 A 的每个有效元素，在 B 中按 UScriptStruct* 找对应条目，存在则 CompareStructValues
	 *    3. 任一环节失败则返回 false */
	template<typename TSharedStruct, bool bSkipNulls=true>
	bool ArraysHaveSameContents(const TArray<TSharedStruct>& ViewA, const TArray<TSharedStruct>& ViewB)
	{
		if constexpr (bSkipNulls)
		{
			const int32 NullstCountA = CountInvalid(ViewA);
			const int32 NullstCountB = CountInvalid(ViewB);
			if (ViewA.Num() - NullstCountA != ViewB.Num() - NullstCountB)
			{
				return false;
			}
		}
		else if (ViewA.Num() != ViewB.Num())
		{
			return false;
		}

		for (const TSharedStruct& SharedStruct : ViewA)
		{
			const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
			if constexpr (bSkipNulls)
			{
				if (StructType == nullptr)
				{
					continue;
				}
			}
			const int32 FragmentIndex = ViewB.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
			if (FragmentIndex == INDEX_NONE)
			{
				return false;
			}
			// CompareStructValues 调用 UScriptStruct::CompareScriptStruct，逐属性比较内容。
			if (ViewB[FragmentIndex].CompareStructValues(SharedStruct) == false)
			{
				return false;
			}
		}

		return true;
	}
}

// 先验：两位集等价（同类型集合），再验：两数组内容等价（顺序无关 + 忽略空槽）。
// 即便 this/Other 的数组顺序不同、中间有 null，只要"有效值集合"一致就返回 true。
bool FMassArchetypeSharedFragmentValues::HasSameValues(const FMassArchetypeSharedFragmentValues& Other) const
{
	if (SharedFragmentBitSet.IsEquivalent(Other.SharedFragmentBitSet) == false
		|| ConstSharedFragmentBitSet.IsEquivalent(Other.ConstSharedFragmentBitSet) == false)
	{
		return false;
	}

	return UE::Mass::Private::ArraysHaveSameContents(SharedFragments, Other.GetSharedFragments())
		&& UE::Mass::Private::ArraysHaveSameContents(ConstSharedFragments, Other.GetConstSharedFragments());
}

// 合并两个 SharedFragmentValues。
// 共同类型：覆盖为 Other 的值；独有类型：追加。
// 对应位集用 += 并集合并。最后 DirtyHashCache。
// 注意：本函数不校验"role 一致"（即 Other 的 SharedFragment 不会被当成 const 加到 this）——因为
// 可写/只读两数组独立合并；若要检测跨 role 冲突需在 ContainsType 层做（外部职责）。
int32 FMassArchetypeSharedFragmentValues::Append(const FMassArchetypeSharedFragmentValues& Other)
{
	int32 AddedOrModifiedCount = 0;

	for (const FSharedStruct& SharedStruct : Other.GetSharedFragments())
	{
		const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
		check(StructType);
		if (SharedFragmentBitSet.Contains(*StructType))
		{
			// 已存在：位集一致性校验，然后覆盖值。
			const int32 FragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
			checkf(FragmentIndex != INDEX_NONE, TEXT("Mismatch between shared fragment bitset and stored values"));
			SharedFragments[FragmentIndex] = SharedStruct;
			++AddedOrModifiedCount;
		}
		else
		{
			// 新增类型：追加到数组，稍后统一更新位集。
			SharedFragments.Add(SharedStruct);
			++AddedOrModifiedCount;
		}
	}

	for (const FConstSharedStruct& SharedStruct : Other.GetConstSharedFragments())
	{
		const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
		check(StructType);
		if (ConstSharedFragmentBitSet.Contains(*StructType))
		{
			const int32 FragmentIndex = ConstSharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
			checkf(FragmentIndex != INDEX_NONE, TEXT("Mismatch between const shared fragment bitset and stored values"));
			ConstSharedFragments[FragmentIndex] = SharedStruct;
			++AddedOrModifiedCount;
		}
		else
		{
			ConstSharedFragments.Add(SharedStruct);
			++AddedOrModifiedCount;
		}
	}

	// 位集的 += 即并集（MassFragmentBitSet 等的重载）。两个位集合并后哈希已失效。
	SharedFragmentBitSet += Other.SharedFragmentBitSet;
	ConstSharedFragmentBitSet += Other.ConstSharedFragmentBitSet;
	DirtyHashCache();

	return AddedOrModifiedCount;
}

// 按可写 shared 位集移除。
// 算法：
//   1. 与本地位集取交集 CommonFragments —— 只处理确实存在的类型
//   2. 遍历 CommonFragments 的类型：调用 SharedFragments[i].Reset() 清空槽位（但不立刻删除数组元素）
//   3. 若有移除，集中 RemoveAllSwap 掉所有 !IsValid() 元素，并从位集中减去 CommonFragments
// 两阶段设计：避免遍历 + 删除交错造成的索引失效。
int32 FMassArchetypeSharedFragmentValues::Remove(const FMassSharedFragmentBitSet& SharedFragmentToRemoveBitSet)
{
	int32 RemovedCount = 0;
	FMassSharedFragmentBitSet CommonFragments = (SharedFragmentBitSet & SharedFragmentToRemoveBitSet);
	FMassSharedFragmentBitSet::FIndexIterator It = CommonFragments.GetIndexIterator();
	while(It)
	{
		const UScriptStruct* StructType = CommonFragments.GetTypeAtIndex(*It);
		check(StructType);

		const int32 RegularFragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		if (RegularFragmentIndex != INDEX_NONE)
		{
			// 先置空，后续批量 swap-remove 清掉。
			SharedFragments[RegularFragmentIndex].Reset();
			++RemovedCount;
		}

		++It;
	}

	if (RemovedCount)
	{
		SharedFragments.RemoveAllSwap([](const FSharedStruct& SharedStruct) { return !SharedStruct.IsValid(); });
		SharedFragmentBitSet -= CommonFragments;
		DirtyHashCache();
	}
	return RemovedCount;
}

// 只读版：逻辑与上对称，作用于 ConstSharedFragments/ConstSharedFragmentBitSet。
int32 FMassArchetypeSharedFragmentValues::Remove(const FMassConstSharedFragmentBitSet& ConstSharedFragmentToRemoveBitSet)
{
	int32 RemovedCount = 0;
	FMassConstSharedFragmentBitSet CommonFragments = (ConstSharedFragmentBitSet & ConstSharedFragmentToRemoveBitSet);
	FMassConstSharedFragmentBitSet::FIndexIterator It = CommonFragments.GetIndexIterator();
	while(It)
	{
		const UScriptStruct* StructType = CommonFragments.GetTypeAtIndex(*It);
		check(StructType);

		const int32 RegularFragmentIndex = ConstSharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		if (RegularFragmentIndex != INDEX_NONE)
		{
			ConstSharedFragments[RegularFragmentIndex].Reset();
			++RemovedCount;
		}

		++It;
	}

	if (RemovedCount)
	{
		ConstSharedFragments.RemoveAllSwap([](const FConstSharedStruct& SharedStruct) { return !SharedStruct.IsValid(); });
		ConstSharedFragmentBitSet -= CommonFragments;
		DirtyHashCache();
	}
	return RemovedCount;
}

//-----------------------------------------------------------------------------
// FMassGenericPayloadView
//-----------------------------------------------------------------------------
// 把每列中 [StartIndex, StartIndex+NumToMove) 的元素原地移到列末尾，保持列之间对齐。
// 算法（对每列独立执行）：
//   1. 若 StartIndex+NumToMove 已经在末尾（或之后）→ 无需动；
//   2. 否则把该段 NumToMove 个元素拷到栈上 MovedElements；
//   3. Memmove 把原位置之后的"尾段"整体往前搬，覆盖原被移段；
//   4. Memcpy 把 MovedElements 写到数组末端。
// 这种方式使用原始字节拷贝，不调用 FSharedStruct 等对象的 copy/assign ——要求元素类型具备 trivially-copyable 语义。
// 在 Mass 中，这里的"元素"是按 UScriptStruct 描述的 POD-like 数据块，符合此假设。
void FMassGenericPayloadView::SwapElementsToEnd(const int32 StartIndex, int32 NumToMove)
{
	check(StartIndex >= 0 && NumToMove >= 0);

	// UNLIKELY 给编译器做分支预测 hint —— 常规路径不会触发该退出。
	if (UNLIKELY(NumToMove <= 0 || StartIndex < 0))
	{
		return;
	}

	// 栈上 Inline 16 字节分配的临时缓冲；超过时退化为堆分配。
	TArray<uint8, TInlineAllocator<16>> MovedElements;

	for (FStructArrayView& StructArrayView : Content)
	{
		check((StartIndex + NumToMove) <= StructArrayView.Num());
		if (StartIndex + NumToMove >= StructArrayView.Num() - 1)
		{
			// nothing to do here, the elements are already at the back
			// 已经在末尾 —— 无需搬移。注意判断包含 "Num-1"，允许恰好贴边的情形。
			continue;
		}

		uint8* ViewData = static_cast<uint8*>(StructArrayView.GetData());
		const uint32 ElementSize = StructArrayView.GetTypeSize();
		const uint32 MovedStartOffset = StartIndex * ElementSize;                                 // 被移段起始字节
		const uint32 MovedSize = NumToMove * ElementSize;                                          // 被移段字节数
		const uint32 MoveOffset = (StructArrayView.Num() - (StartIndex + NumToMove)) * ElementSize; // 尾段字节数

		MovedElements.Reset();
		// 1) 暂存被移段
		MovedElements.Append(ViewData + MovedStartOffset, MovedSize);
		// 2) 尾段前移（覆盖旧位置），使用 Memmove 处理可能重叠
		FMemory::Memmove(ViewData + MovedStartOffset, ViewData + MovedStartOffset + MovedSize, MoveOffset);
		// 3) 被移段写到末尾
		FMemory::Memcpy(ViewData + MovedStartOffset + MoveOffset, MovedElements.GetData(), MovedSize);
	}
}

//-----------------------------------------------------------------------------
// FMassArchetypeCreationParams
//-----------------------------------------------------------------------------
// 从既存 Archetype 拷贝 chunk 分配尺寸；DebugName/DebugColor 不拷贝（由调用方显式设定）。
FMassArchetypeCreationParams::FMassArchetypeCreationParams(const FMassArchetypeData& Archetype)
	: ChunkMemorySize(static_cast<int32>(Archetype.GetChunkAllocSize()))
{
}