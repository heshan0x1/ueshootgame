// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFM.h"
#include "AutoRTFMTestUtils.h"
#include "AutoRTFMTesting.h"
#include "MyAutoRTFMTestObject.h"
#include "Misc/PackageName.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Catch2Includes.h"

static void TrashPackage(UPackage* const Package)
{
	// TODO: If we could move the trashing logic into `UPackage` we could just call that here?
	const FName NewName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), NAME_TrashedPackage);
	Package->Rename(*NewName.ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	Package->SetFlags(RF_Transient);
}

TEST_CASE("UPackage.SetPackageFlagsTo")
{
	SECTION("Commit")
	{
		UPackage* Package = NewObject<UPackage>();
		Package->SetPackageFlagsTo(PKG_None);

		AutoRTFM::Testing::Commit([&]
		{
			Package->SetPackageFlagsTo(PKG_TransientFlags);
		});

		REQUIRE(Package->GetPackageFlags() == PKG_TransientFlags);
	}

	SECTION("Abort")
	{
		UPackage* Package = NewObject<UPackage>();
		Package->SetPackageFlagsTo(PKG_None);

		AutoRTFM::Testing::Abort([&]
		{
			Package->SetPackageFlagsTo(PKG_TransientFlags);
			REQUIRE(Package->GetPackageFlags() == PKG_TransientFlags);
			AutoRTFM::AbortTransaction();
		});

		REQUIRE(Package->GetPackageFlags() == PKG_None);
	}
}

TEST_CASE("UPackage.SetPackageFlags")
{
	SECTION("Commit")
	{
		UPackage* Package = NewObject<UPackage>();
		Package->SetPackageFlagsTo(PKG_RuntimeGenerated);

		AutoRTFM::Testing::Commit([&]
		{
			Package->SetPackageFlags(PKG_TransientFlags);
		});

		REQUIRE(Package->GetPackageFlags() == (PKG_RuntimeGenerated | PKG_TransientFlags));
	}

	SECTION("Abort")
	{
		UPackage* Package = NewObject<UPackage>();
		Package->SetPackageFlagsTo(PKG_RuntimeGenerated);

		AutoRTFM::Testing::Abort([&]
		{
			Package->SetPackageFlags(PKG_TransientFlags);
			REQUIRE(Package->GetPackageFlags() == (PKG_RuntimeGenerated | PKG_TransientFlags));
			AutoRTFM::AbortTransaction();
		});

		REQUIRE(Package->GetPackageFlags() == PKG_RuntimeGenerated);
	}
}

TEST_CASE("UPackage.ClearPackageFlags")
{
	SECTION("Commit")
	{
		UPackage* Package = NewObject<UPackage>();
		Package->SetPackageFlagsTo(PKG_RuntimeGenerated | PKG_TransientFlags);

		AutoRTFM::Testing::Commit([&]
		{
			Package->ClearPackageFlags(PKG_TransientFlags);
		});

		REQUIRE(Package->GetPackageFlags() == PKG_RuntimeGenerated);
	}

	SECTION("Abort")
	{
		UPackage* Package = NewObject<UPackage>();
		Package->SetPackageFlagsTo(PKG_RuntimeGenerated | PKG_TransientFlags);

		AutoRTFM::Testing::Abort([&]
		{
			Package->ClearPackageFlags(PKG_TransientFlags);
			REQUIRE(Package->GetPackageFlags() == PKG_RuntimeGenerated);
			AutoRTFM::AbortTransaction();
		});

		REQUIRE(Package->GetPackageFlags() == (PKG_RuntimeGenerated | PKG_TransientFlags));
	}
}

// Based on `Engine\Private\Tests\Loading\AsyncLoadingTests_Shared.h`, we use similar logic
// here to make a package that the loader will see and be able to actually load!
struct FPackageScopedMaker final
{
	FString PackagePath;

	FPackageScopedMaker(FString PackageName) : PackagePath(FPackageName::LongPackageNameToFilename(*PackageName, FPackageName::GetAssetPackageExtension()))
	{
		// We need to remove any previous package of the same name (could have occurred if a previous test ran segfaulted for instance).
		if (FPackageName::DoesPackageExist(PackageName))
		{
			REQUIRE(IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackagePath, false));
			REQUIRE(IPlatformFile::GetPlatformPhysical().DeleteFile(*PackagePath));
		}

		// Ensure that async loading is done.
		FlushAsyncLoading();

		// Create a package.
		UPackage* const Package = CreatePackage(*PackageName);
		
		// With at least one object in it.
		FString ObjectName(PackageName);
		ObjectName.Append(".TestObject");
		UObject* const Object = NewObject<UMyAutoRTFMTestObject>(Package, *ObjectName, RF_Public | RF_Standalone);

		// Need to mark it is loaded.
		Package->MarkAsFullyLoaded();

		// Then wipe the standalone flag for reasons.
		Object->ClearFlags(RF_Standalone);

		// Save the package to the file-system.
		REQUIRE(UPackage::SavePackage(Package, nullptr, *PackagePath, FSavePackageArgs()));

		// Make sure the package existed in our tables before.
		REQUIRE(FindObject<UObject>(nullptr, *PackageName) != nullptr);

		// GC and make sure everything gets cleaned up before loading.
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		// Then make sure the package is no longer loaded in our tables after.
		REQUIRE(FindObject<UObject>(nullptr, *PackageName) == nullptr);
	}
};

TEST_CASE("UPackage.AsyncLoading")
{
	SECTION("DoesPackageExist")
	{
		AutoRTFM::Testing::Commit([&]
		{
			FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
			REQUIRE(!FPackageName::DoesPackageExist(Name));
		});
		
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		AutoRTFM::Testing::Commit([&]
		{
			REQUIRE(FPackageName::DoesPackageExist(Name));
		});
	}

	SECTION("LoadPackageAsync")
	{
		int32 RequestId = -1;

		AutoRTFM::Testing::Commit([&]
		{
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			RequestId = LoadPackageAsync(Name);
		});

		FlushAsyncLoading(RequestId);
	}

	SECTION("IsAsyncLoading")
	{
		int32 RequestId = -1;

		AutoRTFM::Testing::Commit([&]
		{
			REQUIRE(!IsAsyncLoading());

			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			RequestId = LoadPackageAsync(Name);

			REQUIRE(IsAsyncLoading());
		});

		FlushAsyncLoading(RequestId);
	}

	SECTION("FlushAsyncLoading")
	{
		FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
		int32 RequestId = LoadPackageAsync(Name);

		AutoRTFM::Testing::Commit([&]
		{
			FlushAsyncLoading(RequestId);
		});
	}

	SECTION("FlushAsyncLoading Empty")
	{
		FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
		int32 RequestId = LoadPackageAsync(Name);

		AutoRTFM::Testing::Commit([&]
		{
			FlushAsyncLoading();
		});
	}

	SECTION("FlushAsyncLoading One In One Out")
	{
		FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
		int32 RequestId1 = LoadPackageAsync(Name);

		AutoRTFM::Testing::Commit([&]
		{
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			int32 RequestId2 = LoadPackageAsync(Name);
			TArray<int32> RequestIds;
			RequestIds.Add(RequestId1);
			RequestIds.Add(RequestId2);
			FlushAsyncLoading(RequestIds);
		});
	}

	SECTION("CompletionDelegate is called closed")
	{
		AutoRTFM::Testing::Abort([&]
		{
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			FLoadPackageAsyncDelegate CompletionDelegate;
			CompletionDelegate.BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type) { REQUIRE(AutoRTFM::IsClosed()); });
			int RequestId = LoadPackageAsync(Name, CompletionDelegate);
			FlushAsyncLoading(RequestId);
			AutoRTFM::AbortTransaction();
		});
	}
	
	SECTION("CompletionDelegate aborts")
	{
		AutoRTFM::Testing::Abort([&]
		{
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			FLoadPackageAsyncDelegate CompletionDelegate;
			CompletionDelegate.BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type) { AutoRTFM::AbortTransaction(); });
			int RequestId = LoadPackageAsync(Name, CompletionDelegate);
			FlushAsyncLoading(RequestId);
			FAIL("Unreachable!");
		});
	}
	
	SECTION("FLoadPackageAsyncOptionalParams::CompletionDelegate is called closed")
	{
		AutoRTFM::Testing::Abort([&]
		{
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			FLoadPackageAsyncOptionalParams Params;
			Params.CompletionDelegate.Reset(new FLoadPackageAsyncDelegate());
			Params.CompletionDelegate->BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type) { REQUIRE(AutoRTFM::IsClosed()); });
			int RequestId = LoadPackageAsync(Name, MoveTemp(Params));
			FlushAsyncLoading(RequestId);
			AutoRTFM::AbortTransaction();
		});
	}
	
	SECTION("FLoadPackageAsyncOptionalParams::CompletionDelegate aborts")
	{
		AutoRTFM::Testing::Abort([&]
		{
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			FLoadPackageAsyncOptionalParams Params;
			Params.CompletionDelegate.Reset(new FLoadPackageAsyncDelegate());
			Params.CompletionDelegate->BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type) { AutoRTFM::AbortTransaction(); });
			int RequestId = LoadPackageAsync(Name, MoveTemp(Params));
			FlushAsyncLoading(RequestId);
			FAIL("Unreachable!");
		});
	}
	
	SECTION("FLoadPackageAsyncOptionalParams::CompletionDelegate creates UObject")
	{
		UMyAutoRTFMTestObject* OpenObject = nullptr;
		UMyAutoRTFMTestObject* ClosedObject = nullptr;

		AutoRTFM::Testing::Abort([&]
		{
			ClosedObject = NewObject<UMyAutoRTFMTestObject>();
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			FLoadPackageAsyncOptionalParams Params;
			Params.CompletionDelegate.Reset(new FLoadPackageAsyncDelegate());
			Params.CompletionDelegate->BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type)
			{
				OpenObject = NewObject<UMyAutoRTFMTestObject>();
				AutoRTFM::AbortTransaction();
			});
			int RequestId = LoadPackageAsync(Name, MoveTemp(Params));
			FlushAsyncLoading(RequestId);
			FAIL("Unreachable!");
		});

		REQUIRE(nullptr == ClosedObject);
		REQUIRE(nullptr == OpenObject);
	}
	
	SECTION("FLoadPackageAsyncOptionalParams::CompletionDelegate calls another LoadPackageAsync")
	{
		AutoRTFM::Testing::Abort([&]
		{
			FLoadPackageAsyncOptionalParams Params;
			Params.CompletionDelegate.Reset(new FLoadPackageAsyncDelegate());
			Params.CompletionDelegate->BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type)
			{
				int RequestId = LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), MoveTemp(Params));
				AutoRTFM::AbortTransaction();
			});
			int RequestId = LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), MoveTemp(Params));
			FlushAsyncLoading(RequestId);
			FAIL("Unreachable!");
		});
	}

	SECTION("Multiple retries because of multiple loads with commit")
	{
		AutoRTFMTestUtils::FScopedRetry Retry(AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::NoRetry);

		int NumCompletionCallbacks = 0;
		AutoRTFM::Testing::Commit([&]
		{
			FLoadPackageAsyncDelegate CompletionDelegate;
			CompletionDelegate.BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type) 
			{
				// Do this open so we can check how many retries occurred.
				AutoRTFM::Open([&] { NumCompletionCallbacks++; });
			});

			TArray<int32> RequestIds;
			RequestIds.Add(LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), CompletionDelegate));
			RequestIds.Add(LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), CompletionDelegate));
			RequestIds.Add(LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), CompletionDelegate));

			FlushAsyncLoading(RequestIds);
			REQUIRE(3 == NumCompletionCallbacks);
		});
	}

	SECTION("Multiple retries because of multiple loads with abort")
	{
		AutoRTFMTestUtils::FScopedRetry Retry(AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::NoRetry);

		int NumCompletionCallbacks = 0;
		AutoRTFM::Testing::Abort([&]
		{
			FLoadPackageAsyncDelegate CompletionDelegate;
			CompletionDelegate.BindLambda([&](const FName&, UPackage*, EAsyncLoadingResult::Type) 
			{
				// Do this open so we can check how many retries occurred.
				AutoRTFM::Open([&] { NumCompletionCallbacks++; });
			});

			TArray<int32> RequestIds;
			RequestIds.Add(LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), CompletionDelegate));
			RequestIds.Add(LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), CompletionDelegate));
			RequestIds.Add(LoadPackageAsync(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__), CompletionDelegate));

			FlushAsyncLoading(RequestIds);
			REQUIRE(3 == NumCompletionCallbacks);

			AutoRTFM::AbortTransaction();
		});
	}

	SECTION("Stack Local Linker Instancing Context")
	{
		int32 RequestId = -1;

		AutoRTFM::Testing::Commit([&]
		{
			FLinkerInstancingContext Context;
			FLoadPackageAsyncOptionalParams Params;
			Params.InstancingContext = &Context;
			FString Name(FString::Printf(TEXT("/AutoRTFMTestPackage%d"), __LINE__));
			RequestId = LoadPackageAsync(Name, MoveTemp(Params));
		});

		FlushAsyncLoading(RequestId);
	}

	SECTION("Trashed Package")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
		REQUIRE(Package);
			
		TrashPackage(Package);

		AutoRTFM::Testing::Commit([&]
		{
			UPackage* const ReloadedPackage = LoadPackage(nullptr, *Name, LOAD_None);
			REQUIRE(ReloadedPackage);
			REQUIRE(ReloadedPackage->GetFName() == Name);

			REQUIRE(Package != ReloadedPackage);
		});
	}

	SECTION("Find Package Loaded In Transaction")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		AutoRTFM::Testing::Commit([&]
		{
			// Make sure the package doesn't exist.
			REQUIRE(FindObject<UObject>(nullptr, *Name) == nullptr);

			UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
			REQUIRE(Package);

			TArray<UObject*> Objects;

			REQUIRE(StaticFindAllObjects(Objects, UObject::StaticClass(), *Name));
			REQUIRE(Objects.Num() == 1);
			REQUIRE(Objects[0] == Package);
		});
	}

	SECTION("Trash Package that was created in same transaction as reloaded")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		AutoRTFM::Testing::Commit([&]
		{
			UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
			REQUIRE(Package);
			
			TrashPackage(Package);

			UPackage* const ReloadedPackage = LoadPackage(nullptr, *Name, LOAD_None);
			REQUIRE(ReloadedPackage);
			REQUIRE(ReloadedPackage->GetFName() == Name);

			REQUIRE(Package != ReloadedPackage);
		});
	}

	SECTION("Trash Package that was found in the same transaction")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);
		UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
		REQUIRE(Package);

		AutoRTFM::Testing::Commit([&]
		{
			UPackage* const FoundPackage = FindObject<UPackage>(nullptr, *Name);
			REQUIRE(Package == FoundPackage);
			
			TrashPackage(Package);

			UPackage* const ReloadedPackage = LoadPackage(nullptr, *Name, LOAD_None);
			REQUIRE(ReloadedPackage);
			REQUIRE(ReloadedPackage->GetFName() == Name);

			REQUIRE(Package != ReloadedPackage);
		});

		UPackage* const FoundPackage = FindObject<UPackage>(nullptr, *Name);
		REQUIRE(Package != FoundPackage);
	}

	SECTION("Multiple retries to load multiple packages")
	{
		FString Name1(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _1(Name1);

		FString Name2(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _2(Name2);

		FString Name3(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _3(Name3);

		FString Name4(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _4(Name4);

		TArray<FString> PackagesToLoad({Name1, Name2, Name3, Name4});

		AutoRTFM::Testing::Commit([&]
		{
			for (const FString& Name : PackagesToLoad)
			{
				UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
				REQUIRE(Package);
			}
		});
	}

	SECTION("Multiple retries to load multiple packages with trashing")
	{
		FString Name1(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _1(Name1);

		FString Name2(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _2(Name2);

		FString Name3(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _3(Name3);

		FString Name4(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _4(Name4);

		TArray<FString> PackagesToLoad({Name1, Name2, Name3, Name4});

		AutoRTFM::Testing::Commit([&]
		{
			for (const FString& Name : PackagesToLoad)
			{
				UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
				REQUIRE(Package);

				TrashPackage(Package);

				UPackage* const ReloadedPackage = LoadPackage(nullptr, *Name, LOAD_None);
				REQUIRE(ReloadedPackage);
				REQUIRE(ReloadedPackage->GetFName() == Name);

				REQUIRE(Package != ReloadedPackage);
			}
		});
	}

	SECTION("Multiple async loads")
	{
		FString Name1(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _1(Name1);

		FString Name2(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _2(Name2);

		AutoRTFM::Testing::Commit([&]
		{
			REQUIRE(FindObject<UPackage>(nullptr, *Name1) == nullptr);
			REQUIRE(FindObject<UPackage>(nullptr, *Name2) == nullptr);

			FLoadPackageAsyncDelegate Delegate;
			const int32 Id1 = LoadPackageAsync(Name1, Delegate);
			const int32 Id2 = LoadPackageAsync(Name2, Delegate);
			FlushAsyncLoading(Id2);
			
			REQUIRE(FindObject<UPackage>(nullptr, *Name2) != nullptr);
		});
	}

	SECTION("Load With Custom Name")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		AutoRTFM::Testing::Commit([&]
		{
			FName CustomName("Wowwee");

			FLoadPackageAsyncDelegate Delegate;

			Delegate.BindLambda([&](const FName& Name, UPackage* const Package, EAsyncLoadingResult::Type Result)
			{
				REQUIRE(EAsyncLoadingResult::Succeeded == Result);
				REQUIRE(Package->GetFName() == Name);
				REQUIRE(CustomName == Name);
			});

			FlushAsyncLoading(LoadPackageAsync(FPackagePath::FromPackageNameUnchecked(*Name), CustomName, Delegate));
		});
	}

	SECTION("Trash And Load With Custom Name")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		bool bHit = false;

		UPackage* const Package = LoadPackage(nullptr, *Name, LOAD_None);
		REQUIRE(Package);

		AutoRTFM::Testing::Commit([&]
		{
			TrashPackage(Package);

			FName CustomName("Wowwee");

			FLoadPackageAsyncDelegate Delegate;

			Delegate.BindLambda([&](const FName& Name, UPackage* const Package, EAsyncLoadingResult::Type Result)
			{
				REQUIRE(AutoRTFM::IsClosed());
				REQUIRE(EAsyncLoadingResult::Succeeded == Result);
				REQUIRE(Package->GetFName() == Name);
				REQUIRE(CustomName == Name);
				bHit = true;
			});

			FlushAsyncLoading(LoadPackageAsync(FPackagePath::FromPackageNameUnchecked(*Name), CustomName, Delegate));
		});

		REQUIRE(bHit);
	}

	SECTION("First Time Load Package, Second Time Don't")
	{
		FString Name(FString::Printf(TEXT("/Game/%dAutoRTFMTestPackage%d"), FPlatformProcess::GetCurrentProcessId(), __LINE__));
		FPackageScopedMaker _(Name);

		bool bHit = false;

		bool bFirst = true;

		FName CustomName("Wowwee");

		FLoadPackageAsyncDelegate Delegate;

		Delegate.BindLambda([&](const FName& Name, UPackage* const Package, EAsyncLoadingResult::Type Result)
		{
			REQUIRE(!AutoRTFM::IsTransactional());
			REQUIRE(EAsyncLoadingResult::Succeeded == Result);
			REQUIRE(Package->GetFName() == Name);
			REQUIRE(CustomName == Name);
			bHit = true;
		});

		AutoRTFM::Testing::Commit([&]
		{
			if (!bFirst)
			{
				return;
			}

			AutoRTFM::OnComplete([&] { bFirst = false; });

			FlushAsyncLoading(LoadPackageAsync(FPackagePath::FromPackageNameUnchecked(*Name), CustomName, Delegate));
		});

		FlushAsyncLoading(LoadPackageAsync(FPackagePath::FromPackageNameUnchecked(*Name), CustomName, Delegate));

		REQUIRE(bHit);
	}
}
