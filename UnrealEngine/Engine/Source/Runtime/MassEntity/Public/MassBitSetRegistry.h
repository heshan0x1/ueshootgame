// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/StructUtilsTypes.h"
#include "TypeBitSetBuilder.h"
#include "MassEntityElementTypes.h"
#include "MassEntityConcepts.h"

/** Specialization of TTypeBitSetTraits for FMassTag */
/*
 * 中文说明：
 *   针对 FMassTag 的 traits 偏特化。把"派生自 FMassTag"的要求收窄成 UE::Mass::CTag concept
 *   （详见 MassEntityConcepts.h），并声明 RequiresBaseType=true，使得对应的 TypeBitSetBuilder
 *   在运行期也会做一次 IsChildOf(FMassTag::StaticStruct()) 校验，防止非 Tag 类型混入 TagBitSet。
 */
template <>
struct TTypeBitSetTraits<FMassTag> final
{
	/** Compile-time check to ensure that TTestedType is a valid Mass Tag */
	// 编译期约束：T 必须满足 CTag concept（继承自 FMassTag、USTRUCT 等）。
	template<typename TTestedType>
	static constexpr bool IsValidType = UE::Mass::CTag<TTestedType>;

	/** Indicates that a base type is required for inheritance checks */
	// 开启运行期继承校验，使得 TagBitSet 的 bTestInheritanceAtRuntime 模板参数默认为 true。
	static constexpr bool RequiresBaseType = true;
};

/** Specialization of TTypeBitSetTraits for FMassFragment */
/*
 * 中文说明：
 *   针对 FMassFragment 的 traits 偏特化，逻辑与 FMassTag 版本对称，只是把 concept 换成 CFragment。
 *   这两个偏特化让 "FragmentBitSet 和 TagBitSet 使用各自独立的 FStructTracker"这件事在类型系统
 *   里就被表达清楚，避免跨基类混用导致位索引冲突。
 */
template <>
struct TTypeBitSetTraits<FMassFragment> final
{
	/** Compile-time check to ensure that TTestedType is a valid Mass Fragment */
	template<typename TTestedType>
	static constexpr bool IsValidType = UE::Mass::CFragment<TTestedType>;

	/** Indicates that a base type is required for inheritance checks */
	static constexpr bool RequiresBaseType = true;
};

// UE::Mass 命名空间：Mass 模块公共类型与工具。
namespace UE::Mass
{
	// Private 命名空间：实现细节，外部代码应使用下方的 F*BitRegistry / F*BitSetBuilder 等别名而非直接用 TBitTypeRegistry。
	namespace Private
	{
		/**
		 * Template class for registering and managing bitsets for Mass types (e.g., Fragments and Tags).
		 * Provides functionality to create builders for constructing bitsets.
		 * The type hosts a FStructTracker instance that stores information on all the types used to 
		 * build bitsets via it, and only those types - as opposed to TStructTypeBitSet, which is using
		 * the same FStructTracker throughout the engine's instance lifetime.
		 *
		 * @param T the base Mass type (e.g., FMassFragment or FMassTag).
		 * @param TUStructType the Unreal Engine struct type, default is UScriptStruct.
		 *
		 * 中文说明：
		 *   TBitTypeRegistry 是 Mass 针对单一基类（如 FMassFragment 或 FMassTag）的"位集注册中心"。
		 *   每个实例内部持有自己专属的 FStructTracker，只记录该基类族下的类型，与引擎全局的
		 *   TStructTypeBitSet 共享注册表的做法相对：
		 *     - 好处：FragmentBitSet 和 TagBitSet 的位索引独立编号，不会互相挤占空间；
		 *     - 约束：跨 Registry 的位集不可互操作（位索引含义完全不同）。
		 *
		 *   它对外暴露两条构造路径：
		 *     - MakeBuilder(BitSet)：给已有 BitSet 绑一个 Builder 视图做增删改；
		 *     - MakeBuilder()：用 FBitSetFactory 构造一个"空 BitSet + 绑定好的 Builder"组合。
		 *
		 *   对应 Archetype 的 Fragment/Tag 位集都来自这里，所以这个注册中心的类型数量直接决定了
		 *   整个游戏工程"最多可能用到的 Mass 类型数"。
		 */
		template<typename T, typename TUStructType = UScriptStruct>
		struct TBitTypeRegistry
		{
			/** Alias for the bitset builder specific to the type T */
			// 特化后的 Builder 类型：bTestInheritanceAtRuntime 是否开启，由 TTypeBitSetTraits<T>::RequiresBaseType 决定。
			using FBitSetBuilder = TTypeBitSetBuilder<T, TUStructType, /*bTestInheritanceAtRuntime=*/TTypeBitSetTraits<T>::RequiresBaseType>;

			/** The type representing the runtime-used bitset. Const by design. */
			// 对外暴露的 BitSet 类型：故意设为 const，强迫调用方通过 Builder 修改，避免绕过 tracker 直接写入。
			using FBitSet = typename FBitSetBuilder::FConstBitSet;

			/**
			 * Factory type for creating and initializing bitsets.
			 * Inherits from FBitSetBuilder to provide building capabilities.
			 * Use this type when you want to build a bitset from scratch (i.e. when you 
			 * don't have a FBitSet instance you want to modify).
			 *
			 * 中文说明：
			 *   FBitSetFactory = "自带 BitSet 存储的 Builder"。继承自 FBitSetBuilder 的同时，
			 *   新增一个成员 BitSetInstance，并用它作为 Builder 指向的底层容器。这样用户可以
			 *   在栈上临时构造一个 factory，对它做 Add<T>()/Remove<T>()，最后把它当 FBitSet 复制出去。
			 *
			 *   注意：这里利用了"基类初始化发生在成员初始化之前"的 C++ 规则 —— 
			 *   FBitSetBuilder(InStructTracker, BitSetInstance) 里传入的是一个尚未构造的成员
			 *   BitSetInstance 的引用。只要 Builder 在构造过程中不读写 BitSet 的内容（它只是存引用），
			 *   这就是安全的，且配合 FBitSet 的 default 构造不做任何事就能成立。
			 */
			struct FBitSetFactory : FBitSetBuilder
			{
				/**
				 * Constructor that initializes the builder with a new bitset instance.
				 * @param InStructTracker the struct tracker to use for managing types.
				 */
				FBitSetFactory(FStructTracker& InStructTracker)
					: FBitSetBuilder(InStructTracker, BitSetInstance)
				{
				}

				/** The bitset instance being built */
				// 真正持有位数据的实例；Builder 部分只保存对它的引用。
				FBitSet BitSetInstance; 
			};

			/**
			 * Constructor that initializes the struct tracker.
			 * Uses a lambda to retrieve the UStruct representing the base type T.
			 *
			 * 中文说明：
			 *   默认通用构造：用 lambda 惰性获取基类反射对象。对 FMassFragment/FMassTag 等有偏特化
			 *   （见 MassBitSetRegistry.cpp），会传入更严格的 TypeValidation 函数，拒绝那些虽然继承
			 *   自基类但不是合法 Mass 类型（如直接继承自 FMassFragment 却忘了 USTRUCT 注册）的情况。
			 */
			TBitTypeRegistry()
				: StructTracker([](){ return StructUtils::GetAsUStruct<T>(); })
			{
			}

			/**
			 * Creates a bitset builder for an existing bitset.
			 * @param BitSet the bitset instance to modify
			 * @return A new FBitSetBuilder instance
			 *
			 * 中文说明：对已有 BitSet 生成 Builder。[[nodiscard]] 防止调用者忘记接住结果
			 * （builder 不保存本身就没意义）。
			 */
			[[nodiscard]] inline FBitSetBuilder MakeBuilder(FBitSet& BitSet) const
			{
				return FBitSetBuilder(StructTracker, BitSet);
			}

			/**
			 * Creates a factory for building new bitsets, essentially a FBitSetBuilder-BitSet combo.
			 * @return A new FBitSetFactory instance.
			 *
			 * 中文说明：构造"空 BitSet + 绑定好的 Builder"的 factory。
			 */
			[[nodiscard]] inline FBitSetFactory MakeBuilder() const
			{
				return FBitSetFactory(StructTracker);
			}

			/**
			 * Registers a type with the struct tracker.
			 * @param Type the UScriptStruct representing the type to register.
			 * @return The index assigned to the registered type.
			 *
			 * 中文说明：主动注册一个反射类型。一般由 Mass 的自动注册宏/启动代码触发，而不是手调用。
			 */
			inline int32 RegisterType(const UScriptStruct* Type)
			{
				return StructTracker.Register(*Type);
			}

			/**
			 * Template method to register a type with the struct tracker.
			 * @param TType the type to register.
			 * @return The index assigned to the registered type.
			 *
			 * 中文说明：模板版本，调用 TType::StaticStrict() —— 注意这里不是常见的 StaticStruct，
			 *   而是 StaticStrict，意在避免派生类被意外注册到父类的位上；具体实现见对应基类宏。
			 */
			template<typename TType>
			inline int32 RegisterType()
			{
				return RegisterType(TType::StaticStrict());
			}

			/**
			 * Specialized struct tracker for bitsets, disabling serialization.
			 * Inherits from FStructTracker.
			 *
			 * 中文说明：
			 *   针对位集场景定制的 FStructTracker 子类。当前唯一的不同是把 bIsSerializable 设为 false —— 
			 *   新版位集的序列化格式尚未最终定稿，先禁用以避免旧数据乱用。
			 */
			struct FBitSetStructTracker : public FStructTracker
			{
				/**
				 * Constructor that initializes the base type and optional type validation function.
				 * @param InBaseType the base UStruct type.
				 * @param InTypeValidation Optional function for type validation.
				 */
				FBitSetStructTracker(const UStruct* InBaseType, const FTypeValidation& InTypeValidation = FTypeValidation())
					: FStructTracker(InBaseType, InTypeValidation)
				{
					// Disable serialization, temporarily, until serialization is implemented for the new bitset type
					// 暂时关闭序列化：等新位集格式最终确定再打开，避免老存档拿到不一致的位索引。
					bIsSerializable = false;
				}
			};

			/** Struct tracker for managing types */
			// mutable：即便 TBitTypeRegistry 实例以 const 出现，也允许通过 Builder 向 tracker 注册新类型
			// —— Registry 对外的"只读"指的是 API 的语义（不会新增位集），类型注册这件事不算破坏 const。
			mutable FBitSetStructTracker StructTracker; 
		};
	} // namespace Private

	/** Alias for the fragment bit registry */
	// Fragment 专用注册中心（进程级单例，由 Mass 初始化代码持有）。
	using FFragmentBitRegistry = Private::TBitTypeRegistry<FMassFragment>;
	/** Alias for the fragment bitset builder */
	using FFragmentBitSetBuilder = FFragmentBitRegistry::FBitSetBuilder;
	/** Alias for a read-only fragment bitset builder */
	// Reader 就是 const Builder，语义是"只读视图"，常用作只需检查位集内容的函数参数类型。
	using FFragmentBitSetReader = const FFragmentBitSetBuilder;
	/** Alias for the fragment bitset factory */
	using FFragmentBitSetFactory = FFragmentBitRegistry::FBitSetFactory;

	/** Alias for the tag bit registry */
	// Tag 专用注册中心，与 Fragment 注册中心完全独立。
	using FTagBitRegistry = Private::TBitTypeRegistry<FMassTag>;
	/** Alias for the tag bitset builder */
	using FTagBitSetBuilder = FTagBitRegistry::FBitSetBuilder;
	/** Alias for a read-only tag bitset builder */
	using FTagBitSetReader = const FTagBitSetBuilder;
	/** Alias for the tag bitset factory */
	using FTagBitSetFactory = FTagBitRegistry::FBitSetFactory;

	/** Explicit template instantiation declarations for the registries */
	// 显式模板实例化声明：构造函数在 .cpp 中单独提供带 TypeValidation 的实现（防止错误类型注册进位集）。
	// 这两行告诉编译器不要在每个使用 TU 里各自生成默认版本的构造函数，而是去 MassBitSetRegistry.cpp 链接。
	template<>
	MASSENTITY_API FFragmentBitRegistry::TBitTypeRegistry();

	template<>
	MASSENTITY_API FTagBitRegistry::TBitTypeRegistry();

} // namespace UE::Mass

// 临时类型别名（_WIP 代表 Work In Progress）：在新旧位集体系并行时期，给上层代码提供一个
// 指向"新版位集类型"的稳定名字。未来完全切换后预计这两个别名会移除或替换名字。
using FMassFragmentBitSet_WIP = UE::Mass::FFragmentBitRegistry::FBitSet;
using FMassTagBitSet_WIP = UE::Mass::FTagBitRegistry::FBitSet;
