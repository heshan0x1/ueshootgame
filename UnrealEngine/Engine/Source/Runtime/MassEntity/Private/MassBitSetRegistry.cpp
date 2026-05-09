// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassBitSetRegistry.h"

/*
 * 文件说明：
 *   为 FFragmentBitRegistry / FTagBitRegistry 提供显式模板特化的构造函数定义。头文件里只给了
 *   通用模板的默认构造（仅用 lambda 返回基类 UStruct），而这里的版本多传了一个 TypeValidation
 *   谓词给 StructTracker，使得向 tracker 注册类型时能再次校验"它确实是 FMassFragment / FMassTag
 *   体系里的合法类型"（通过 UE::Mass::IsA<Base> 判断）。
 *
 *   这样做的意义：防止用户给 FragmentBitRegistry 注册了一个虽然继承自 FMassFragment、
 *   但其实是别的用途（比如运行期生成的 UScriptStruct）的结构，导致位集语义被污染。
 *
 *   这两个特化都是进程级一次性构造（见头文件 using FFragmentBitRegistry = ...，以及相应的
 *   MASSENTITY_API 显式实例化声明），调用方通常通过 FMassEntityManager 间接访问到它们。
 */

namespace UE::Mass
{
	// FFragmentBitRegistry 的构造：基类设为 FMassFragment，校验器用 UE::Mass::IsA<FMassFragment>。
	template<>
	FFragmentBitRegistry::TBitTypeRegistry()
		: StructTracker(FMassFragment::StaticStruct()
			, [](const UStruct* Struct)
				{
					// 注册时的校验：确保传入结构确实在 FMassFragment 族中。
					return UE::Mass::IsA<FMassFragment>(Struct);
				})
	{	
	}

	// FTagBitRegistry 的构造：基类设为 FMassTag，校验器用 UE::Mass::IsA<FMassTag>。
	template<>
	FTagBitRegistry::TBitTypeRegistry()
		: StructTracker(FMassTag::StaticStruct()
			, [](const UStruct* Struct)
				{
					// 注册时的校验：确保传入结构确实在 FMassTag 族中。
					return UE::Mass::IsA<FMassTag>(Struct);
				})
	{	
	}
}
