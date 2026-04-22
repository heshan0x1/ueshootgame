// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/TestHarnessAdapter.h"
#include "AutoRTFM.h"
#include "Tickable.h"

#if WITH_DEV_AUTOMATION_TESTS

TEST_CASE_NAMED(FAutoRTFMTickableTests, "AutoRTFM.FTickableObject", "[EngineFilter][ClientContext][ServerContext][CommandletContext][SupportsAutoRTFM]")
{
	struct FMyTickableGameObject final : FTickableGameObject
	{
		void Tick(float) override {}
		TStatId GetStatId() const override { return TStatId(); }
	};

	FMyTickableGameObject Tickable;
	Tickable.SetTickableTickType(ETickableTickType::Always);
}

#endif //WITH_DEV_AUTOMATION_TESTS
