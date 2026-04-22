// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Culture.h"
#include "IWebBrowserWindow.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"

#include "AIAssistantConfig.h"
#include "AIAssistantWebApi.h"
#include "AIAssistantWebApplication.h"
#include "Tests/AIAssistantFakeWebJavaScriptDelegateBinder.h"
#include "Tests/AIAssistantFakeWebJavaScriptExecutor.h"
#include "Tests/AIAssistantFakeWebApi.h"
#include "Tests/AIAssistantTestFlags.h"
#include "Tests/AIAssistantUefnModeConsoleVar.h"

using namespace UE::AIAssistant;

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::AIAssistant
{
	// Create a user message.
	static FAddMessageToConversationOptions CreateUserMessage()
	{
		FAddMessageToConversationOptions Options;
		auto& Message = Options.Message;
		Message.MessageRole = EMessageRole::User;
		auto& MessageContentItem = Message.MessageContent.Emplace_GetRef();
		MessageContentItem.bVisibleToUser = true;
		MessageContentItem.ContentType = EMessageContentType::Text;
		MessageContentItem.Content.Emplace<FTextMessageContent>();
		MessageContentItem.Content.Get<FTextMessageContent>().Text = TEXT("hello");
		return Options;
	}

	// Create a navigation request for opening a link in the main frame.
	static FWebNavigationRequest CreateWebNavigationRequestForLink()
	{
		FWebNavigationRequest Request;
		Request.bIsRedirect = false;
		Request.bIsMainFrame = true;
		Request.bIsExplicitTransition = false;
		Request.TransitionSource = EWebTransitionSource::Link;
		Request.TransitionSourceQualifier = EWebTransitionSourceQualifier::Unknown;
		return Request;
	}

	// Fake web API with a fake JavaScript execution environment.
	struct FFakeWebApiWithFakeJavaScriptEnvironment
	{
		FFakeWebApiWithFakeJavaScriptEnvironment() :
			WebApi(MakeShared<FFakeWebApi>(JavaScriptExecutor, JavaScriptDelegateBinder))
		{
		}

		FFakeWebJavaScriptExecutor JavaScriptExecutor;
		FFakeWebJavaScriptDelegateBinder JavaScriptDelegateBinder;
		TSharedPtr<FFakeWebApi> WebApi;
	};

	// Factory that tracks created FWebApi instances.
	struct FFakeWebApiTracker
	{
		TArray<TSharedPtr<FFakeWebApiWithFakeJavaScriptEnvironment>> WebApis;

		TFunction<TSharedPtr<FWebApi>()> CreateFactory()
		{
			return [this]() -> TSharedPtr<FWebApi>
				{
					auto WebApiContainer = MakeShared<FFakeWebApiWithFakeJavaScriptEnvironment>();
					WebApis.Add(WebApiContainer);
					return WebApiContainer->WebApi;
				};
		}
	};

	// Creates a web application with a default configuration while tracking web API instances.
	struct FWebApplicationWithTracker
	{
		FWebApplicationWithTracker(FAutomationTestBase& InTestBase) :
			Config(FAIAssistantConfig::Load()),
			WebApplication(MakeShared<FWebApplication>(WebApiTracker.CreateFactory())),
			TestBase(InTestBase)
		{
		}

		// Navigate to the main URL.
		void NavigateToMainUrl()
		{
			WebApplication->OnBeforeNavigation(
				Config.MainUrl, CreateWebNavigationRequestForLink());
			WebApplication->OnPageLoadComplete();
			(void)TestBase.TestEqual(
				TEXT("NotLoaded"), WebApplication->GetLoadState(),
				FWebApplication::ELoadState::NotLoaded);
		}

		// Get the most recently created fake web API and report an error if one isn't found.
		TSharedPtr<FFakeWebApi> GetWebApi() const
		{
			TSharedPtr<FFakeWebApi> WebApi;
			int32 NumberOfWebApis = WebApiTracker.WebApis.Num();
			if (NumberOfWebApis > 0)
			{
				WebApi = WebApiTracker.WebApis[NumberOfWebApis - 1]->WebApi;
			}
			(void)TestBase.TestTrue(TEXT("HasWebApi"), WebApi ? true : false);
			return WebApi;
		}

		// Ensure IsInitialized() is called to make sure the web API is available.
		bool TestExpectIsInitialized()
		{
			auto FakeWebApi = GetWebApi();
			FWebApiBoolResult Result;
			Result.bValue = true;
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, TEXT(""), *FFakeWebApi::GetWebApiAvailableFunction(), TEXT(""),
				*Result.ToJson(false), false);
		}

		// Ensure the locale was updated and complete the execution.
		bool TestExpectUpdateGlobalLocale()
		{
			auto FakeWebApi = GetWebApi();
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("updateGlobalLocale"),
				*FString::Printf(
					TEXT("\"%s\""), *FInternationalization::Get().GetCurrentLanguage()->GetName()),
				TEXT(""), false);
		}

		// Ensure the agent environment was added returning a reference to the fake handle if an agent
		// environment was added.
		TUniquePtr<FAgentEnvironmentHandle> TestExpectAddAgentEnvironment(
			TOptional<bool> ExpectedUefnMode = TOptional<bool>())
		{
			auto AgentEnvironmentHandle = MakeUnique<FAgentEnvironmentHandle>();
			AgentEnvironmentHandle->Hash.Hash = TEXT("fake_hash");
			AgentEnvironmentHandle->Id.Id = TEXT("fake_id");
			auto FakeWebApi = GetWebApi();
			bool bExpectedUefnMode =
				ExpectedUefnMode.IsSet() ? ExpectedUefnMode.GetValue() : IsUefnMode();
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("addAgentEnvironment"),
				*FWebApplication::GetAgentEnvironment(bExpectedUefnMode)->ToJson(false),
				*AgentEnvironmentHandle->ToJson(false), false)
				? MoveTemp(AgentEnvironmentHandle)
				: TUniquePtr<FAgentEnvironmentHandle>();
		}

		// Ensure an agent environment was set.
		bool TestExpectSetAgentEnvironment(const FAgentEnvironmentId& AgentEnvironmentId)
		{
			auto FakeWebApi = GetWebApi();
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("setAgentEnvironment"), *AgentEnvironmentId.ToJson(false),
				TEXT(""), false);
		}

		// Verify the application was initialized.
		bool TestInitialized()
		{
			bool bIsInitialized = 
				TestBase.TestEqual(TEXT("CreatedOneWebApi"), WebApiTracker.WebApis.Num(), 1);
			bIsInitialized = TestExpectIsInitialized() && bIsInitialized;
			bIsInitialized = TestExpectUpdateGlobalLocale() && bIsInitialized;
			bIsInitialized = TestBase.TestEqual(
				TEXT("StillNotLoaded"), WebApplication->GetLoadState(),
				FWebApplication::ELoadState::NotLoaded) && bIsInitialized;

			auto AgentEnvironmentHandle = TestExpectAddAgentEnvironment();
			bIsInitialized =
				AgentEnvironmentHandle.IsValid() &&
				TestExpectSetAgentEnvironment(AgentEnvironmentHandle->Id) &&
				bIsInitialized;
			bIsInitialized = TestBase.TestEqual(
				TEXT("Loaded"), WebApplication->GetLoadState(),
				FWebApplication::ELoadState::Complete) && bIsInitialized;
			return bIsInitialized;
		}

		// Ensure the application is initialized by navigating to the main URL and verifying
		// initializers have been called.
		bool EnsureInitialized()
		{
			NavigateToMainUrl();
			return TestInitialized();
		}

		// Verify that a conversation was created and complete the method.
		bool TestExpectCreateConversation()
		{
			auto WebApi = GetWebApi();
			return WebApi && WebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("createConversation"), TEXT(""), TEXT("{}"), false);
		}

		// Expect a JSON error 
		static const TCHAR* ExpectJavaScriptError(FAutomationTestBase& TestBase)
		{
			static const TCHAR* ErrorJson = TEXT(R"json({"error": "fake error"})json");
			TestBase.AddExpectedMessage(
				ErrorJson, EAutomationExpectedMessageFlags::MatchType::Contains, 1, false);
			return ErrorJson;
		}

		FFakeWebApiTracker WebApiTracker;
		FAIAssistantConfig Config;
		TSharedRef<FWebApplication> WebApplication;
		FAutomationTestBase& TestBase;
	};
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCreateWebApiFactory,
	"AI.Assistant.WebApplication.CreateWebApiFactory",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCreateWebApiFactory::RunTest(const FString& UnusedParameters)
{
	FFakeWebJavaScriptExecutor JavaScriptExecutor;
	FFakeWebJavaScriptDelegateBinder JavaScriptDelegateBinder;
	auto Factory = FWebApplication::CreateWebApiFactory(
		JavaScriptExecutor, JavaScriptDelegateBinder);
	if (!TestTrue(TEXT("FactoryIsSet"), Factory.IsSet())) return false;

	auto WebApi = Factory();
	if (!TestTrue(TEXT("FactoryCreatedWebApi"), WebApi.IsValid())) return false;

	// Ensure the web API is bound to the JavaScript environment.
	(void)TestEqual(
		TEXT("BoundApiToJavaScriptEnvironment"),
		JavaScriptDelegateBinder.BoundObjects.Num(), 1);
	// Make sure the web API is using the supplied executor.
	WebApi->UpdateGlobalLocale(TEXT("us-en"));
	(void)TestNotEqual(
		TEXT("ExecutedFunction"), JavaScriptExecutor.ExecutedJavaScriptText.Num(), 0);

	auto AnotherWebApi = Factory();
	if (!TestNotEqual(TEXT("FactoryCreatesNewWebApis"), WebApi, AnotherWebApi)) return false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestGetAgentEnvironmentUefnMode,
	"AI.Assistant.WebApplication.GetAgentEnvironmentUefnMode",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestGetAgentEnvironmentUefnMode::RunTest(
	const FString& UnusedParameters)
{
	auto AgentEnvironment = FWebApplication::GetAgentEnvironment(true);
	(void)TestEqual(
		TEXT("EnvironmentName"), AgentEnvironment->Descriptor.EnvironmentName, TEXT("UEFN"));
	(void)TestEqual(
		TEXT("EnvironmentVersion"), AgentEnvironment->Descriptor.EnvironmentVersion,
		FEngineVersion::Current().ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestGetAgentEnvironmentUeMode,
	"AI.Assistant.WebApplication.GetAgentEnvironmentUeMode",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestGetAgentEnvironmentUeMode::RunTest(
	const FString& UnusedParameters)
{
	auto AgentEnvironment = FWebApplication::GetAgentEnvironment(false);
	(void)TestEqual(
		TEXT("EnvironmentName"), AgentEnvironment->Descriptor.EnvironmentName, TEXT("UE"));
	(void)TestEqual(
		TEXT("EnvironmentVersion"), AgentEnvironment->Descriptor.EnvironmentVersion,
		FEngineVersion::Current().ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCreateUserMessageNoHiddenContext,
	"AI.Assistant.WebApplication.CreateUserMessageNoHiddenContext",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCreateUserMessageNoHiddenContext::RunTest(
	const FString& UnusedParameters)
{
	const TCHAR* VisibleMessage = TEXT("hello");
	auto MessageOptions = FWebApplication::CreateUserMessage(VisibleMessage, TEXT(""));
	(void)TestFalse(TEXT("ConversationId"), MessageOptions.ConversationId.IsSet());
	auto& Message = MessageOptions.Message;
	(void)TestEqual(TEXT("MessageRole"), Message.MessageRole, EMessageRole::User);
	if (!TestEqual(TEXT("MessageContentNum"), Message.MessageContent.Num(), 1)) return false;

	auto& MessageContent = Message.MessageContent[0];
	if (!(TestEqual(TEXT("ContentType"), MessageContent.ContentType, EMessageContentType::Text) &&
		TestTrue(TEXT("VisibleToUser"), MessageContent.bVisibleToUser)))
	{
		return false;
	}
	auto& TextMessageContent = MessageContent.Content.Get<FTextMessageContent>();
	(void)TestEqual(TEXT("VisibleText"), TextMessageContent.Text, VisibleMessage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCreateUserMessageHiddenContext,
	"AI.Assistant.WebApplication.CreateUserMessageHiddenContext",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCreateUserMessageHiddenContext::RunTest(
	const FString& UnusedParameters)
{
	const TCHAR* VisibleMessage = TEXT("hello");
	const TCHAR* HiddenContext = TEXT("hidden context");
	auto MessageOptions = FWebApplication::CreateUserMessage(VisibleMessage, HiddenContext);
	(void)TestFalse(TEXT("ConversationId"), MessageOptions.ConversationId.IsSet());
	auto& Message = MessageOptions.Message;
	(void)TestEqual(TEXT("MessageRole"), Message.MessageRole, EMessageRole::User);
	if (!TestEqual(TEXT("MessageContentNum"), Message.MessageContent.Num(), 2)) return false;

	auto& VisibleMessageContent = Message.MessageContent[0];
	auto& HiddenMessageContent = Message.MessageContent[1];
	if (!(
		TestEqual(
			TEXT("VisibleContentType"), VisibleMessageContent.ContentType,
			EMessageContentType::Text) &&
		TestTrue(TEXT("VisibleContentVisibleToUser"), VisibleMessageContent.bVisibleToUser) &&
		TestEqual(
			TEXT("HiddenContentType"), HiddenMessageContent.ContentType,
			EMessageContentType::Text) &&
		TestFalse(TEXT("HiddenContentVisibleToUser"), HiddenMessageContent.bVisibleToUser)))
	{
		return false;
	}
	auto& VisibleTextMessageContent = VisibleMessageContent.Content.Get<FTextMessageContent>();
	(void)TestEqual(TEXT("VisibleText"), VisibleTextMessageContent.Text, VisibleMessage);
	
	auto& HiddenTextMessageContent = HiddenMessageContent.Content.Get<FTextMessageContent>();
	(void)TestEqual(TEXT("HiddenText"), HiddenTextMessageContent.Text, HiddenContext);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestConstruct,
	"AI.Assistant.WebApplication.Construct",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestConstruct::RunTest(const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	(void)TestEqual(
		TEXT("LoadState"), WebApplication->GetLoadState(), FWebApplication::ELoadState::NotLoaded);
	return TestEqual(TEXT("NoWebApis"), WebApplicationWithTracker.WebApiTracker.WebApis.Num(), 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestOnBeforeNavigation,
	"AI.Assistant.WebApplication.OnBeforeNavigation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestOnBeforeNavigation::RunTest(const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	WebApplication->OnBeforeNavigation(
		WebApplicationWithTracker.Config.MainUrl, CreateWebNavigationRequestForLink());
	(void)TestEqual(
		TEXT("StillLoading"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::NotLoaded);
	
	WebApplication->OnBeforeNavigation(
		TEXT("https://not.assistant.url"), CreateWebNavigationRequestForLink());
	(void)TestEqual(
		TEXT("StillLoading"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::NotLoaded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestOnPageLoadError,
	"AI.Assistant.WebApplication.OnPageLoadError",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestOnPageLoadError::RunTest(const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	(void)WebApplication->OnBeforeNavigation(
		WebApplicationWithTracker.Config.MainUrl, CreateWebNavigationRequestForLink());
	WebApplication->OnPageLoadError();
	(void)TestEqual(
		TEXT("ErrorOccurred"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestPageLoadNonMainPage,
	"AI.Assistant.WebApplication.PageLoadNonMainPage",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestPageLoadNonMainPage::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	const TCHAR* NonAssistantPage = TEXT("https://unrealengine.com");
	(void)WebApplication->OnBeforeNavigation(
		NonAssistantPage, CreateWebNavigationRequestForLink());
	WebApplication->OnPageLoadComplete();
	(void)TestEqual(
		TEXT("NotLoaded"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::NotLoaded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestPageLoadMainPage,
	"AI.Assistant.WebApplication.PageLoadMainPage",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestPageLoadMainPage::RunTest(const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	return WebApplicationWithTracker.EnsureInitialized();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFails,
	"AI.Assistant.WebApplication.PageLoadMainPageIsInitializedFails",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFails::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	WebApplicationWithTracker.NavigateToMainUrl();

	auto FakeWebApi = WebApplicationWithTracker.GetWebApi();
	if (!FakeWebApi) return false;

	FWebApiBoolResult Result;
	(void)FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, TEXT(""), *FFakeWebApi::GetWebApiAvailableFunction(),
		TEXT(""), *Result.ToJson(false), false);

	(void)TestEqual(
		TEXT("NotLoaded"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::NotLoaded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFailsWithError,
	"AI.Assistant.WebApplication.PageLoadMainPageIsInitializedFailsWithError",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFailsWithError::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	WebApplicationWithTracker.NavigateToMainUrl();

	const TCHAR* ErrorJson = FWebApplicationWithTracker::ExpectJavaScriptError(*this);
	auto FakeWebApi = WebApplicationWithTracker.GetWebApi();
	if (!FakeWebApi) return false;

	(void)FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, TEXT(""), *FFakeWebApi::GetWebApiAvailableFunction(),
		TEXT(""), ErrorJson, true);

	(void)TestEqual(
		TEXT("FailedToLoad"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::Error);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestPageLoadMainPageUpdateEnvironmentFails,
	"AI.Assistant.WebApplication.PageLoadMainPageUpdateEnvironmentFails",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestPageLoadMainPageUpdateEnvironmentFails::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	WebApplicationWithTracker.NavigateToMainUrl();

	(void)WebApplicationWithTracker.TestExpectIsInitialized();
	(void)WebApplicationWithTracker.TestExpectUpdateGlobalLocale();

	const TCHAR* ErrorJson = FWebApplicationWithTracker::ExpectJavaScriptError(*this);
	auto FakeWebApi = WebApplicationWithTracker.GetWebApi();
	if (!FakeWebApi) return false;
	
	(void)FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("addAgentEnvironment"),
		*FWebApplication::GetAgentEnvironment(IsUefnMode())->ToJson(false),
		ErrorJson, true);

	(void)TestEqual(
		TEXT("SetAgentEnvironmentNotCalled"),
		FakeWebApi->FindExecutedAsyncFunctions(nullptr, TEXT("setAgentEnvironment")).Num(), 0);

	(void)TestEqual(
		TEXT("FailedToLoad"), WebApplication->GetLoadState(),
		FWebApplication::ELoadState::Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestChangeUefnMode,
	"AI.Assistant.WebApplication.ChangeUefnMode",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestChangeUefnMode::RunTest(const FString& UnusedParameters)
{
	auto UefnModeConsoleVariableRestorer = MakeShared<ScopedUefnModeConsoleVariableRestorer>();
	auto WebApplicationWithTracker = MakeShared<FWebApplicationWithTracker>(*this);
	if (!WebApplicationWithTracker->EnsureInitialized()) return false;

	// Clear executed functions.
	auto FakeWebApi = WebApplicationWithTracker->GetWebApi();
	if (!FakeWebApi) return false;
	FakeWebApi->ExecutedAsyncFunctions.Empty();

	// Change UEFN mode.
	bool bExpectedUefnMode = !IsUefnMode();
	auto* UefnModeVariable = FindUefnModeConsoleVariable();
	if (!TestTrue(TEXT("UEFNVar"), UefnModeVariable ? true : false)) return false;
	UefnModeVariable->Set(bExpectedUefnMode);

	// Wait for UEFN mode to change and make sure that it was updated.
	AddCommand(
		new FDelayedFunctionLatentCommand(
			[this, UefnModeConsoleVariableRestorer, WebApplicationWithTracker, bExpectedUefnMode]
			{
				auto AgentEnvironmentHandle =
					WebApplicationWithTracker->TestExpectAddAgentEnvironment(
						TOptional<bool>(bExpectedUefnMode));
				return AgentEnvironmentHandle.IsValid() &&
					WebApplicationWithTracker->TestExpectSetAgentEnvironment(
						AgentEnvironmentHandle->Id) &&
					TestEqual(
						TEXT("Loaded"),
						WebApplicationWithTracker->WebApplication->GetLoadState(),
						FWebApplication::ELoadState::Complete);
			},
			ConsoleVariableUpdateDelayInSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCultureChanged,
	"AI.Assistant.WebApplication.CultureChanged",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCultureChanged::RunTest(const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	if (!WebApplicationWithTracker.EnsureInitialized()) return false;

	auto WebApi = WebApplicationWithTracker.GetWebApi();
	if (!WebApi) return false;
	WebApi->ExecutedAsyncFunctions.Empty();

	FInternationalization::Get().OnCultureChanged().Broadcast();
	return WebApplicationWithTracker.TestExpectUpdateGlobalLocale();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCreateConversationNotLoaded,
	"AI.Assistant.WebApplication.CreateConversationNotLoaded",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCreateConversationNotLoaded::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	// If the application isn't loaded, creating a conversation should do nothing.
	WebApplication->CreateConversation();

	(void)TestEqual(
		TEXT("NoWebApis"), WebApplicationWithTracker.WebApiTracker.WebApis.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCreateConversationSuccessful,
	"AI.Assistant.WebApplication.CreateConversationSuccessful",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCreateConversationSuccessful::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	if (!WebApplicationWithTracker.EnsureInitialized()) return false;

	WebApplication->CreateConversation();
	auto WebApi = WebApplicationWithTracker.GetWebApi();
	if (!WebApi) return false;

	(void)WebApplicationWithTracker.TestExpectCreateConversation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestCreateConversationFailure,
	"AI.Assistant.WebApplication.CreateConversationFailure",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestCreateConversationFailure::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	if (!WebApplicationWithTracker.EnsureInitialized()) return false;

	WebApplication->CreateConversation();
	auto WebApi = WebApplicationWithTracker.GetWebApi();
	if (!WebApi) return false;

	const TCHAR* ErrorJson = FWebApplicationWithTracker::ExpectJavaScriptError(*this);
	(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("createConversation"), TEXT(""), ErrorJson, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestAddUserMessageToConversationNotLoaded,
	"AI.Assistant.WebApplication.AddUserMessageToConversationNotLoaded",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestAddUserMessageToConversationNotLoaded::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;

	WebApplication->AddUserMessageToConversation(CreateUserMessage());
	(void)TestEqual(
		TEXT("NoWebApis"), WebApplicationWithTracker.WebApiTracker.WebApis.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestAddUserMessageToConversation,
	"AI.Assistant.WebApplication.AddUserMessageToConversation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestAddUserMessageToConversation::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	if (!WebApplicationWithTracker.EnsureInitialized()) return false;

	WebApplication->AddUserMessageToConversation(CreateUserMessage());
	auto WebApi = WebApplicationWithTracker.GetWebApi();
	if (!WebApi) return false;

	(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("addMessageToConversation"), *CreateUserMessage().ToJson(false),
		TEXT("{}"), false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestAddUserMessageToConversationBeforeLoad,
	"AI.Assistant.WebApplication.AddUserMessageToConversationBeforeLoad",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestAddUserMessageToConversationBeforeLoad::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;

	WebApplication->AddUserMessageToConversation(CreateUserMessage());

	if (!WebApplicationWithTracker.EnsureInitialized()) return false;

	auto WebApi = WebApplicationWithTracker.GetWebApi();
	if (!WebApi) return false;

	(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("addMessageToConversation"), *CreateUserMessage().ToJson(false),
		TEXT("{}"), false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApplicationTestAddUserMessageToConversationAfterNewConversation,
	"AI.Assistant.WebApplication.AddUserMessageToConversationAfterNewConversation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApplicationTestAddUserMessageToConversationAfterNewConversation::RunTest(
	const FString& UnusedParameters)
{
	FWebApplicationWithTracker WebApplicationWithTracker(*this);
	auto WebApplication = WebApplicationWithTracker.WebApplication;
	if (!WebApplicationWithTracker.EnsureInitialized()) return false;

	WebApplication->CreateConversation();
	WebApplication->AddUserMessageToConversation(CreateUserMessage());

	auto WebApi = WebApplicationWithTracker.GetWebApi();
	if (!WebApi) return false;

	(void)TestEqual(
		TEXT("NoMessageAddedToConversation"),
		WebApi->FindExecutedAsyncFunctions(nullptr, TEXT("addMessageToConversation")).Num(), 0);

	(void)WebApplicationWithTracker.TestExpectCreateConversation();

	(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("addMessageToConversation"), *CreateUserMessage().ToJson(false),
		TEXT("{}"), false);
	return true;
}

#endif