// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/CoreMiscDefines.h"

#if WITH_EDITORONLY_DATA
#include "MetasoundFrontendDocument.h"


// Forward Declartions
class IMetaSoundDocumentInterface;
struct FMetaSoundFrontendDocumentBuilder;


namespace Metasound::Frontend
{
	static FMetasoundFrontendVersionNumber GetMaxDocumentVersion()
	{
		return FMetasoundFrontendVersionNumber { 1, 16 };
	}

	// Version where page data was migrated from a singleton graph to
	// multiple paged graphs.  Some legacy behavior requires access to
	// this version to initialize a builder prior to versioning document
	// data.
	static FMetasoundFrontendVersionNumber GetPageMigrationVersion()
	{
		return FMetasoundFrontendVersionNumber { 1, 13 };
	}

	bool ChangeIDComparisonEnabledInAutoUpdate();

	// Versions Frontend Document. Passed as AssetBase for backward compat to
	// version asset documents predating the IMetaSoundDocumentInterface
	bool VersionDocument(FMetaSoundFrontendDocumentBuilder& Builder);
} // namespace Metasound::Frontend
#endif // WITH_EDITORONLY_DATA