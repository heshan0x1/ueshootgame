// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFM.h"
#include "AutoRTFM/OpenWrapper.h"
#include "AutoRTFMTesting.h"
#include "Catch2Includes.h"

TEST_CASE("OpenWrapper")
{
	struct FObject
	{
		FObject()
		{
			REQUIRE(!AutoRTFM::IsClosed());
		}
		FObject(const FObject&)
		{
			REQUIRE(!AutoRTFM::IsClosed());
		}
		FObject(FObject&&)
		{
			REQUIRE(!AutoRTFM::IsClosed());
		}
		FObject& operator=(const FObject&)
		{
			REQUIRE(!AutoRTFM::IsClosed());
			return *this;
		}
		FObject& operator=(FObject&&)
		{
			REQUIRE(!AutoRTFM::IsClosed());
			return *this;
		}
		~FObject()
		{
			REQUIRE(!AutoRTFM::IsClosed());
		}
	};

	FObject Object;
	AutoRTFM::Testing::Commit([&]
	{
		AutoRTFM::TOpenWrapper WrapperA{Object};
		AutoRTFM::TOpenWrapper WrapperB{std::move(WrapperA)};
		AutoRTFM::TOpenWrapper WrapperC = WrapperB;
		AutoRTFM::TOpenWrapper WrapperD = std::move(WrapperC);
	});
}
