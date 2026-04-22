// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IPersonaToolkit;

namespace UE::UAF
{

	class IAbstractSkeletonEditor
	{
	public:
		virtual ~IAbstractSkeletonEditor() = default;
		virtual TSharedPtr<IPersonaToolkit> GetPersonaToolkit() const = 0;
	};

}