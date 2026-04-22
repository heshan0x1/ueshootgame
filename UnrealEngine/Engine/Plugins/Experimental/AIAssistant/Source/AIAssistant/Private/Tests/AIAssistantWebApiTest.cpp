// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Regex.h"
#include "Misc/AutomationTest.h"
#include "Templates/Tuple.h"

#include "AIAssistantFakeWebApi.h"
#include "AIAssistantFakeWebJavaScriptExecutor.h"
#include "AIAssistantFakeWebJavaScriptDelegateBinder.h"
#include "AIAssistantTestFlags.h"
#include "AIAssistantWebApi.h"
#include "AIAssistantWebJavaScriptResultDelegateAccessor.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UE::AIAssistant;

namespace UE::AIAssistant
{
	struct FFakeWebApiContainer
	{
		FFakeWebApiContainer() : WebApi(WebJavaScriptExecutor, WebJavaScriptDelegateBinder) {}

		FFakeWebJavaScriptExecutor WebJavaScriptExecutor;
		FFakeWebJavaScriptDelegateBinder WebJavaScriptDelegateBinder;
		FFakeWebApi WebApi;

		FFakeWebApi& operator*() { return WebApi; }
		FFakeWebApi* operator->() { return &WebApi; }
	};

	class FWebApiAccessor
	{
	public:
		static FString FormatFunctionCall(
			FWebApi& WebApi, const TCHAR* InstanceName,
			const TCHAR* FunctionName, const TCHAR* Arguments = TEXT(""),
			const FString& HandlerId = FString())
		{
			return WebApi.FormatFunctionCall(InstanceName, FunctionName, Arguments, HandlerId);
		}

		static TPair<FString, FString> FormatResultAndErrorHandlers(
			FWebApi& WebApi, const FString& HandlerId)
		{
			return WebApi.FormatResultAndErrorHandlers(HandlerId);
		}
		
		static UAIAssistantWebJavaScriptResultDelegate& GetJavaScriptResultDelegate(
			FWebApi& WebApi)
		{
			return *WebApi.WebJavaScriptResultDelegate;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallGlobalNoArgs,
	"AI.Assistant.WebApi.FormatFunctionCallGlobalNoArgs",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallGlobalNoArgs::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	return TestEqual(
		TEXT("FormatFunctionCallGlobalNoArgs"),
		FWebApiAccessor::FormatFunctionCall(*WebApi, nullptr, TEXT("test"), TEXT("")),
		TEXT(R"js(
try {
  Promise.resolve(test()).then(
    (result) => {
      
    },
    (error) => {
      
    });
} catch (error) {
  
}
)js"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallNoArgs,
	"AI.Assistant.WebApi.FormatFunctionCallNoArgs",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallNoArgs::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	return TestEqual(
		TEXT("FormatFunctionCallNoArgs"),
		FWebApiAccessor::FormatFunctionCall(*WebApi, TEXT("window.eda"), TEXT("test"), TEXT("")),
		TEXT(R"js(
try {
  Promise.resolve(window.eda.test()).then(
    (result) => {
      
    },
    (error) => {
      
    });
} catch (error) {
  
}
)js"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallWithArgs,
	"AI.Assistant.WebApi.FormatFunctionCallWithArgs",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallWithArgs::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	return TestEqual(
		TEXT("FormatFunctionCallWithArgs"),
		FWebApiAccessor::FormatFunctionCall(
			*WebApi, TEXT("window.eda"), TEXT("test"), TEXT("{foo: 'bar'}")),
		TEXT(R"js(
try {
  Promise.resolve(window.eda.test({foo: 'bar'})).then(
    (result) => {
      
    },
    (error) => {
      
    });
} catch (error) {
  
}
)js"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatResultAndErrorHandlers,
	"AI.Assistant.WebApi.FormatResultAndErrorHandlers",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatResultAndErrorHandlers::RunTest(
	const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	const FString FakeHandlerId = TEXT("foobar");
	const auto Handlers = FWebApiAccessor::FormatResultAndErrorHandlers(*WebApi, FakeHandlerId);
	const auto& JavaScriptResultDelegate = FWebApiAccessor::GetJavaScriptResultDelegate(*WebApi);
	(void)TestEqual(
		TEXT("ResultHandler"),
		Handlers.Key,
		FString::Format(
			*JavaScriptResultDelegate.FormatJavaScriptHandler(FakeHandlerId),
			{ TEXT("result"), TEXT("false") }));
	(void)TestEqual(
		TEXT("ErrorHandler"),
		Handlers.Value,
		FString::Format(
			*JavaScriptResultDelegate.FormatJavaScriptHandler(FakeHandlerId),
			{ TEXT("error"), TEXT("true") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallWithResultHandler,
	"AI.Assistant.WebApi.FormatFunctionCallWithResultHandler",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallWithResultHandler::RunTest(
	const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	const FString FakeHandlerId = TEXT("foobar");
	const auto Handlers = FWebApiAccessor::FormatResultAndErrorHandlers(*WebApi, FakeHandlerId);
	return TestEqual(
		TEXT("FormatFunctionCallWithResultHandler"),
		FWebApiAccessor::FormatFunctionCall(
			*WebApi, TEXT("window.eda"), TEXT("test"), TEXT(""), FakeHandlerId),
		FString::Format(
			TEXT(R"js(
try {
  Promise.resolve(window.eda.test()).then(
    (result) => {
      {HandleResult}
    },
    (error) => {
      {HandleError}
    });
} catch (error) {
  {HandleError}
}
)js"),
			{
				{ TEXT("HandleResult"), *Handlers.Key },
				{ TEXT("HandleError"), *Handlers.Value },
			}));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestIsAvailable,
	"AI.Assistant.WebApi.IsAvailable",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestIsAvailable::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	auto Result = WebApi->IsAvailable();
	FWebApiBoolResult ExpectedResult;
	ExpectedResult.bValue = true;
	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, TEXT(""),
		TEXT("(() => { return {value: Object.hasOwn(window, 'eda')}; })"),
		TEXT(""), Result, *ExpectedResult.ToJson(false), false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestCreateConversation,
	"AI.Assistant.WebApi.CreateConversation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestCreateConversation::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	auto Result = WebApi->CreateConversation();
	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("createConversation"), TEXT(""), Result, TEXT(""), false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestAddMessageToConversation,
	"AI.Assistant.WebApi.AddMessageToConversation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestAddMessageToConversation::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	FAddMessageToConversationOptions Options;
	Options.ConversationId.Emplace().Id = TEXT("convo");
	Options.Message.MessageRole = EMessageRole::User;
	FMessageContent& MessageContent = Options.Message.MessageContent.AddDefaulted_GetRef();
	MessageContent.ContentType = EMessageContentType::Text;
	MessageContent.Content.Emplace<FTextMessageContent>();
	MessageContent.Content.Get<FTextMessageContent>().Text = TEXT("Hello");
	WebApi->AddMessageToConversation(Options);
	return WebApi->TestExpectAsyncFunctionCall(
		*this, nullptr, TEXT("addMessageToConversation"), *Options.ToJson(false)).IsValid();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestAddAgentEnvironment,
	"AI.Assistant.WebApi.AddAgentEnvironment",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestAddAgentEnvironment::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	FAgentEnvironment AgentEnvironment;
	AgentEnvironment.Descriptor.EnvironmentName = TEXT("UE");
	AgentEnvironment.Descriptor.EnvironmentVersion = TEXT("5.7.0");
	auto Result = WebApi->AddAgentEnvironment(AgentEnvironment);

	FAgentEnvironmentHandle AgentEnvironmentHandle;
	AgentEnvironmentHandle.Id.Id = TEXT("fakeId");
	AgentEnvironmentHandle.Hash.Hash = TEXT("fakeHash");

	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("addAgentEnvironment"), *AgentEnvironment.ToJson(false),
		Result, *AgentEnvironmentHandle.ToJson(false), false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestAddAgentEnvironmentFailed,
	"AI.Assistant.WebApi.AddAgentEnvironmentFailed",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestAddAgentEnvironmentFailed::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	FAgentEnvironment AgentEnvironment;
	auto Result = WebApi->AddAgentEnvironment(AgentEnvironment);

	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, nullptr, TEXT("addAgentEnvironment"), *AgentEnvironment.ToJson(false),
		Result, TEXT("failed"), true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestSetAgentEnvironment,
	"AI.Assistant.WebApi.SetAgentEnvironment",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestSetAgentEnvironment::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	FAgentEnvironmentId Id;
	Id.Id = TEXT("fakeId");
	WebApi->SetAgentEnvironment(Id);
	return WebApi->TestExpectAsyncFunctionCall(
		*this, nullptr, TEXT("setAgentEnvironment"), *Id.ToJson(false)).IsValid();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestUpdateGlobalLocale,
	"AI.Assistant.WebApi.UpdateGlobalLocale",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestUpdateGlobalLocale::RunTest(const FString& UnusedParameters)
{
	FFakeWebApiContainer WebApi;
	FString LocaleFromSetting = TEXT("fr");
	WebApi->UpdateGlobalLocale(LocaleFromSetting);
	return WebApi->TestExpectAsyncFunctionCall(
		*this, nullptr, TEXT("updateGlobalLocale"), *FString::Printf(TEXT("\"%s\""),
		*LocaleFromSetting)).IsValid();
}

#endif  // WITH_DEV_AUTOMATION_TESTS