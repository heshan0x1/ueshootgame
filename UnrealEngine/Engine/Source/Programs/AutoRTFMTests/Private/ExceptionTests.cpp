// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFMTesting.h"
#include "AutoRTFMTestUtils.h"
#include "Catch2Includes.h"
#include "BuildMacros.h"

#include <string>
#include <vector>

#if AUTORTFM_EXCEPTIONS_ENABLED

TEST_CASE("Exceptions")
{
	// Tests are sensitive to retries. Disable for these tests.
	AutoRTFMTestUtils::FScopedRetry Retry(AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::NoRetry);

	using EventList = std::vector<std::string>;
	EventList Events;

	auto Event = [&Events](std::string_view Event)
	{
		UE_AUTORTFM_OPEN
		{
			Events.push_back(std::string(Event));
		};
	};

	struct FScope
	{
		UE_AUTORTFM_ALWAYS_OPEN
		FScope(EventList& Events, const char* Name) : Events{Events}, Name{Name}
		{
			Events.push_back(std::string{Name});
		}
		UE_AUTORTFM_ALWAYS_OPEN
		~FScope()
		{
			Events.push_back(std::string{"End-"} + Name);
		}
		EventList& Events;
		char const* const Name;
	};
#define SCOPE(INDEX) FScope UE_AUTORTFM_CONCAT(Scope_, __COUNTER__){Events, INDEX}

	SECTION("Try(Transact())")
	{
		{
			SCOPE("Outer");
			try
			{
				SCOPE("Try");
				AutoRTFM::Testing::Commit([&]
				{
					SCOPE("Transact");
				});
			}
			catch (...)
			{
				SCOPE("Catch");
			}
		}

		const EventList Expected
		{
			"Outer",
			"Try",
			"Transact",
			"End-Transact",
			"End-Try",
			"End-Outer",
		};
		REQUIRE(Events == Expected);
	}

	SECTION("Try(Transact(Abort))")
	{
		{
			SCOPE("Outer");
			try
			{
				SCOPE("Try");
				AutoRTFM::Testing::Abort([&]
				{
					SCOPE("Transact");
					Event("Abort");
					AutoRTFM::AbortTransaction();
				});
			}
			catch (...)
			{
				SCOPE("Catch");
			}
		}

		const EventList Expected
		{
			"Outer",
			"Try",
			"Transact",
			"Abort",
			"End-Try",
			"End-Outer",
		};
		REQUIRE(Events == Expected);
	}

	SECTION("Try(Transact(Throw))")
	{
		{
			SCOPE("Outer");
			try
			{
				SCOPE("Try");
				AutoRTFM::Testing::Commit([&]
				{
					SCOPE("Transact");
					Event("Throw");
					throw(42);
				});
			}
			catch (int I)
			{
				SCOPE("Catch");
				REQUIRE(I == 42);
			}
		}

		const EventList Expected
		{
			"Outer",
			"Try",
			"Transact",
			"Throw",
			"End-Transact",
			"End-Try",
			"Catch",
			"End-Catch",
			"End-Outer",
		};
		REQUIRE(Events == Expected);
	}

	SECTION("Transact(Try(Throw))")
	{
		{
			SCOPE("Outer");
			AutoRTFM::Testing::Commit([&]
			{
				SCOPE("Transact");
				try
				{
					SCOPE("Try");
					Event("Throw");
					throw(42);
				}
				catch (int I)
				{
					SCOPE("Catch");
					REQUIRE(I == 42);
				}
			});
		}

		const EventList Expected
		{
			"Outer",
			"Transact",
			"Try",
			"Throw",
			"End-Try",
			"Catch",
			"End-Catch",
			"End-Transact",
			"End-Outer",
		};
		REQUIRE(Events == Expected);
	}

	SECTION("Transact(Try(Transact(Throw)))")
	{
		{
			SCOPE("Outer");
			AutoRTFM::Testing::Commit([&]
			{
				SCOPE("Transact-A");
				try
				{
					SCOPE("Try");
					AutoRTFM::Testing::Commit([&]
					{
						SCOPE("Transact-B");
						Event("Throw");
						throw(42);
					});
				}
				catch (int I)
				{
					SCOPE("Catch");
					REQUIRE(I == 42);
				}
			});
		}

		const EventList Expected
		{
			"Outer",
			"Transact-A",
			"Try",
			"Transact-B",
			"Throw",
			"End-Transact-B",
			"End-Try",
			"Catch",
			"End-Catch",
			"End-Transact-A",
			"End-Outer",
		};
		REQUIRE(Events == Expected);
	}
}

#endif // AUTORTFM_EXCEPTIONS_ENABLED
