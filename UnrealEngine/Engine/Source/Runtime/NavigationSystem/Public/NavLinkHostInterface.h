// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 INavLinkHostInterface —— "NavLink 的宿主" 接口。
//   一个 Actor 可以通过实现此接口，向 NavigationSystem 声明自身托管
//   了若干 UNavLinkDefinition 派生类（静态链接定义）或直接返回一组
//   NavigationLink / SegmentLink 数据，供 NavMesh Tile 生成时读取。
//
// 与其它文件的关系：
//   - 实现文件：Private/NavigationSystemTypes.cpp 提供 UObject 构造体。
//   - 与 INavLinkCustomInterface 的区别：本接口描述"静态链接定义集合"
//     （通常不会动态改变），Custom 版描述"单个可动态开关的链接实例"。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "NavLinkHostInterface.generated.h"

// UInterface 壳：反射/CDO 用。
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UNavLinkHostInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class INavLinkHostInterface
{
	GENERATED_IINTERFACE_BODY()
		
	/**
	 *	Retrieves UNavLinkDefinition derived UClasses hosted by this interface implementer
	 */
	// 纯虚：返回宿主 Actor 持有的所有 UNavLinkDefinition 子类（蓝图里配置的链接集）。
	virtual bool GetNavigationLinksClasses(TArray<TSubclassOf<class UNavLinkDefinition> >& OutClasses) const PURE_VIRTUAL(INavLinkHostInterface::GetNavigationLinksClasses,return false;);

	/** 
	 *	_Optional_ way of retrieving navigation link data - if INavLinkHostInterface 
	 *	implementer defines custom navigation links then it can just retrieve 
	 *	a list of links
	 */
	// 可选：若宿主直接构造链接而非通过 UClass 描述，可重写此函数返回链接数组。
	// 默认返回 false 表示"走 GetNavigationLinksClasses 的路径"。
	virtual bool GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const { return false; }
};
