// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Mutex.h"
#include "Async/RecursiveMutex.h"
#include "Containers/UnrealString.h"
#include "Delegates/IDelegateInstance.h"
#include "IWebBrowserWindow.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/ValueOrError.h"

#include "AIAssistantConsole.h"
#include "AIAssistantLog.h"
#include "AIAssistantConversationReadyExecutor.h"
#include "AIAssistantWebApi.h"
#include "AIAssistantWebJavaScriptDelegateBinder.h"
#include "AIAssistantWebJavaScriptExecutor.h"


namespace UE::AIAssistant
{
	// Assistant web application.
	class FWebApplication : public TSharedFromThis<FWebApplication, ESPMode::ThreadSafe>
	{
	public:
		// Load state of the AI Assistant URL.
		enum class ELoadState
		{
			NotLoaded,
			Error,
			Complete,
		};

	public:
		// This *must* be constructed using MakeShared.
		explicit FWebApplication(
			TFunction<TSharedPtr<FWebApi>()>&& WebApiFactory);

		~FWebApplication();

		// Disable move and delete.
		FWebApplication(const FWebApplication&) = delete;
		FWebApplication& operator=(const FWebApplication&) = delete;
		FWebApplication(FWebApplication&&) = delete;
		FWebApplication& operator=(FWebApplication&&) = delete;

		// Create a new conversation.
		void CreateConversation();

		// Add a user message to the existing conversation.
		void AddUserMessageToConversation(
			FAddMessageToConversationOptions&& Options);

		// Should be called before navigation to a new webpage occurs.
		void OnBeforeNavigation(const FString& Url, const FWebNavigationRequest& Request);

		// Should be called to notify that a page load is complete.
		void OnPageLoadComplete();

		// Notify the application that a page load error occurred.
		void OnPageLoadError();

		// Get the load state of the application.
		// If the application enters ELoadState::Error a new instance of this object must be
		// created to use the application.
		ELoadState GetLoadState() const;

	private:
		// Handle language / culture changed notification.
		void OnCultureChanged();

		// Update the agent environment exposed to the assistant.
		void TryUpdateAgentEnvironment(bool bUseUefnMode);

		// Check a result for an error, logging the error if one is present, setting the load state
		// to error and returning true. Returns false if the result does not have an error.
		template<typename ValueType>
		bool HandleErrorResult(
			const FString& HandlerContext,
			const TValueOrError<ValueType, FString>& Result)
		{
			if (Result.HasError())
			{
				UE_LOG(
					LogAIAssistant, Error,
					TEXT("JavaScript Execution Failed %s: '%s'"),
					*HandlerContext, *Result.GetError());
				ResetState(ELoadState::Error);
				return true;
			}
			return false;
		}

		// Ensure state that needs to lazily initialized is initialized.
		void EnsureInitialized();

		// Get / create the web API.
		TSharedPtr<FWebApi> EnsureWebApi();

		// Ensure the web API is initialized and get a reference to it if possible.
		// Returns true if the function was executed, false if the web API was not available.
		// 
		// NOTE: The provided reference to the web API is only valid within the current stack
		// frame. If the web API needs to be accessed later (e.g in a future continuation) it
		// should be requested again using this method.
		bool WithWebApi(TFunction<void(FWebApi&)>&& UsingWebApi);

		// Use the web API when the conversation is ready.
		// IMPORTANT: See caveats in WithWebApi() documentation.
		// NOTE: It's possible that UsingWebApi is never called if ConversationReadyExecutor's
		// queue is flushed.
		void WithWebApiWhenConversationReady(
			TFunction<void(FWebApi&)>&& UsingWebApi);

		// Reset the application state.
		void ResetState(const TOptional<ELoadState>&& NewLoadState = TOptional<ELoadState>());

		// Whether the assistant application has been loaded.
		bool IsLoaded() const;

	private:
		// Handles deferring adding messages until a conversation is ready.
		FConversationReadyExecutor ConversationReadyExecutor;
		// Used to construct the web API.
		const TFunction<TSharedPtr<FWebApi>()> CreateWebApi;
		UE::FMutex InitializationMutex;
		// Subscription to a cvar that controls the mode of the assistant.
		TOptional<FUefnModeSubscription> UefnModeSubscription;
		// Reference to a culture change notification delegate.
		FDelegateHandle CultureChangeDelegateHandle;

		// Guards:
		// * MaybeWebApi
		// * bAgentEnvironmentIsUefn
		// * bRequestedAgentEnvironmentIsUefn
		// * bPageLoaded
		// * bApplicationAvailable
		// * LoadState
		mutable UE::FRecursiveMutex StateMutex;

		// Interface for the assistant web application.
		TSharedPtr<FWebApi> MaybeWebApi;

		// Current agent environment mode if set.
		TOptional<bool> bAgentEnvironmentIsUefn;
		// Requested agent environment mode.
		bool bRequestedAgentEnvironmentIsUefn = false;

		// Whether a page load is complete.
		bool bPageLoaded = false;
		// Whether the application is available on the loaded page.
		bool bApplicationAvailable = false;
		// State of the application.
		ELoadState LoadState = ELoadState::NotLoaded;

	public:
		// Create a factory for web API instances.
		static TFunction<TSharedPtr<FWebApi>()> CreateWebApiFactory(
			IWebJavaScriptExecutor& JavaScriptExecutor,
			IWebJavaScriptDelegateBinder& JavaScriptDelegateBinder);

		// Get the current agent environment.
		static TUniquePtr<FAgentEnvironment> GetAgentEnvironment(bool bUseUefnMode);

		// Create a user message from visible and hidden prompt.
		static FAddMessageToConversationOptions CreateUserMessage(
			const FString& VisiblePrompt, const FString& HiddenContext);
	};
}