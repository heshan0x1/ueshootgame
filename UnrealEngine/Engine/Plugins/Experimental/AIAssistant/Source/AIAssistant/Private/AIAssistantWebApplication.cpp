// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantWebApplication.h"

#include "Async/UniqueLock.h"
#include "Internationalization/Culture.h"
#include "Misc/EngineVersion.h"

namespace UE::AIAssistant
{
	FWebApplication::FWebApplication(
		TFunction<TSharedPtr<FWebApi>()>&& WebApiFactory) :
		ConversationReadyExecutor(
			[this]() -> FExecuteWhenReady::EExecuteWhenReadyState
			{
				// Extend the conversation ready executor to query the current URL and block
				// execution if an error has occurred.
				return LoadState == ELoadState::Error
					? FExecuteWhenReady::EExecuteWhenReadyState::Reject
					: IsLoaded()
						? FExecuteWhenReady::EExecuteWhenReadyState::Execute
						: FExecuteWhenReady::EExecuteWhenReadyState::Wait;
			}),
		CreateWebApi(MoveTemp(WebApiFactory))
	{
	}

	FWebApplication::~FWebApplication()
	{
		if (CultureChangeDelegateHandle.IsValid())
		{
			FInternationalization::Get().OnCultureChanged().Remove(CultureChangeDelegateHandle);
		}
	}

	void FWebApplication::CreateConversation()
	{
		(void)WithWebApi(
			[this](FWebApi& WebApi) -> void
			{
				if (!ConversationReadyExecutor.SetCreatingConversation(true))
				{
					WebApi.CreateConversation().Then(
						[WeakThis = SharedThis(this).ToWeakPtr()](
							const TFuture<TValueOrError<void, FString>>& ResultFuture) -> void
						{
							auto This = WeakThis.Pin();
							if (This)
							{
								This->ConversationReadyExecutor.SetCreatingConversation(false);
								(void)This->HandleErrorResult(
									TEXT("CreateConversation"), ResultFuture.Get());
							}
						});
				}
			});
	}

	void FWebApplication::AddUserMessageToConversation(
		FAddMessageToConversationOptions&& Options)
	{
		WithWebApiWhenConversationReady(
			[WeakThis = SharedThis(this).ToWeakPtr(),
			 Options = MoveTemp(Options)](FWebApi& WebApi) -> void
			{
				auto This = WeakThis.Pin();
				if (This) WebApi.AddMessageToConversation(Options);
			});
	}

	void FWebApplication::OnBeforeNavigation(
		const FString& Url, const FWebNavigationRequest& Request)
	{
		(void)Url;
		(void)Request;
		bApplicationAvailable = false;
		bPageLoaded = false;
	}

	void FWebApplication::OnPageLoadComplete()
	{
		EnsureInitialized();
		bPageLoaded = true;

		EnsureWebApi()->IsAvailable().Then(
			[WeakThis = SharedThis(this).ToWeakPtr()](const auto& IsAvailableFuture) mutable
			{
				auto This = WeakThis.Pin();
				if (!This) return;

				// If the assistant isn't loaded, reset the application's state.
				const auto& IsAvailable = IsAvailableFuture.Get();
				if (This->HandleErrorResult(TEXT("IsAvailable"), IsAvailable))
				{
					return;
				}
				if (!(IsAvailable.HasValue() && IsAvailable.GetValue()))
				{
					This->ResetState();
					return;
				}

				UE::TUniqueLock StateLock(This->StateMutex);
				This->bApplicationAvailable = true;

				// Configure the assistant and execute any pending operations.
				This->OnCultureChanged();
				This->TryUpdateAgentEnvironment(This->bRequestedAgentEnvironmentIsUefn);
				This->ConversationReadyExecutor.UpdateExecuteWhenReady();
			});
	}

	void FWebApplication::OnPageLoadError()
	{
		ResetState(ELoadState::Error);
	}

	FWebApplication::ELoadState FWebApplication::GetLoadState() const
	{
		UE::TUniqueLock StateLock(StateMutex);
		return LoadState;
	}

	void FWebApplication::OnCultureChanged()
	{
		(void)WithWebApi(
			[](FWebApi& WebApi) -> void
			{
				WebApi.UpdateGlobalLocale(
					FInternationalization::Get().GetCurrentLanguage()->GetName());
			});
	}

	void FWebApplication::TryUpdateAgentEnvironment(bool bUseUefnMode)
	{
		bRequestedAgentEnvironmentIsUefn = bUseUefnMode;
		// If the mode hasn't changed and the agent environment has been configured, do nothing.
		bool bUefnModeChanged =
			!bAgentEnvironmentIsUefn.IsSet() ||
			bAgentEnvironmentIsUefn.GetValue() != bRequestedAgentEnvironmentIsUefn;
		if (!bUefnModeChanged && ConversationReadyExecutor.IsAgentEnvironmentConfigured()) return;

		(void)WithWebApi(
			[this, bUseUefnMode](FWebApi& WebApi) -> void
			{
				// Configure the agent's environment.
				WebApi.AddAgentEnvironment(*GetAgentEnvironment(bUseUefnMode)).Then(
					[WeakThis = SharedThis(this).ToWeakPtr(), bUseUefnMode](
						const auto& ResultFuture) mutable -> void
					{
						auto This = WeakThis.Pin();
						if (!This) return;

						const auto& Result = ResultFuture.Get();
						if (This->HandleErrorResult(
							TEXT("UpdateAgentEnvironment"), ResultFuture.Get()))
						{
							return;
						}
						const auto& EnvironmentId = Result.GetValue().Id;
						if (!This->WithWebApi(
							[&This, bUseUefnMode, &EnvironmentId](FWebApi& WebApi)
							{
								WebApi.SetAgentEnvironment(EnvironmentId);
								This->bAgentEnvironmentIsUefn.Emplace(bUseUefnMode);
								This->ConversationReadyExecutor.NotifyAgentEnvironmentConfigured();
								This->LoadState = ELoadState::Complete;
							}))
						{
							This->ResetState(ELoadState::Error);
						}
					});
			});
	}

	void FWebApplication::EnsureInitialized()
	{
		UE::TUniqueLock Lock(InitializationMutex);
		// It's not possible to use SharedThis() on construction as the shared pointer hasn't been
		// allocated at that point so we lazy initialize listeners here.

		// Subscribe to culture / language modifications.
		if (!CultureChangeDelegateHandle.IsValid())
		{
			CultureChangeDelegateHandle = FInternationalization::Get().OnCultureChanged().AddSP(
				SharedThis(this), &FWebApplication::OnCultureChanged);
		}

		// Subscribe to editor mode updates.
		if (!UefnModeSubscription.IsSet())
		{
			UefnModeSubscription.Emplace(
				[WeakThis = SharedThis(this).ToWeakPtr()](bool bIsUefnMode) -> void
				{
					auto This = WeakThis.Pin();
					if (This) This->TryUpdateAgentEnvironment(bIsUefnMode);
				});
		}
	}

	TSharedPtr<FWebApi> FWebApplication::EnsureWebApi()
	{
		UE::TUniqueLock StateLock(StateMutex);
		if (!MaybeWebApi) MaybeWebApi = CreateWebApi();
		return MaybeWebApi;
	}

	bool FWebApplication::WithWebApi(TFunction<void(FWebApi&)>&& UsingWebApi)
	{
		// Ensure the assistant application is loaded.
		if (!IsLoaded()) return false;

		UsingWebApi(*EnsureWebApi());
		return true;
	}

	void FWebApplication::WithWebApiWhenConversationReady(
		TFunction<void(FWebApi&)>&& UsingWebApi)
	{
		ConversationReadyExecutor.ExecuteWhenReady(
			[WeakThis = SharedThis(this).ToWeakPtr(),
			UsingWebApi = MoveTemp(UsingWebApi)]() mutable -> void
			{
				auto This = WeakThis.Pin();
				if (This) (void)This->WithWebApi(MoveTemp(UsingWebApi));
			});
	}

	void FWebApplication::ResetState(const TOptional<ELoadState>&& NewLoadState)
	{
		UE::TUniqueLock StateLock(StateMutex);
		MaybeWebApi.Reset();
		bAgentEnvironmentIsUefn.Reset();
		ConversationReadyExecutor.Reset();
		bPageLoaded = false;
		bApplicationAvailable = false;
		if (NewLoadState.IsSet()) LoadState = NewLoadState.GetValue();
	}

	bool FWebApplication::IsLoaded() const
	{
		// Ensure the assistant application is loaded.
		return bApplicationAvailable && bPageLoaded &&
			GetLoadState() != ELoadState::Error;
	}

	TFunction<TSharedPtr<FWebApi>()> FWebApplication::CreateWebApiFactory(
		IWebJavaScriptExecutor& JavaScriptExecutor,
		IWebJavaScriptDelegateBinder& JavaScriptDelegateBinder)
	{
		return [&JavaScriptExecutor, &JavaScriptDelegateBinder]() -> TSharedPtr<FWebApi>
			{
				return MakeShared<FWebApi>(JavaScriptExecutor, JavaScriptDelegateBinder);
			};
	}

	TUniquePtr<FAgentEnvironment> FWebApplication::GetAgentEnvironment(bool bUseUefnMode)
	{
		auto AgentEnvironment = MakeUnique<FAgentEnvironment>();
		auto& Descriptor = AgentEnvironment->Descriptor;
		Descriptor.EnvironmentName = bUseUefnMode ? TEXT("UEFN") : TEXT("UE");
		Descriptor.EnvironmentVersion = FEngineVersion::Current().ToString();
		return AgentEnvironment;
	}

	FAddMessageToConversationOptions FWebApplication::CreateUserMessage(
		const FString& VisiblePrompt, const FString& HiddenContext)
	{
		FAddMessageToConversationOptions Options;
		auto& Message = Options.Message;
		Message.MessageRole = EMessageRole::User;
		auto& MessageContent = Message.MessageContent;
		for (const auto& PromptAndVisible : {
				TPair<const FString&, bool>(VisiblePrompt, true),
				TPair<const FString&, bool>(HiddenContext, false),
			})
		{
			const auto& Prompt = PromptAndVisible.Key;
			bool bVisible = PromptAndVisible.Value;
			if (Prompt.IsEmpty()) continue;

			auto& MessageContentItem = MessageContent.Emplace_GetRef();
			MessageContentItem.bVisibleToUser = bVisible;
			MessageContentItem.ContentType = EMessageContentType::Text;
			MessageContentItem.Content.Emplace<FTextMessageContent>();
			MessageContentItem.Content.Get<FTextMessageContent>().Text = Prompt;
		}
		return MoveTemp(Options);
	}
}