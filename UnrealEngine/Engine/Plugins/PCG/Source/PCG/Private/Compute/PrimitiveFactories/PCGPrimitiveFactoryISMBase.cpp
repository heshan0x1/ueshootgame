// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/PrimitiveFactories/PCGPrimitiveFactoryISMBase.h"

namespace PCGPrimitiveFactoryHelpers
{
	static TFunction<TSharedPtr<IPCGPrimitiveFactoryISMBase>()> FastGeoPrimitiveFactoryGetter;

	TSharedPtr<IPCGPrimitiveFactoryISMBase> GetFastGeoPrimitiveFactory()
	{
		return FastGeoPrimitiveFactoryGetter ? FastGeoPrimitiveFactoryGetter() : nullptr;
	}

	namespace Private
	{
		void SetupFastGeoPrimitiveFactory(TFunction<TSharedPtr<IPCGPrimitiveFactoryISMBase>()>&& Getter)
		{
			ensure(!FastGeoPrimitiveFactoryGetter); // Trivial set and reset patterns expected.
			FastGeoPrimitiveFactoryGetter = MoveTemp(Getter);
		}

		void ResetFastGeoPrimitiveFactory()
		{
			ensure(FastGeoPrimitiveFactoryGetter); // Trivial set and reset patterns expected.
			FastGeoPrimitiveFactoryGetter.Reset();
		}
	}
}
