// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Experimental/BuildServerInterface.h"
#include "Experimental/ZenServerInterface.h"
#include "Internationalization/Regex.h"
#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/ITableRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "ZenBuildUtils.h"

#define UE_API STORAGESERVERWIDGETS_API

class FUProjectDictionary;
class SMultiSelectComboBox;

namespace UE::Zen::Build
{ 
	struct FListBuildsState;
}

DECLARE_DELEGATE_ThreeParams(FOnBuildTransferStarted, UE::Zen::Build::FBuildServiceInstance::FBuildTransfer /*Transfer*/,
	FStringView /*Name*/,
	FStringView /*Platform*/);

class SBuildSelection : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SBuildSelection)
		: _ZenServiceInstance(nullptr)
		, _BuildServiceInstance(nullptr)
	{ }

	SLATE_ATTRIBUTE(TSharedPtr<UE::Zen::FZenServiceInstance>, ZenServiceInstance);
	SLATE_ATTRIBUTE(TSharedPtr<UE::Zen::Build::FBuildServiceInstance>, BuildServiceInstance);

	SLATE_EVENT(FOnBuildTransferStarted, OnBuildTransferStarted)
	SLATE_END_ARGS()

	struct FBuildGroup : public TSharedFromThis<FBuildGroup, ESPMode::ThreadSafe>
	{
		FString Namespace;
		FString DisplayName;
		FString CommitIdentifier;
		FString Suffix;
		FDateTime CreatedAt;
		FString Category;
		FString Job;
		TMap<FString, UE::Zen::Build::FBuildServiceInstance::FBuildRecord> PerPlatformBuilds;
	};

	UE_API void Construct(const FArguments& InArgs);

private:
	enum class EBuildType
	{
		Oplog,
		StagedBuild,
		PackagedBuild,
		EditorPreCompiledBinary,
		EditorInstalledBuild,
		Unknown,
		Count
	};
	struct FKnownBuildType
	{
		EBuildType Type;
		FText UserText;
	};
	static TPair<FRegexPattern, FKnownBuildType> MakeKnownBuildTypePattern(const FStringView InPattern, EBuildType Type, FText&& InText);
	UE_API EBuildType GetSelectedBuildType() const;
	UE_API void RebuildLists();
	UE_API void RegenerateBuildGroups(UE::Zen::Build::FListBuildsState& ListBuildsState);
	UE_API void ConditionalSortBuildGroups();
	UE_API void RegenerateActivePlatformFilters();
	UE_API void ValidateBuildGroupSelection();
	UE_API TSharedRef<SWidget> OnGenerateTextBlockFromString(TSharedPtr<FString> Item);
	UE_API TSharedRef<SWidget> OnGenerateBuildTypeTextBlockFromString(TSharedPtr<FString> Item);
	UE_API bool BuildGroupIsSelectableOrNavigable(TSharedPtr<FBuildGroup> InItem) const;
	UE_API TSharedRef<ITableRow> GenerateBuildGroupRow(TSharedPtr<FBuildGroup> InItem, const TSharedRef<STableViewBase>& InOwningTable);
	UE_API TSharedPtr<SWidget> OnGetBuildGroupContextMenuContent() const;
	UE_API void OnOpenDestinationDirectoryClicked();
	UE_API TSharedRef<SWidget> GetBuildDestinationPanel();
	UE_API TSharedRef<SWidget> GetGridPanel();
	UE_API void BuildGroupSelectionChanged(TSharedPtr<FBuildGroup> Item, ESelectInfo::Type SelectInfo);
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode);

	FText ConvertBuildTypeToText(const FString& InBuildType);

	UE_API void SetUserSelectedDestination(const FStringView InDestination);
	UE_API FString GetUserSelectedDestination() const;
	UE_API FString GetDefaultDestination() const;
	UE_API FString GetEffectiveDestination() const;

	void SetSelectedStream(const TSharedPtr<FString> InSelectedStream);
	void SetSelectedProject(const TSharedPtr<FString> InSelectedProject);
	void SetSelectedBuildType(const TSharedPtr<FString> InBuildType);

	bool WriteSetting(FStringView InSectionName, FStringView InKeyName, FStringView InValue);
	bool WriteSetting(FStringView InKeyName, FStringView InValue);
	bool ReadSetting(FStringView InSectionName, FStringView InKeyName, FString& OutValue);
	bool ReadSetting(FStringView InKeyName, FString& OutValue);

	void SetUserSelectedProjectDictionaryRoot(FStringView InRoot);

	static UE_API FString SanitizeForPath(const FString& InString);
	static UE_API FString SanitizeForZenId(const FString& InString);
	static UE_API FString SanitizeBucketSegment(const FString& InString);

	static const TCHAR* LexToString(EBuildType BuildType);

	UE_API FReply ExploreDestination_OnClicked();

	static const FString EmptyString;

	TMap<FString, FString> EngineInstallations;

	TUniquePtr<FUProjectDictionary> FallbackProjectDictionary;

	FString UserSelectedProjectDictionaryRootDir;
	TUniquePtr<FUProjectDictionary> UserSelectedProjectDictionary;

	FString BuildUrl;
	FString BuildId;
	FString BuildVersion;
	FString BranchName;
	int EffectiveCompatibleChangelist = 0;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProjectWidget;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> StreamWidget;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> BuildTypeWidget;
	TSharedPtr<SMultiSelectComboBox> RequiredPlatformsWidget;

	TSharedPtr<SGridPanel> SelectedGroupPlatformGrid;
	TArray<FString> SelectedGroupSelectedPlatforms;

	TMultiMap<FString, FString> BucketsToNamespaces;

	TArray<TSharedPtr<FString>> ProjectList;
	TArray<TSharedPtr<FString>> StreamList;
	TArray<TSharedPtr<FString>> BuildTypeList;
	TArray<TSharedPtr<FString>> PlatformList;

	TArray<FString> ActivePlatformFilters;
	FString UserSelectedDestinations[(int)EBuildType::Count];
	bool bAppendBuildNameToDestinations[(int)EBuildType::Count];

	TSharedPtr<FString> SelectedStream;
	TSharedPtr<FString> SelectedProject;
	TSharedPtr<FString> SelectedBuildType;
	FString SelectedCommitFilter;

	TArray<TPair<FRegexPattern, FKnownBuildType>> KnownBuildTypePatterns;

	TSharedPtr<SListView<TSharedPtr<SBuildSelection::FBuildGroup, ESPMode::ThreadSafe>>> BuildGroupListView;
	FName BuildGroupSortByColumn;
	EColumnSortMode::Type BuildGroupSortMode;

	std::atomic<uint32> BuildListRefreshesInProgress = 0;
	std::atomic<uint32> BuildRefreshGeneration = 0;
	TArray<TSharedPtr<SBuildSelection::FBuildGroup, ESPMode::ThreadSafe>> BuildGroups;

	SVerticalBox::FSlot* GridSlot = nullptr;
	TAttribute<TSharedPtr<UE::Zen::FZenServiceInstance>> ZenServiceInstance;
	TAttribute<TSharedPtr<UE::Zen::Build::FBuildServiceInstance>> BuildServiceInstance;
	FOnBuildTransferStarted OnBuildTransferStarted;
};

typedef TSharedPtr<SBuildSelection::FBuildGroup, ESPMode::ThreadSafe> FBuildSelectionBuildGroupPtr;

class SBuildGroupTableRow : public SMultiColumnTableRow<FBuildSelectionBuildGroupPtr>
{
public:
	SLATE_BEGIN_ARGS(SBuildGroupTableRow) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const FBuildSelectionBuildGroupPtr InBuildGroup);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	const FSlateBrush* GetBorder() const;
	FReply OnBrowseClicked();

private:
	FBuildSelectionBuildGroupPtr BuildGroup;
};

#undef UE_API
