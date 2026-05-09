// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityView.h"
#include "MassEntityManager.h"
#include "MassArchetypeData.h"
#include "MassTestableEnsures.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntityView)


//-----------------------------------------------------------------------------
// FMassEntityView
//-----------------------------------------------------------------------------
// 中文：构造时三步：
//   1) 保存 entity handle；
//   2) 把 ArchetypeHandle / 由 EntityManager 解析得到的 ArchetypeHandle 转成内部 FMassArchetypeData* 指针；
//   3) 让 archetype 反查 entity 在哪个 chunk 的第几个 slot，得到 FMassEntityInChunkDataHandle（含序列号）。
// 之后所有 fragment 访问都基于 (Archetype*, EntityDataHandle) 这一对完成。
FMassEntityView::FMassEntityView(const FMassArchetypeHandle& ArchetypeHandle, FMassEntityHandle InEntity)
{
	Entity = InEntity;
	// 中文：ArchetypeDataFromHandleChecked 会 check ArchetypeHandle 有效性。
	Archetype = &FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	// 中文：MakeEntityHandle 会 check entity 确实属于这个 archetype；不属于则 check fail。
	EntityDataHandle = Archetype->MakeEntityHandle(Entity);
}

// 中文：通用构造——先让 EntityManager 反查 archetype（Entity -> Archetype），再交给上面的逻辑。
// 若 entity 已被销毁，GetArchetypeForEntity 内部会触发 check（见 TryMakeView 的对应实现）。
FMassEntityView::FMassEntityView(const FMassEntityManager& EntityManager, FMassEntityHandle InEntity)
{
	Entity = InEntity;
	const FMassArchetypeHandle ArchetypeHandle = EntityManager.GetArchetypeForEntity(Entity);
	Archetype = &FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	EntityDataHandle = Archetype->MakeEntityHandle(Entity);
}

// 中文：安全工厂——查 archetype 但不要求一定存在；entity 已销毁时返回默认（IsSet()==false）的 view。
FMassEntityView FMassEntityView::TryMakeView(const FMassEntityManager& EntityManager, FMassEntityHandle InEntity)
{
	const FMassArchetypeHandle ArchetypeHandle = EntityManager.GetArchetypeForEntity(InEntity);
	return ArchetypeHandle.IsValid() ? FMassEntityView(ArchetypeHandle, InEntity) : FMassEntityView();
}

// 中文：fragment 取址（可选版）。
//   - 若 view 没有设置 archetype（默认构造的 view），ensure 提示并返回 nullptr。
//   - GetFragmentIndex 在 archetype 没有此类型时返回 nullptr，这里也返回 nullptr。
//   - GetFragmentData 内部按 (chunk, slot) 在该 fragment 的列里取地址。
void* FMassEntityView::GetFragmentPtr(const UScriptStruct& FragmentType) const
{
	if (testableEnsureMsgf(Archetype, TEXT("%hs: Trying to access fragments while no archetype set"), __FUNCTION__))
	{
		CA_ASSUME(Archetype);
		if (const int32* FragmentIndex = Archetype->GetFragmentIndex(&FragmentType))
		{
			// failing the below Find means given entity's archetype is missing given FragmentType
			// 中文：FragmentIndex 存在意味着 archetype 含有该 fragment；GetFragmentData 必然能取到合法地址。
			return Archetype->GetFragmentData(*FragmentIndex, EntityDataHandle);
		}
	}
	return nullptr;
}

// 中文：fragment 取址（必有版）。GetFragmentIndexChecked 在缺失时直接 check fail，调用方必须确保该 fragment 存在。
void* FMassEntityView::GetFragmentPtrChecked(const UScriptStruct& FragmentType) const
{
	if (testableEnsureMsgf(Archetype, TEXT("%hs: Trying to access fragments while no archetype set"), __FUNCTION__))
	{
		CA_ASSUME(Archetype);
		const int32 FragmentIndex = Archetype->GetFragmentIndexChecked(&FragmentType);
		return Archetype->GetFragmentData(FragmentIndex, EntityDataHandle);
	}
	return nullptr;
}

// 中文：const shared fragment 取址（可选版）——shared 不在 chunk 列里，而在 archetype 上挂的 SharedFragmentValues。
//   注意：每个 entity 的 SharedFragmentValues 可能不同（同一 archetype 内可被进一步分桶），所以是按 entity 查。
const void* FMassEntityView::GetConstSharedFragmentPtr(const UScriptStruct& FragmentType) const
{
	const FConstSharedStruct* SharedFragment = Archetype->GetSharedFragmentValues(Entity).GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(&FragmentType));
	return (SharedFragment != nullptr) ? SharedFragment->GetMemory() : nullptr;
}

// 中文：const shared fragment 取址（必有版），缺失时 check fail。
const void* FMassEntityView::GetConstSharedFragmentPtrChecked(const UScriptStruct& FragmentType) const
{
	const FConstSharedStruct* SharedFragment = Archetype->GetSharedFragmentValues(Entity).GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(&FragmentType));
	check(SharedFragment != nullptr);
	return SharedFragment->GetMemory();
}

// 中文：(mutable) shared fragment 取址（可选版），逻辑同 const 版本，只是查找另一个数组。
void* FMassEntityView::GetSharedFragmentPtr(const UScriptStruct& FragmentType) const
{
	const FSharedStruct* SharedFragment = Archetype->GetSharedFragmentValues(Entity).GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(&FragmentType));
	return (SharedFragment != nullptr) ? SharedFragment->GetMemory() : nullptr;
}

// 中文：(mutable) shared fragment 取址（必有版）。
void* FMassEntityView::GetSharedFragmentPtrChecked(const UScriptStruct& FragmentType) const
{
	const FSharedStruct* SharedFragment = Archetype->GetSharedFragmentValues(Entity).GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(&FragmentType));
	check(SharedFragment != nullptr);
	return SharedFragment->GetMemory();
}

// 中文：tag 检查——tag 只影响 archetype 组成，不在 chunk 数据列里，因此直接问 archetype 是否含该 tag 类型。
//   先校验 EntityDataHandle 仍然合法（防止 archetype 已被改动导致 view 过期）。
bool FMassEntityView::HasTag(const UScriptStruct& TagType) const
{
	check(EntityDataHandle.IsValid(Archetype));
	return Archetype->HasTagType(&TagType);
}
