// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"

namespace UE::Editor::DataStorage::Compatibility
{
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTypedElementBridgeEnabled, bool /*bEnabled*/);

	TEDSTYPEDELEMENTBRIDGE_API FOnTypedElementBridgeEnabled& OnTypedElementBridgeEnabled();
	TEDSTYPEDELEMENTBRIDGE_API bool IsTypedElementBridgeEnabled();
} // namespace UE::Editor::DataStorage::Compatibility
