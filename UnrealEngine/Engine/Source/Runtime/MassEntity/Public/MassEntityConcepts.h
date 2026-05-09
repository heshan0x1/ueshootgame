// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityElementTypes.h"
#include "MassExternalSubsystemTraits.h"
#include "Subsystems/Subsystem.h"


// =============================================================================
// Mass 类型谓词（C++20 concepts）
// =============================================================================
// 本文件把 MassEntityElementTypes.h 里的"基类继承关系"升级成编译期 concept。
// 作用：
//   1) 让 API 的模板参数能直接用 concept 约束，如
//        template<CFragment T> void Register();
//      比传统 SFINAE 更清晰，错误信息更友好；
//   2) 在 TRemoveReference 之后判断，允许调用方传 T& / const T&，concept 自动剥离；
//   3) CFragment 额外校验"trivially copyable 或显式豁免"，把内存安全约束前移到编译期。
// =============================================================================
namespace UE::Mass
{
	/**
	 * Clean<T> —— 去掉引用的"规范化类型"别名。
	 * 用它作为所有 concept 的入口 T 处理，避免调用方写 T& 时 concept 检查失败。
	 * 注意：这里没有同时去 const/volatile，是因为 Mass 约定不会传带 cv 修饰的 fragment，
	 * 而是通过参数是否 const 来体现 RO/RW 意图。
	 */
	template<typename T>
	using Clean = typename TRemoveReference<T>::Type;
	
	/**
	 * CFragment：是否为合法的"数据 Fragment"类型。
	 * 两个条件（逻辑与）：
	 *   (a) 继承自 FMassFragment；
	 *   (b) 要么本身就是 trivially copyable（POD 式），要么作者已通过
	 *       TMassFragmentTraits<T>::AuthorAcceptsItsNotTriviallyCopyable = true 显式豁免。
	 * 这对应 ECS 框架里"组件要能被高效按块搬运"的核心约束。
	 */
	template<typename T>
	concept CFragment = TIsDerivedFrom<Clean<T>, FMassFragment>::Value &&
		(
			std::is_trivially_copyable_v<Clean<T>> ||
			static_cast<bool>(TMassFragmentTraits<Clean<T>>::AuthorAcceptsItsNotTriviallyCopyable)
		);
	
	/** CTag：Tag（标签）类型，继承自 FMassTag。仅存在/缺失意义，不占数据空间。 */
	template<typename T>
	concept CTag = TIsDerivedFrom<Clean<T>, FMassTag>::Value;

	/** CChunkFragment：Chunk 级数据（按 Archetype chunk 共享一份）。 */
	template<typename T>
	concept CChunkFragment = TIsDerivedFrom<Clean<T>, FMassChunkFragment>::Value;

	/** CSharedFragment：多实体可写共享数据。 */
	template<typename T>
	concept CSharedFragment = TIsDerivedFrom<Clean<T>, FMassSharedFragment>::Value;

	/** CConstSharedFragment：多实体只读共享数据。 */
	template<typename T>
	concept CConstSharedFragment = TIsDerivedFrom<Clean<T>, FMassConstSharedFragment>::Value;

	/**
	 * CNonTag：四种"真正携带数据"的 Fragment 的并集。
	 * 用于需要区分"数据"与"标签"的 API，例如 Query 的 RequireFragment 系列只想要
	 * 带数据的类型，不想把 Tag 混进来。
	 */
	template<typename T>
	concept CNonTag = CFragment<T> || CChunkFragment<T> || CSharedFragment<T> || CConstSharedFragment<T>;

	/**
	 * CElement：所有 Mass 元素类型的总集（四种数据 + Tag）。
	 * "能加到 Archetype 组合键上的东西"都满足 CElement。
	 */
	template<typename T>
	concept CElement = CNonTag<T> || CTag<T>;

	/**
	 * CSubsystem：T 是否继承自 USubsystem（WorldSubsystem / GameInstanceSubsystem 等都包含）。
	 * 用来约束 Processor 声明外部依赖时传入的类型，避免把普通 UObject 误当 Subsystem。
	 */
	template<typename T>
	concept CSubsystem = TIsDerivedFrom<Clean<T>, USubsystem>::Value;

	namespace Private
	{
		/**
		 * TElementTypeHelper —— "把具体类型 T 映射回其元素基类"的编译期查找表。
		 * 使用嵌套 std::conditional_t 按"CFragment -> CTag -> CChunk -> CShared -> CConstShared"
		 * 的顺序依次判定，匹配到的第一个即为 Type；都不匹配则是 void（理论上被
		 * CElement 约束阻止，不会发生）。
		 *
		 * 用途：上层模板代码经常需要知道"我拿到的 T 属于哪一大类"，比如在 Archetype
		 * 组合键里分桶存放；TElementType<T> 提供了一个统一的答案，不需要每个调用点
		 * 手写 if-else。
		 */
		template<CElement T>
		struct TElementTypeHelper
		{
			using Type = std::conditional_t<CFragment<T>, FMassFragment
				, std::conditional_t<CTag<T>, FMassTag
				, std::conditional_t<CChunkFragment<T>, FMassChunkFragment
				, std::conditional_t<CSharedFragment<T>, FMassSharedFragment
				, std::conditional_t<CConstSharedFragment<T>, FMassConstSharedFragment
				, void>>>>>;
		};
	}

	/**
	 * TElementType<T> —— 对外暴露的"具体类型 -> 元素基类"别名。
	 * 例如 TElementType<FMyHealthFragment> == FMassFragment。
	 */
	template<typename T>
	using TElementType = typename Private::TElementTypeHelper<T>::Type;
}
