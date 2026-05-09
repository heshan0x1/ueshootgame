// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   极小的编译单元——仅用于触发 UNavNodeInterface 的 UInterface 反射注册
//   （UHT 生成的代码需要在某个 cpp 里实例化构造函数）。
//   其它接口（UNavLinkHostInterface、UNavLinkCustomInterface、UNavigationPathGenerator）
//   的构造函数都放在 NavigationSystemTypes.cpp。
// =============================================================================

#include "NavNodeInterface.h"


// UInterface 的默认构造：仅供反射系统生成 CDO 使用，无额外逻辑。
UNavNodeInterface::UNavNodeInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

