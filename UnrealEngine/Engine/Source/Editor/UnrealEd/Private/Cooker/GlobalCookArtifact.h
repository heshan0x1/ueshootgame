// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cooker/CookArtifact.h"

class UCookOnTheFlyServer;

namespace UE::Cook
{

/**
 * CookArtifact that triggers a recook of all packages if any of the global settings that affect all
 * packages change. This artifact does not store files on disk other than its record of the global CookSettings.
 */
class FGlobalCookArtifact : public ICookArtifact
{
public:
	FGlobalCookArtifact(UCookOnTheFlyServer& InCOTFS);

	FString GetArtifactName() const override;
	FConfigFile CalculateCurrentSettings(ICookInfo& CookInfo, const ITargetPlatform* TargetPlatform) override;
	void CompareSettings(UE::Cook::Artifact::FCompareSettingsContext& Context) override;

private:
	UCookOnTheFlyServer& COTFS;
};


} // namespace UE::Cook