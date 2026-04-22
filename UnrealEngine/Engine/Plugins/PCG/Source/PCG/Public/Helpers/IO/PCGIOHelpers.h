// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/Accessors/PCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

namespace PCG::IO
{
	namespace Constants
	{
		namespace Attribute
		{
			// Custom PCG version for attribute export
			struct FCustomExportVersion
			{
				enum Type
				{
					InitialVersion = 0,

					// New versions can be added above this line
					VersionPlusOne,
					LatestVersion = VersionPlusOne - 1
				};

				static FName GetFriendlyName() { return FName(TEXT("PCG::IO::AttributeExport")); }
				const static FGuid GUID;
			};
		} // namespace Attribute

		static constexpr int32 InlineAllocationCount = 16;
	} // namespace Constants

	namespace Helpers::String
	{
		void ToPrecisionString(const double Value, const int32 Precision, FString& OutString);
	}

	namespace Accessor
	{
		struct FCacheEntry
		{
			FPCGAttributePropertySelector Selector;
			TUniquePtr<const IPCGAttributeAccessor> Accessor = nullptr;
			TUniquePtr<const IPCGAttributeAccessorKeys> Keys = nullptr;
		};

		using FCache = TArray<FCacheEntry, TInlineAllocator<Constants::InlineAllocationCount>>;
	} // namespace Accessor
} // namespace PCG::IO
