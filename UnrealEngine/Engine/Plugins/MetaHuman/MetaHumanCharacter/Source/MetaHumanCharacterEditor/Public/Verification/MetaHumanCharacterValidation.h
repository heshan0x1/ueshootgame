// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotNull.h"
#include "Internationalization/Text.h"
#include "UObject/Object.h"
#include "Logging/TokenizedMessage.h"
#include "IMetaHumanValidationContext.h"

#include "MetaHumanCharacterValidation.generated.h"


/**
 * Validation context used to encapsulate the validation process of MetaHuman Character items
 */
UCLASS()
class UMetaHumanCharacterValidationContext
	: public UObject
	, public IMetaHumanValidationContext
{
	GENERATED_BODY()

public:

	//~Begin IMetaHumanValidationContext interface
	virtual bool ValidateWardrobeItem(const class UMetaHumanWardrobeItem* InWardrobeItem) override;
	//~End IMetaHumanValidationContext interface

	struct FBeginReportParams
	{
		// The object to be validated
		UObject* ObjectToValidate = nullptr;

		// Prevents message from being logged in the Output Log
		bool bSilent = false;
	};

	/**
	 * @brief Begin a new report by constructing a validation context that can be used to validate items
	 * 
	 * @param InReportParams parameters to configure the report
	 */
	static UMetaHumanCharacterValidationContext* BeginReport(const FBeginReportParams& InReportParams);

	/**
	 * @brief Adds a new message to the report of the given severity
	 * 
	 * @param InSeverity the severity of the message to be added
	 * @return Reference to the newly added message so more tokens can be added to it
	 */
	TSharedRef<FTokenizedMessage> AddMessage(EMessageSeverity::Type InSeverity);

	/**
	 * @brief Return all the messages currently stored in the context
	 */
	const TArray<TSharedRef<FTokenizedMessage>>& GetMessages() const;

	/**
	 * @brief Cancel the current report meaning that when EndReport is called no messages are going to be displayed in the Message Log
	 */
	void CancelReport();

	/**
	 * @brief Ends the current report and displays the Message Log if there are errors or warnings collected in the report
	 */
	void EndReport();

public:

	/**
	 * @brief Utility to scope a report making sure its closed on the end of a scope
	 */
	struct FScopedReport
	{
		FScopedReport(const UMetaHumanCharacterValidationContext::FBeginReportParams& InParams)
			: Context{ UMetaHumanCharacterValidationContext::BeginReport(InParams) }
		{
		}

		~FScopedReport()
		{
			if (Context.IsValid())
			{
				Context->EndReport();
				Context->MarkAsGarbage();
			}
		}

		void Cancel() const
		{
			if (Context.IsValid())
			{
				Context->CancelReport();
			}
		}

		TStrongObjectPtr<UMetaHumanCharacterValidationContext> Context;
	};


private:

	// The object being validated
	UPROPERTY()
	TObjectPtr<UObject> ObjectBeingValidated;

	// Makes the internal report silent, meaning no messages are to be printed in the logs
	UPROPERTY()
	bool bSilent = false;

	// True if the report was cancelled
	UPROPERTY()
	bool bCancelled = false;

	// Current list of messages the report contains
	TArray<TSharedRef<FTokenizedMessage>> TokenizedMessages;
};