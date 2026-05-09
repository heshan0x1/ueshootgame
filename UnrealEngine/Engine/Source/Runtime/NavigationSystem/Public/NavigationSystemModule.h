// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationSystemModule.h
// -----------------------------------------------------------------------------
// NavigationSystem 模块的对外 IModuleInterface。
// 当前接口非常单薄 —— 仅提供 Get()/IsAvailable() 便捷访问；
// 模块初始化逻辑都在 Private/NavigationSystemModule.cpp 的 FNavigationSystemModule 里。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "Modules/ModuleManager.h"
//#include "AI/NavigationSystemBase.h"
#if WITH_EDITOR
#include "AssetTypeCategories.h"
#endif // WITH_EDITOR

/**
 * The public interface to this module
 */
// NavigationSystem 模块的对外接口。
class INavSysModule : public IModuleInterface
{

public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	// 单例式访问：必要时自动 Load 模块。注意：引擎关闭阶段不要再调。
	static inline INavSysModule& Get()
	{
		return FModuleManager::LoadModuleChecked<INavSysModule>("NavigationSystem");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	// 是否已加载可用：调 Get() 前最好先 IsAvailable()。
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("NavigationSystem");
	}
};

