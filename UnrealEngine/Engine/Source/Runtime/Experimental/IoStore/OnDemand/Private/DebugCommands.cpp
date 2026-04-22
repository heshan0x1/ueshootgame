// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugCommands.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IO/IoChunkId.h"
#include "IO/IoDispatcher.h"
#include "IO/IoStoreOnDemand.h"
#include "Misc/AssertionMacros.h"
#include "OnDemandIoStore.h"
#include "String/Numeric.h"
#include "IasHostGroup.h"

#if !UE_BUILD_SHIPPING

namespace UE::IoStore
{

FOnDemandDebugCommands::FOnDemandDebugCommands(FOnDemandIoStore* InOnDemandIoStore)
	: OnDemandIoStore(InOnDemandIoStore)
{
	check(InOnDemandIoStore != nullptr);

	BindConsoleCommands();
}

FOnDemandDebugCommands::~FOnDemandDebugCommands()
{
	UnbindConsoleCommands();
}

void FOnDemandDebugCommands::BindConsoleCommands()
{
#if !NO_CVARS
	DynamicConsoleCommands.Emplace(
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("ias.InvokeHttpFailure"),
			TEXT("Marks the current ias http connections as failed and forcing them  to try to reconnect"),
			FConsoleCommandDelegate::CreateLambda([this]()
				{
					UE_LOG(LogIas, Display, TEXT("User invoked http error via 'ias.InvokeHttpFailure'"));
					FHostGroupManager::Get().DisconnectAll();
				}),
			ECVF_Default)
	);

	DynamicConsoleCommands.Emplace(
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("ias.RunRequestTest"),
			TEXT("[optional Number of requests to make] Creates a number of requests for chunks that can be found in that IAS system"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FOnDemandDebugCommands::RunRequestTest),
			ECVF_Default));
#endif // !NO_CVARS
}

void FOnDemandDebugCommands::UnbindConsoleCommands()
{
	for (IConsoleCommand* Cmd : DynamicConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Cmd);
	}
}

void FOnDemandDebugCommands::RunRequestTest(const TArray<FString>& Args) const
{
	int32 NumToRequest = -1;
	const float CancelWaitTime = 5.0f;
	bool bCancelRequests = false;
	bool bQuitAfterTest = false;


	for (const FString& Arg : Args)
	{
		if (UE::String::IsNumeric(Arg))
		{
			if (NumToRequest != -1)
			{
				UE_LOG(LogIas, Error, TEXT("Too many numeric args for 'ias.RunRequestTest'"));
				return;
			}

			int32 ArgValue = TCString<TCHAR>::Atoi(*Arg);
			NumToRequest = ArgValue > 0 ? ArgValue : MAX_int32;
		}
		else if (Arg == "Cancel")
		{
			bCancelRequests = true;
		}
		else if (Arg == "Quit")
		{
			bQuitAfterTest = true;
		}
		else
		{
			UE_LOG(LogIas, Error, TEXT("Unknown arg '%s' for 'ias.RunRequestTest'"), *Arg);
			return;
		}
	}

	// Set number of requests if it was not provided as an arg
	NumToRequest = NumToRequest == -1 ? 1000 : NumToRequest;

	TArray<FIoChunkId> RequestIds = OnDemandIoStore->DebugFindStreamingChunkIds(NumToRequest);

	UE_LOG(LogIas, Display, TEXT("Running IAS Request Test with %d requests..."), RequestIds.Num());

	TArray<FIoRequest> Requests;

	FIoBatch IoBatch;
	for (const FIoChunkId& Id : RequestIds)
	{
		FIoRequest Request = IoBatch.Read(Id, FIoReadOptions(), IoDispatcherPriority_Medium);
		Requests.Emplace(MoveTemp(Request));
	}

	const double StartTime = FPlatformTime::Seconds();

	IoBatch.IssueWithCallback([StartTime, bQuitAfterTest]()
		{
			const double TotalTime = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogIas, Display, TEXT("IAS Request Test took %.3f(s)"), TotalTime);

			if (bQuitAfterTest)
			{
				UE_LOG(LogIas, Display, TEXT("Quitting now that the request batch has completed"));
				FPlatformMisc::RequestExitWithStatus(false, 0);
			}
		});

	if (bCancelRequests)
	{
		UE_LOG(LogIas, Display, TEXT("Requests will be canceled in %f seconds"), CancelWaitTime);

		FTSTicker::GetCoreTicker().AddTicker(TEXT("IASDebugCancel"), CancelWaitTime, [Requests = MoveTemp(Requests)](float DeltaTime) mutable
			{
				for (FIoRequest& Request : Requests)
				{
					Request.Cancel();
				}

				UE_LOG(LogIas, Display, TEXT("Canceled all requests!"));
				return false;
			});
	}

	UE_CLOG(bQuitAfterTest, LogIas, Display, TEXT("Process will quit once the requests have been completed"));
}

} // namespace UE::IoStore

#endif // !UE_BUILD_SHIPPING
