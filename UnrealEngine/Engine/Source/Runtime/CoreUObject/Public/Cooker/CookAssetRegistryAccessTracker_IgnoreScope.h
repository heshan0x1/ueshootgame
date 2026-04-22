// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"

namespace UE::CookAssetRegistryAccessTracker
{
	/** RAII class used to enable the creation of dependencies for the packages returned by an asset registry request. */
	class FIgnoreScope
	{
	public:
		COREUOBJECT_API FIgnoreScope();
		COREUOBJECT_API ~FIgnoreScope();

		COREUOBJECT_API static bool ShouldIgnoreAccessTracker();

	private:
		bool bPreviousValue;
	};
}
