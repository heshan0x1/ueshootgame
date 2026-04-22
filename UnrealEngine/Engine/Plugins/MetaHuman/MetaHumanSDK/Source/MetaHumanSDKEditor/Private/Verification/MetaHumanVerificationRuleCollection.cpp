// Copyright Epic Games, Inc. All Rights Reserved.

#include "Verification/MetaHumanVerificationRuleCollection.h"

#include "MetaHumanAssetReport.h"

#include "EngineAnalytics.h"
#include "MetaHumanSDKEditor.h"
#include "Misc/RuntimeErrors.h"
#include "Misc/ScopedSlowTask.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanVerificationRuleCollection)

#define LOCTEXT_NAMESPACE "MetaHumanVerificationRuleCollection"

void UMetaHumanVerificationRuleCollection::AddVerificationRule(UMetaHumanVerificationRuleBase* Rule)
{
	if (ensureAsRuntimeWarning(Rule))
	{
		Rules.Add(Rule);
	}
}

UMetaHumanAssetReport* UMetaHumanVerificationRuleCollection::ApplyAllRules(const UObject* Target, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const
{
	if (ensureAsRuntimeWarning(Report))
	{
		Report->SetVerbose(Options.bVerbose);
		Report->SetWarningsAsErrors(Options.bTreatWarningsAsErrors);
		if (Target)
		{
			FScopedSlowTask RulesTask(Rules.Num(), LOCTEXT("RulesProgressMessage", "Applying verification rules."));
			const float Delay = 2.0f;
			RulesTask.MakeDialogDelayed(Delay);
			Report->SetSubject(Target->GetName());
			for (const TObjectPtr<UMetaHumanVerificationRuleBase>& Rule : Rules)
			{
				RulesTask.EnterProgressFrame(1, FText::Format(LOCTEXT("RuleMessage", "Applying verification rule \"{0}\"."), Rule.GetClass()->GetDisplayNameText()));
				Rule->Verify(Target, Report, Options);
			}
		}
		else
		{
			if (!FModuleManager::Get().IsModuleLoaded(TEXT("MetaHumanCharacterEditor")))
			{
				Report->AddError({LOCTEXT("ModuleNotLoaded", "Unable to load asset for verification. Please ensure the MetaHumanCharacter plugin is loaded in the plugin manager")});
			}
			else
			{
				Report->AddError({LOCTEXT("AssetNotLoadable", "Unable to load asset for verification. Please ensure all required plugins are loaded in the plugin manager")});
			}
		}

		UE::MetaHuman::AnalyticsEvent(TEXT("AssetGroupVerified"), {{TEXT("VerificationResult"), UEnum::GetDisplayValueAsText(Report->GetReportResult()).ToString()}});
	}

	return Report;
}

#undef LOCTEXT_NAMESPACE
