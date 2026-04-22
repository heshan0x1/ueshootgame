// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBuildSelection.h"

#include "DesktopPlatformModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformFileManager.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Math/UnitConversion.h"
#include "Misc/App.h"
#include "Misc/ExpressionParser.h"
#include "Misc/Paths.h"
#include "Misc/UProjectInfo.h"
#include "Modules/BuildVersion.h"
#include "SMultiSelectComboBox.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "ZenServiceInstanceManager.h"
#include <atomic>

#define LOCTEXT_NAMESPACE "StorageServerBuild"

const FString SBuildSelection::EmptyString;

namespace UE::BuildSelection::Internal
{
	namespace FBuildGroupIds
	{
		const FName ColName = TEXT("Name");
		const FName ColCommit = TEXT("Commit");
		const FName ColSuffix = TEXT("Suffix");
		const FName ColCategory = TEXT("Category");
		const FName ColCreated = TEXT("Created");
	}
}

TPair<FRegexPattern, SBuildSelection::FKnownBuildType> SBuildSelection::MakeKnownBuildTypePattern(const FStringView InPattern, EBuildType Type, FText&& InText)
{
	FString Pattern(InPattern);
	return TPair<FRegexPattern, FKnownBuildType>(Pattern, FKnownBuildType{Type, InText});
}

void SBuildSelection::Construct(const FArguments& InArgs)
{
	using namespace UE::BuildSelection::Internal;

	KnownBuildTypePatterns = {
		// These should be made more constinent and perhaps specified in INI
		MakeKnownBuildTypePattern(TEXT(".*oplog-?(.*)"), EBuildType::Oplog, LOCTEXT("BuildSelection_BuildType_Oplog", "Cooked Data") ),
		MakeKnownBuildTypePattern(TEXT(".*packaged-?build-?(.*)"), EBuildType::PackagedBuild, LOCTEXT("BuildSelection_BuildType_PackagedBuild", "Packaged Build")),
		MakeKnownBuildTypePattern(TEXT(".*staged-?(.*?)-?build.*"), EBuildType::StagedBuild, LOCTEXT("BuildSelection_BuildType_StagedBuild", "Staged Build")),
		MakeKnownBuildTypePattern(TEXT(".*ugs-?pcb-?(.*)"), EBuildType::EditorPreCompiledBinary, LOCTEXT("BuildSelection_BuildType_PreCompiledBinary", "Editor Pre-compiled Binary")),
		MakeKnownBuildTypePattern(TEXT(".*installed-?build-?(.*)"), EBuildType::EditorPreCompiledBinary, LOCTEXT("BuildSelection_BuildType_PreCompiledBinary", "Editor Pre-compiled Binary"))
	};

	FString UserSelectedEngineInstallation;
	if (ReadSetting(TEXT("UserSelectedEngineInstallation"), UserSelectedEngineInstallation))
	{
		if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
		{
			FString UserSelectedRootDir;
			if (DesktopPlatform->GetEngineRootDirFromIdentifier(UserSelectedEngineInstallation, UserSelectedRootDir))
			{
				SetUserSelectedProjectDictionaryRoot(UserSelectedRootDir);
			}
		}
	}

	for (int32 BuildTypeIndex = 0; BuildTypeIndex < (int32)EBuildType::Count; ++BuildTypeIndex)
	{
		ReadSetting(LexToString((EBuildType)BuildTypeIndex), TEXT("UserSelectedDestination"), UserSelectedDestinations[BuildTypeIndex]);
	}

	if (EngineInstallations.IsEmpty())
	{
		if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
		{
			DesktopPlatform->EnumerateEngineInstallations(EngineInstallations);
		}
	}

	for (const TPair<FString, FString>& EngineInstallation : EngineInstallations)
	{
		FallbackProjectDictionary = MakeUnique<FUProjectDictionary>(EngineInstallation.Value);
		break;
	}

	ZenServiceInstance = InArgs._ZenServiceInstance;
	BuildServiceInstance = InArgs._BuildServiceInstance;
	OnBuildTransferStarted = InArgs._OnBuildTransferStarted;

	BuildGroupSortByColumn = FBuildGroupIds::ColCreated;
	BuildGroupSortMode = EColumnSortMode::Descending;

	FBuildVersion CurrentBuildVersion;
	if (FBuildVersion::TryRead(FBuildVersion::GetDefaultFileName(), CurrentBuildVersion))
	{
		BuildUrl = CurrentBuildVersion.BuildUrl;
		BuildId = CurrentBuildVersion.BuildId;
		BuildVersion = CurrentBuildVersion.BuildVersion;
		BranchName = SanitizeBucketSegment(CurrentBuildVersion.BranchName);

		EffectiveCompatibleChangelist = CurrentBuildVersion.GetEffectiveCompatibleChangelist();
	}

	if (BranchName.IsEmpty())
	{
		ReadSetting(TEXT("Branch"), BranchName);
	}

	for (bool& bAppendBuildNameToDestination : bAppendBuildNameToDestinations)
	{
		bAppendBuildNameToDestination = false;
	}

	if (TSharedPtr<UE::Zen::Build::FBuildServiceInstance> ServiceInstance = BuildServiceInstance.Get())
	{
		ServiceInstance->OnRefreshNamespacesAndBucketsComplete().AddSP(this, &SBuildSelection::RebuildLists);
	}

	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(0, 0, 0, 0)
		.Expose(GridSlot)
		[
			GetGridPanel()
		]
	];
}

SBuildSelection::EBuildType SBuildSelection::GetSelectedBuildType() const
{
	if (!SelectedBuildType)
	{
		return EBuildType::Unknown;
	}
	for (const TPair<FRegexPattern, FKnownBuildType>& KnownBuildTypeItem : KnownBuildTypePatterns)
	{
		FRegexMatcher Matcher(KnownBuildTypeItem.Key, *SelectedBuildType);
		if (Matcher.FindNext())
		{
			return KnownBuildTypeItem.Value.Type;
		}
	}
	return EBuildType::Unknown;
}

void SBuildSelection::RebuildLists()
{
	using namespace UE::Zen::Build;
	using namespace UE::BuildSelection::Internal;

	BuildListRefreshesInProgress.fetch_add(1);
	BucketsToNamespaces.Empty();
	StreamList.Empty();
	ProjectList.Empty();
	BuildTypeList.Empty();
	PlatformList.Empty();

	if (TSharedPtr<FBuildServiceInstance> ServiceInstance = BuildServiceInstance.Get())
	{
		TArray<FString> Namespaces;
		TArray<FString> Projects;
		TArray<FString> Streams;
		TArray<FString> BuildTypes;
		TArray<FString> Platforms;
		TMultiMap<FString, FString> NamespacesAndBuckets = ServiceInstance->GetNamespacesAndBuckets();

		auto StringToSegmentViews = [](const FString& Str, TArray<FStringView>& OutViews)
			{
				FStringView WorkingStringView(Str);
				int32 CurrentIndex = 0;
				while (WorkingStringView.FindChar(TCHAR('.'), CurrentIndex))
				{
					if (CurrentIndex != 0)
					{
						OutViews.Add(WorkingStringView.Left(CurrentIndex));
					}
					WorkingStringView.RightChopInline(CurrentIndex + 1);
				}
				if (!WorkingStringView.IsEmpty())
				{
					OutViews.Add(WorkingStringView);
				}
			};

		NamespacesAndBuckets.GetKeys(Namespaces);

		auto ConvertToSharedPtrs = [](TArray<FString>& Strings, TArray<TSharedPtr<FString>>& SharedStrings)
			{
				Strings.StableSort();
				for (FString& String : Strings)
				{
					SharedStrings.Add(MakeShared<FString>(MoveTemp(String)));
				}
			};

		auto ConformSelection = [](TSharedPtr<FString>& SelectedItem, const TArray<TSharedPtr<FString>>& SelectionList)
			{
				if (!SelectedItem)
				{
					SelectedItem = SelectionList.IsEmpty() ? nullptr : SelectionList[0];
					return;
				}

				const TSharedPtr<FString>* FoundSelectionListItem = SelectionList.FindByPredicate([&SelectedItem](const TSharedPtr<FString>& Item)
					{
						return *Item == *SelectedItem;
					});

				SelectedItem = FoundSelectionListItem ? *FoundSelectionListItem : SelectionList.IsEmpty() ? nullptr : SelectionList[0];
			};

		const uint32 SegmentIndexProject = 0;
		const uint32 SegmentIndexBuildType = 1;
		const uint32 SegmentIndexStream = 2;
		const uint32 SegmentIndexPlatform = 3;
		const uint32 SegmentIndexNum = 4;

		// Stream list generation and selection conforming
		int32 DefaultStreamIndex = 0;
		TMultiMap<FStringView, TArray<FStringView>> NamespacesToBucketSegmentViews;
		for (const TPair<FString, FString>& NamespaceAndBucket : NamespacesAndBuckets)
		{
			BucketsToNamespaces.AddUnique(NamespaceAndBucket.Value, NamespaceAndBucket.Key);
			TArray<FStringView>& BucketSegmentViews = NamespacesToBucketSegmentViews.Add(NamespaceAndBucket.Key);
			StringToSegmentViews(NamespaceAndBucket.Value, BucketSegmentViews);

			if (BucketSegmentViews.Num() == SegmentIndexNum)
			{
				int32 CurrentIndex = Streams.AddUnique(FString(BucketSegmentViews[SegmentIndexStream]));
				if (!BranchName.IsEmpty() && (Streams[CurrentIndex] == BranchName))
				{
					DefaultStreamIndex = CurrentIndex;
				}
			}
		}
		FString DefaultStreamValue = Streams.Num() > 0 ? Streams[DefaultStreamIndex] : TEXT("");
		ConvertToSharedPtrs(Streams, StreamList);
		// Must search to find DefaultStreamIndex because it will have re-sorted in ConverToSharedPtrs
		const TSharedPtr<FString>* DefaultStreamListItem = StreamList.FindByPredicate([&DefaultStreamValue](const TSharedPtr<FString>& Item)
			{
				return *Item == DefaultStreamValue;
			});
		if (!SelectedStream && DefaultStreamListItem)
		{
			SetSelectedStream(*DefaultStreamListItem);
		}
		ConformSelection(SelectedStream, StreamList);

		// Project list generation and selection conforming
		for (const TPair<FStringView, TArray<FStringView>>& NamespaceToBucketSegmentViews : NamespacesToBucketSegmentViews)
		{
			const TArray<FStringView>& BucketSegmentViews = NamespaceToBucketSegmentViews.Value;
			if (BucketSegmentViews.Num() == SegmentIndexNum)
			{
				if (BucketSegmentViews[SegmentIndexStream] != (SelectedStream ? *SelectedStream : Streams[0]))
				{
					continue;
				}

				Projects.AddUnique(FString(BucketSegmentViews[SegmentIndexProject]));
			}
		}

		if (!SelectedProject)
		{
			if (FString PastProject; ReadSetting(TEXT("Project"), PastProject))
			{
				SelectedProject = MakeShared<FString>(MoveTemp(PastProject));
			}
		}
		ConvertToSharedPtrs(Projects, ProjectList);
		ConformSelection(SelectedProject, ProjectList);

		// BuildType list generation and selection conforming
		for (const TPair<FStringView, TArray<FStringView>>& NamespaceToBucketSegmentViews : NamespacesToBucketSegmentViews)
		{
			const TArray<FStringView>& BucketSegmentViews = NamespaceToBucketSegmentViews.Value;
			if (BucketSegmentViews.Num() == SegmentIndexNum)
			{
				if (BucketSegmentViews[SegmentIndexStream] != (SelectedStream ? *SelectedStream : Streams[0]))
				{
					continue;
				}

				if (BucketSegmentViews[SegmentIndexProject] != (SelectedProject ? *SelectedProject : Projects[0]))
				{
					continue;
				}

				BuildTypes.AddUnique(FString(BucketSegmentViews[SegmentIndexBuildType]));
			}
		}

		if (!SelectedBuildType)
		{
			if (FString PastBuildType; ReadSetting(TEXT("BuildType"), PastBuildType))
			{
				SelectedBuildType = MakeShared<FString>(MoveTemp(PastBuildType));
			}
		}
		ConvertToSharedPtrs(BuildTypes, BuildTypeList);
		ConformSelection(SelectedBuildType, BuildTypeList);

		// Platform list generation
		for (const TPair<FStringView, TArray<FStringView>>& NamespaceToBucketSegmentViews : NamespacesToBucketSegmentViews)
		{
			const TArray<FStringView>& BucketSegmentViews = NamespaceToBucketSegmentViews.Value;
			if (BucketSegmentViews.Num() == SegmentIndexNum)
			{
				if (BucketSegmentViews[SegmentIndexStream] != (SelectedStream ? *SelectedStream : Streams[0]))
				{
					continue;
				}

				if (BucketSegmentViews[SegmentIndexProject] != (SelectedProject ? *SelectedProject : Projects[0]))
				{
					continue;
				}

				if (BucketSegmentViews[SegmentIndexBuildType] != (SelectedBuildType ? *SelectedBuildType : BuildTypes[0]))
				{
					continue;
				}

				Platforms.AddUnique(FString(BucketSegmentViews[SegmentIndexPlatform]));
			}
		}
		ConvertToSharedPtrs(Platforms, PlatformList);
	}

	ExecuteOnGameThread(UE_SOURCE_LOCATION,
		[this]
		{
			StreamWidget->RefreshOptions();
			StreamWidget->SetSelectedItem(SelectedStream);
			ProjectWidget->RefreshOptions();
			ProjectWidget->SetSelectedItem(SelectedProject);
			BuildTypeWidget->RefreshOptions();
			BuildTypeWidget->SetSelectedItem(SelectedBuildType);
			RegenerateActivePlatformFilters();
		});

	if (TSharedPtr<FBuildServiceInstance> ServiceInstance = BuildServiceInstance.Get())
	{
		TArray<FNamespacePlatformBucketTuple> NamespacePlatformBucketTuples;
		for (TSharedPtr<FString> Platform : PlatformList)
		{
			FString Bucket = FString::Printf(TEXT("%s.%s.%s.%s"), **SelectedProject, **SelectedBuildType, **SelectedStream, **Platform);
			TArray<FString> NamespacesForBucket;
			BucketsToNamespaces.MultiFind(Bucket, NamespacesForBucket);
			for (FString& Namespace : NamespacesForBucket)
			{
				NamespacePlatformBucketTuples.Emplace(MoveTemp(Namespace), *Platform, Bucket);
			}
		}

		TSharedPtr<FListBuildsState> PendingQueryState = MakeShared<FListBuildsState>();
		PendingQueryState->PendingQueries = NamespacePlatformBucketTuples.Num();
		PendingQueryState->QueryState.SetNum(NamespacePlatformBucketTuples.Num());
		uint32 QueryIndex = 0;

		++BuildRefreshGeneration;
		BuildGroups.Empty();

		for (const FNamespacePlatformBucketTuple& NamespacePlatformBucket : NamespacePlatformBucketTuples)
		{
			ServiceInstance->ListBuilds(NamespacePlatformBucket.Namespace, NamespacePlatformBucket.Bucket,
			[this, QueryIndex, Namespace = NamespacePlatformBucket.Namespace, Platform = NamespacePlatformBucket.Platform,
				ExpectedBuildRefreshGeneration = BuildRefreshGeneration.load(), PendingQueryState]
			(TArray<FBuildServiceInstance::FBuildRecord>&& Results) mutable
				{
					FBuildState& NewBuildState = PendingQueryState->QueryState[QueryIndex];
					NewBuildState.Namespace = MoveTemp(Namespace);
					NewBuildState.Platform = MoveTemp(Platform);
					NewBuildState.Results = MoveTemp(Results);

					if (--PendingQueryState->PendingQueries == 0)
					{
						// All queries complete
						if (ExpectedBuildRefreshGeneration == BuildRefreshGeneration)
						{
							// Expected generation is the current generation
							RegenerateBuildGroups(*PendingQueryState);

							ExecuteOnGameThread(UE_SOURCE_LOCATION,
								[this]
								{
									BuildGroupListView->RequestListRefresh();
									BuildListRefreshesInProgress.fetch_sub(1);
								});
						}
						else
						{
							// Expected generation is not the current generation
							ExecuteOnGameThread(UE_SOURCE_LOCATION,
								[this]
								{
									BuildListRefreshesInProgress.fetch_sub(1);
								});
						}
					}
				});
			++QueryIndex;
		}

		if (NamespacePlatformBucketTuples.IsEmpty())
		{
			BuildListRefreshesInProgress.fetch_sub(1);
		}
	}
	else
	{
		BuildListRefreshesInProgress.fetch_sub(1);
	}
}

void SBuildSelection::RegenerateBuildGroups(UE::Zen::Build::FListBuildsState& ListBuildsState)
{
	using namespace UE::Zen::Build;
	using namespace UE::BuildSelection::Internal;

	BuildGroups.Empty();

	auto MakeGroupKey = [](const FString& Namespace, const FString& CommitIdentifier, const FBuildServiceInstance::FBuildRecord& BuildRecord)
	{
		// TODO: Append the configuration to the group key if present to workaround  issues with multiple uploads with the same build group but separate configuration
		// Should be removed once we improve the platform handling to be more flexible and thus include configurations
		FString Configuration;
		if (FCbFieldView ConfigurationField = BuildRecord.Metadata["Configuration"]; ConfigurationField.HasValue() && !ConfigurationField.HasError())
		{
			Configuration = FUTF8ToTCHAR(ConfigurationField.AsString());
		}

		if (FCbFieldView BuildGroupField = BuildRecord.Metadata["buildgroup"]; BuildGroupField.HasValue() && !BuildGroupField.HasError())
		{
			if (Configuration.IsEmpty()) 
			{
				return FString(WriteToString<64>(Namespace, ".", BuildGroupField.AsString()));
			}
			else
			{
				return FString(WriteToString<64>(Namespace, ".", BuildGroupField.AsString(), ".", Configuration));
			}
		}
		if (FCbFieldView NameField = BuildRecord.Metadata["name"]; NameField.HasValue() && !NameField.HasError())
		{
			return FString(WriteToString<64>(Namespace, ".", NameField.AsString()));
		}
		if (!CommitIdentifier.IsEmpty())
		{
			return FString(WriteToString<64>(Namespace, ".", CommitIdentifier));
		}
		return FString(WriteToString<64>(BuildRecord.BuildId));
	};

	TMap<FString, TSharedPtr<FBuildGroup>> KeyedGroups;
	for (FBuildState& BuildState : ListBuildsState.QueryState)
	{
		for (FBuildServiceInstance::FBuildRecord& BuildRecord : BuildState.Results)
		{
			FString CommitIdentifier = BuildRecord.GetCommitIdentifier();

			FString GroupKey = MakeGroupKey(BuildState.Namespace, CommitIdentifier, BuildRecord);
			TSharedPtr<FBuildGroup>& BuildGroup = KeyedGroups.FindOrAdd(GroupKey);
			if (!BuildGroup)
			{
				BuildGroup = MakeShared<FBuildGroup>();
			}

			// TODO: Temporary detection of bulk builds to work around issue were a build can have multiple versions of the same platform
			bool IsBulkBuild = false;
			if (FCbFieldView NameView = BuildRecord.Metadata["name"]; NameView.HasValue() && !NameView.HasError())
			{
				FString Name;
				Name = FUTF8ToTCHAR(NameView.AsString());
				int32 BulkStartIndex = Name.Find(TEXT("-Bulk"));
				if (BulkStartIndex != INDEX_NONE)
				{
					IsBulkBuild = true;
				}
			}

			if (IsBulkBuild)
			{
				 continue;
			}
			
			if (BuildGroup->DisplayName.IsEmpty())
			{
				FString Job;
				if (FCbFieldView JobField = BuildRecord.Metadata["job"]; JobField.HasValue() && !JobField.HasError())
				{
					Job = FUTF8ToTCHAR(JobField.AsString());
				}

				FString Category;
				if (FCbFieldView TemplateIdField = BuildRecord.Metadata["hordeTemplateId"]; TemplateIdField.HasValue() && !TemplateIdField.HasError())
				{
					Category = FUTF8ToTCHAR(TemplateIdField.AsString());
				}

				bool IsPreflight = false;
				if (FCbFieldView IsPreflightField = BuildRecord.Metadata["ispreflight"]; IsPreflightField.HasValue() && !IsPreflightField.HasError())
				{
					FString IsPreflightString;
					IsPreflightString = FUTF8ToTCHAR(IsPreflightField.AsString());
					IsPreflight = IsPreflightString.ToBool();;
				}

				FString Suffix("");
				if (IsPreflight)
				{
					Suffix.Append("PF");
				}

				FDateTime CreatedAt;
				if (FCbFieldView CreatedAtField = BuildRecord.Metadata["createdAt"]; CreatedAtField.HasValue() && !CreatedAtField.HasError())
				{
					if (CreatedAtField.IsString())
					{
						FDateTime::ParseIso8601(FUTF8ToTCHAR(CreatedAtField.AsString()).Get(), CreatedAt);
					}
					else if (CreatedAtField.IsDateTime())
					{
						CreatedAt = CreatedAtField.AsDateTime();
					}
				}

				FString Configuration;
				if (FCbFieldView ConfigurationField = BuildRecord.Metadata["Configuration"]; ConfigurationField.HasValue() && !ConfigurationField.HasError())
				{
					Configuration = FUTF8ToTCHAR(ConfigurationField.AsString());
				}

				FString ItemName;
				FCbFieldView GroupNameView = BuildRecord.Metadata["buildgroup"];
				if (!GroupNameView.HasValue())
				{
					GroupNameView = BuildRecord.Metadata["name"];
				}
				if (FCbFieldView NameView = GroupNameView; NameView.HasValue() && !NameView.HasError())
				{
					// TODO: This name manipulation needs to be removed when the metadata is more consistent.
					ItemName = FUTF8ToTCHAR(NameView.AsString());
					int32 CLStartIndex = ItemName.Find(TEXT("-CL"));
					if (CLStartIndex != INDEX_NONE)
					{
						int32 TruncationIndex = ItemName.Find(TEXT("-"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CLStartIndex + 4);
						if (TruncationIndex != INDEX_NONE)
						{
							ItemName.LeftInline(TruncationIndex);
						}
						TruncationIndex = ItemName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, CLStartIndex + 4);
						if (TruncationIndex != INDEX_NONE)
						{
							ItemName.LeftInline(TruncationIndex);
						}
					}

					if (!Category.IsEmpty() && ItemName.StartsWith(Category))
					{
						ItemName.RightChopInline(Category.Len());
					}

					// TODO: Configuration appending is temporary until we have improved the platform grouping to include configuration
					if (!Configuration.IsEmpty() && !ItemName.EndsWith(Configuration))
					{
						// append the configuration to the item name if present
						ItemName = *WriteToString<64>(ItemName, "-", Configuration);
					}
					bool bCharRemoved = false;
					do
					{
						ItemName.TrimCharInline(TCHAR('.'), &bCharRemoved);
					}
					while(bCharRemoved);
					do
					{
						ItemName.TrimCharInline(TCHAR('+'), &bCharRemoved);
					}
					while(bCharRemoved);

					ItemName.ReplaceCharInline(TCHAR('+'), TCHAR('-'));
				}
				else
				{
					ItemName = *WriteToString<64>(BuildRecord.BuildId);
				}

				BuildGroup->Namespace = BuildState.Namespace;
				BuildGroup->DisplayName = ItemName;
				BuildGroup->CommitIdentifier = CommitIdentifier;
				BuildGroup->Suffix = Suffix;
				BuildGroup->Category = Category;
				BuildGroup->CreatedAt = CreatedAt;
				BuildGroup->Job = Job;
			}

			BuildGroup->PerPlatformBuilds.FindOrAdd(BuildState.Platform, MoveTemp(BuildRecord));
		}
	}

	KeyedGroups.GenerateValueArray(BuildGroups);
	ConditionalSortBuildGroups();
}

void SBuildSelection::ConditionalSortBuildGroups()
{
	using namespace UE::BuildSelection::Internal;

	if (BuildGroupSortMode == EColumnSortMode::None)
	{
		return;
	}

	// Sorting
	if (BuildGroupSortMode == EColumnSortMode::Ascending)
	{
		if (BuildGroupSortByColumn == FBuildGroupIds::ColName)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->DisplayName < B->DisplayName);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColCommit)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->CommitIdentifier < B->CommitIdentifier);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColSuffix)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->Suffix < B->Suffix);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColCategory)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->Category < B->Category);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColCreated)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->CreatedAt < B->CreatedAt);
			});
		}
	}
	else
	{
		if (BuildGroupSortByColumn == FBuildGroupIds::ColName)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->DisplayName >= B->DisplayName);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColCommit)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->CommitIdentifier >= B->CommitIdentifier);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColSuffix)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->Suffix >= B->Suffix);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColCategory)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->Category >= B->Category);
			});
		}
		else if (BuildGroupSortByColumn == FBuildGroupIds::ColCreated)
		{
			BuildGroups.Sort([this](const TSharedPtr<FBuildGroup>& A, const TSharedPtr<FBuildGroup>& B) {
				return (A->CreatedAt >= B->CreatedAt);
			});
		}
	}
}

void SBuildSelection::RegenerateActivePlatformFilters()
{
	ActivePlatformFilters.Empty();
	for (TSharedPtr<FString> Platform : PlatformList)
	{
		if (Platform && RequiredPlatformsWidget)
		{
			if (RequiredPlatformsWidget->IsChecked(*Platform))
			{
				ActivePlatformFilters.Add(*Platform);
				SelectedGroupSelectedPlatforms.AddUnique(*Platform);
			}
			else
			{
				SelectedGroupSelectedPlatforms.Remove(*Platform);
			}
		}
	}
}

void SBuildSelection::ValidateBuildGroupSelection()
{
	BuildGroupListView->UpdateSelectionSet();
	TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return;
	}
	for (const FBuildSelectionBuildGroupPtr& SelectedItem : SelectedItems)
	{
		if (!BuildGroupIsSelectableOrNavigable(SelectedItem))
		{
			BuildGroupListView->SetItemSelection(SelectedItem, false);
		}
	}
}

TSharedRef<SWidget> SBuildSelection::OnGenerateTextBlockFromString(TSharedPtr<FString> Item)
{
	return SNew(STextBlock)
		.Text(FText::FromString(*Item));
}

TSharedRef<SWidget> SBuildSelection::OnGenerateBuildTypeTextBlockFromString(TSharedPtr<FString> Item)
{
	return SNew(STextBlock)
		.Text(Item ? ConvertBuildTypeToText(*Item) : FText::GetEmpty());
}

bool SBuildSelection::BuildGroupIsSelectableOrNavigable(FBuildSelectionBuildGroupPtr InItem) const
{
	if (!InItem)
	{
		return false;
	}

	for (const FString& ActivePlatformFilter : ActivePlatformFilters)
	{
		if (!InItem->PerPlatformBuilds.Contains(ActivePlatformFilter))
		{
			return false;
		}
	}

	if (!SelectedCommitFilter.IsEmpty() && !InItem->CommitIdentifier.Contains(SelectedCommitFilter))
	{
		return false;
	}
	return true;
}

TSharedRef<ITableRow> SBuildSelection::GenerateBuildGroupRow(FBuildSelectionBuildGroupPtr InItem, const TSharedRef<STableViewBase>& InOwningTable)
{
	return SNew(SBuildGroupTableRow, InOwningTable, InItem)
		.Visibility_Lambda([this, InItem]()
		{
			for (const FString& ActivePlatformFilter : ActivePlatformFilters)
			{
				if (!InItem->PerPlatformBuilds.Contains(ActivePlatformFilter))
				{
					return EVisibility::Collapsed;
				}
			}

			if (!SelectedCommitFilter.IsEmpty() && !InItem->CommitIdentifier.Contains(SelectedCommitFilter))
			{
				return EVisibility::Collapsed;
			}
			return EVisibility::Visible;
		});
}

TSharedPtr<SWidget> SBuildSelection::OnGetBuildGroupContextMenuContent() const
{
	TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();

	if (SelectedItems.IsEmpty())
	{
		return nullptr;
	}

	const bool bCloseAfterSelection = true;
	const bool bCloseSelfOnly = false;
	const bool bSearchable = false;
	const bool bRecursivelySearchable = false;
	FMenuBuilder MenuBuilder(bCloseAfterSelection,
		nullptr,
		TSharedPtr<FExtender>(),
		bCloseSelfOnly,
		&FCoreStyle::Get(),
		bSearchable,
		NAME_None,
		bRecursivelySearchable);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BuildSelection_CopyCommit", "Copy commit"),
		LOCTEXT("BuildSelection_CopyCommit_ToolTip", "Copies the commit or changelist number"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]
				{
					TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();
					if (SelectedItems.Num() == 1)
					{
						FPlatformApplicationMisc::ClipboardCopy(*SelectedItems[0]->CommitIdentifier);
					}
				})
		),
		NAME_None,
		EUserInterfaceActionType::Button
	);


	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SBuildSelection::BuildGroupSelectionChanged(FBuildSelectionBuildGroupPtr Item, ESelectInfo::Type SelectInfo)
{
	using namespace UE::Zen::Build;

	if (!SelectedGroupPlatformGrid)
	{
		return;
	}

	SelectedGroupPlatformGrid->ClearChildren();

	if (!Item)
	{
		return;
	}

	int32 Row = 0;
	int32 Column = 0;

	for (const TSharedPtr<FString>& Platform : PlatformList)
	{
		TSharedPtr<SCheckBox> CurrentCheckbox;
		SelectedGroupPlatformGrid->AddSlot(Column, Row)
			.Padding(0,0,0,2)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Lambda([this, Item, Platform]
				{
					return Item->PerPlatformBuilds.Contains(*Platform);
				})
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(CurrentCheckbox, SCheckBox)
					.IsChecked_Lambda([this, Item, Platform]
					{
						bool bIsChecked = Item->PerPlatformBuilds.Contains(*Platform) && SelectedGroupSelectedPlatforms.Contains(*Platform);
						return bIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, Platform](ECheckBoxState InNewState)
					{
						if (InNewState == ECheckBoxState::Checked)
						{
							SelectedGroupSelectedPlatforms.AddUnique(*Platform);
						}
						else if (InNewState == ECheckBoxState::Unchecked)
						{
							SelectedGroupSelectedPlatforms.Remove(*Platform);
						}
					})
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "InvisibleButton")
					.IsFocusable(false)

					.OnClicked_Lambda([this, CurrentCheckbox, Platform]()
						{
							ECheckBoxState NewState = ECheckBoxState::Checked;
							if (CurrentCheckbox->IsChecked())
							{
								NewState = ECheckBoxState::Unchecked;
							}
							CurrentCheckbox->SetIsChecked(NewState);
							if (NewState == ECheckBoxState::Checked)
							{
								SelectedGroupSelectedPlatforms.AddUnique(*Platform);
							}
							else if (NewState == ECheckBoxState::Unchecked)
							{
								SelectedGroupSelectedPlatforms.Remove(*Platform);
							}
							return FReply::Handled();
						})
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Left)
						.Text(FText::FromString(*Platform))
					]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ToolTipText(LOCTEXT("BuildSelection_BrowseToBuildUrl", "Browse to build url"))
					.IsEnabled_Lambda([this, Item, Platform]
					{
						if (FBuildServiceInstance::FBuildRecord* BuildRecord = Item->PerPlatformBuilds.Find(*Platform))
						{
							if (FCbFieldView BuildUrlField = BuildRecord->Metadata["buildurl"]; BuildUrlField.HasValue() && !BuildUrlField.HasError())
							{
								return true;
							}
						}
						return false;
					})
					.OnClicked_Lambda([this, Item, Platform]
					{
						if (FBuildServiceInstance::FBuildRecord* BuildRecord = Item->PerPlatformBuilds.Find(*Platform))
						{
							if (FCbFieldView BuildUrlField = BuildRecord->Metadata["buildurl"]; BuildUrlField.HasValue() && !BuildUrlField.HasError())
							{
								FString Url(FUTF8ToTCHAR(BuildUrlField.AsString()));
								FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
								return FReply::Handled();
							}
						}
						return FReply::Unhandled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Zen.BrowserView"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			];

		if (++Row > 3)
		{
			Column++;
			Row = 0;
		}
	}
}

EColumnSortMode::Type SBuildSelection::GetColumnSortMode(const FName ColumnId) const
{
	if (BuildGroupSortByColumn == ColumnId)
	{
		return BuildGroupSortMode;
	}

	return EColumnSortMode::None;
}

void SBuildSelection::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	BuildGroupSortByColumn = ColumnId;
	BuildGroupSortMode = InSortMode;

	ConditionalSortBuildGroups();
	ExecuteOnGameThread(UE_SOURCE_LOCATION,
		[this]
		{
			BuildGroupListView->RequestListRefresh();
		});
}

FText SBuildSelection::ConvertBuildTypeToText(const FString& InBuildType)
{
	for (const TPair<FRegexPattern, FKnownBuildType>& KnownBuildTypeItem : KnownBuildTypePatterns)
	{
		FRegexMatcher Matcher(KnownBuildTypeItem.Key, InBuildType);
		if (Matcher.FindNext())
		{
			FString OptionalCaptureGroupString = Matcher.GetCaptureGroup(1);
			if (OptionalCaptureGroupString.IsEmpty())
			{
				return KnownBuildTypeItem.Value.UserText;
			}
			return FText::Format(LOCTEXT("BuildSelection_KnownBuildTypeWithCaptureGroupFormat", "{0} ({1})"), KnownBuildTypeItem.Value.UserText, FText::FromString(OptionalCaptureGroupString));
		}
	}

	return FText::FromString(InBuildType);
}

void SBuildSelection::SetUserSelectedDestination(const FStringView InDestination)
{
	EBuildType BuildType = GetSelectedBuildType();
	int BuildTypeIndex = (int)BuildType;
	UserSelectedDestinations[BuildTypeIndex] = InDestination;

	if (BuildType != EBuildType::Oplog)
	{
		if (EngineInstallations.IsEmpty())
		{
			if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
			{
				DesktopPlatform->EnumerateEngineInstallations(EngineInstallations);
			}
		}

		for (const TPair<FString, FString>& EngineInstallation : EngineInstallations)
		{
			if (FPaths::IsUnderDirectory(UserSelectedDestinations[BuildTypeIndex], EngineInstallation.Value))
			{
				WriteSetting(TEXT("UserSelectedEngineInstallation"), EngineInstallation.Key);
				SetUserSelectedProjectDictionaryRoot(EngineInstallation.Value);
				break;
			}
		}
	}

	WriteSetting(LexToString(BuildType), TEXT("UserSelectedDestination"), InDestination);
}

FString SBuildSelection::GetUserSelectedDestination() const
{
	EBuildType BuildType = GetSelectedBuildType();
	int BuildTypeIndex = (int)BuildType;
	return BuildType == EBuildType::Oplog ? SanitizeForZenId(UserSelectedDestinations[BuildTypeIndex]) : SanitizeForPath(UserSelectedDestinations[BuildTypeIndex]);
}

FString SBuildSelection::GetDefaultDestination() const
{
	EBuildType BuildType = GetSelectedBuildType();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	FString ProjectFilename = FUProjectDictionary::GetDefault().GetProjectPathForGame(**SelectedProject);

	if (ProjectFilename.IsEmpty() && UserSelectedProjectDictionary)
	{
		ProjectFilename = UserSelectedProjectDictionary->GetProjectPathForGame(**SelectedProject);
	}

	if (ProjectFilename.IsEmpty())
	{
		if (BuildType == EBuildType::Oplog)
		{
			return *SelectedProject;
		}

		return PlatformFile.ConvertToAbsolutePathForExternalAppForRead(
				*FPaths::FPaths::Combine(FPaths::EngineSavedDir(), TEXT("DownloadedBuilds"))
			);
	}

	if (ProjectFilename.IsEmpty() && FallbackProjectDictionary)
	{
		ProjectFilename = FallbackProjectDictionary->GetProjectPathForGame(**SelectedProject);
	}

	switch (BuildType)
	{
	case EBuildType::StagedBuild:
		return PlatformFile.ConvertToAbsolutePathForExternalAppForRead(
				*FPaths::Combine(FPaths::GetPath(ProjectFilename), TEXT("Saved"), TEXT("StagedBuilds"))
			);
	case EBuildType::PackagedBuild:
		return PlatformFile.ConvertToAbsolutePathForExternalAppForRead(
				*FPaths::Combine(FPaths::GetPath(ProjectFilename), TEXT("Saved"), TEXT("Packages"))
			);
	case EBuildType::Oplog:
		return FApp::GetZenStoreProjectIdForProject(ProjectFilename);
	}
	return PlatformFile.ConvertToAbsolutePathForExternalAppForRead(
				*FPaths::Combine(FPaths::GetPath(ProjectFilename), TEXT("Saved"), TEXT("DownloadedBuilds"))
			);
}

FString SBuildSelection::GetEffectiveDestination() const
{
	FString UserSelectedDestination = GetUserSelectedDestination();
	return UserSelectedDestination.IsEmpty() ? GetDefaultDestination() : UserSelectedDestination;
}

void SBuildSelection::SetSelectedStream(const TSharedPtr<FString> InSelectedStream)
{
	SelectedStream = InSelectedStream;
	WriteSetting(TEXT("Branch"), *SelectedStream);
}

void SBuildSelection::SetSelectedProject(const TSharedPtr<FString> InSelectedProject)
{
	SelectedProject = InSelectedProject;
	WriteSetting(TEXT("Project"), *SelectedProject);
}

void SBuildSelection::SetSelectedBuildType(const TSharedPtr<FString> InBuildType)
{
	SelectedBuildType = InBuildType;
	WriteSetting(TEXT("BuildType"), *SelectedBuildType);
}

bool SBuildSelection::WriteSetting(const FStringView InSectionName, const FStringView InKeyName, const FStringView InValue)
{
	static const FString StoreId = TEXT("Epic Games");
	static const FString SectionName = TEXT("Unreal Engine/Build Storage");
	const FString FinalSectionName = InSectionName.IsEmpty() ? SectionName : FPaths::Combine(SectionName, InSectionName);
	const FString FinalKeyName(InKeyName);
	FString Value(InValue);
	return FPlatformMisc::SetStoredValue(TEXT("Epic Games"), FinalSectionName, FinalKeyName, Value);
}

bool SBuildSelection::WriteSetting(const FStringView InKeyName, const FStringView InValue)
{
	return WriteSetting(EmptyString, InKeyName, InValue);
}

bool SBuildSelection::ReadSetting(const FStringView InSectionName, const FStringView InKeyName, FString& OutValue)
{
	static const FString StoreId = TEXT("Epic Games");
	static const FString SectionName = TEXT("Unreal Engine/Build Storage");
	const FString FinalSectionName = InSectionName.IsEmpty() ? SectionName : FPaths::Combine(SectionName, InSectionName);
	const FString FinalKeyName(InKeyName);
	return FPlatformMisc::GetStoredValue(TEXT("Epic Games"), FinalSectionName, FinalKeyName, OutValue);
}

bool SBuildSelection::ReadSetting(const FStringView InKeyName, FString& OutValue)
{
	return ReadSetting(EmptyString, InKeyName, OutValue);
}

void SBuildSelection::SetUserSelectedProjectDictionaryRoot(const FStringView InRoot)
{
	if (InRoot.IsEmpty())
	{
		UserSelectedProjectDictionaryRootDir.Empty();
		UserSelectedProjectDictionary.Reset();
	}
	FString Root(InRoot);
	FString FullRoot = FPaths::ConvertRelativePathToFull(Root);
	if (!FPaths::IsSamePath(FullRoot, UserSelectedProjectDictionaryRootDir))
	{
		UserSelectedProjectDictionaryRootDir = FullRoot;
		UserSelectedProjectDictionary = MakeUnique<FUProjectDictionary>(UserSelectedProjectDictionaryRootDir);
	}
}

FString SBuildSelection::SanitizeForPath(const FString& InString)
{
	// TODO: Had to remove path sanitization as the engine only provides validation
	return InString;
}

FString SBuildSelection::SanitizeForZenId(const FString& InString)
{
	FString OutString = InString;
	for (int32 Index = 0; Index < OutString.Len(); ++Index)
	{
		if (!FChar::IsIdentifier(OutString[Index]) && OutString[Index] != TCHAR('.'))
		{
			OutString[Index] = TCHAR('_');
		}
	}
	return OutString;
}

FString SBuildSelection::SanitizeBucketSegment(const FString& InString)
{
	TStringBuilder<64> OutputBuilder;
	for (int32 CharIndex = 0; CharIndex < InString.Len(); ++CharIndex)
	{
		TCHAR Character = FChar::ToLower(InString[CharIndex]);

		if (Character == TCHAR('.'))
		{
			if (OutputBuilder.Len() > 0)
			{
				if (OutputBuilder.LastChar() == TCHAR('-'))
				{
					OutputBuilder.RemoveSuffix(1);
				}
				OutputBuilder.AppendChar(Character);
			}
		}
		else if (FChar::IsIdentifier(Character))
		{
			OutputBuilder.AppendChar(Character);
		}
		else if (OutputBuilder.Len() > 0 &&
			OutputBuilder.LastChar() != TCHAR('-'))
		{
			OutputBuilder.AppendChar(TCHAR('-'));
		}
	}

	FString OutString = OutputBuilder.ToString();

	// Trim leading and trailing dashes
	bool bCharsWereRemoved;
	do
	{
		OutString.TrimCharInline(TEXT('-'), &bCharsWereRemoved);
	} while (bCharsWereRemoved);

	return OutString;
}

const TCHAR* SBuildSelection::LexToString(EBuildType BuildType)
{
	static const TCHAR* Strings[] =
	{
		TEXT("Oplog"),
		TEXT("StagedBuild"),
		TEXT("PackagedBuild"),
		TEXT("EditorPreCompiledBinary"),
		TEXT("EditorInstalledBuild"),
		TEXT("Unknown")
	};
	static_assert((int32)(EBuildType::Count) == UE_ARRAY_COUNT(Strings), "SBuildSelection::LexToString must contain a string for each member of EBuildType");

	return Strings[(int32)BuildType];
}

FReply SBuildSelection::ExploreDestination_OnClicked()
{
	if (GetSelectedBuildType() == EBuildType::Oplog)
	{
		UE::Zen::FZenLocalServiceRunContext RunContext;
		uint16 LocalPort = 8558;
		if (UE::Zen::TryGetLocalServiceRunContext(RunContext))
		{
			if (!UE::Zen::IsLocalServiceRunning(*RunContext.GetDataPath(), &LocalPort))
			{
				UE::Zen::StartLocalService(RunContext);
				UE::Zen::IsLocalServiceRunning(*RunContext.GetDataPath(), &LocalPort);
			}
		}
		FPlatformProcess::LaunchURL(*FString::Printf(TEXT("http://localhost:%d/dashboard/?page=project&project=%s"), LocalPort, *GetEffectiveDestination()), nullptr, nullptr);
	}
	else
	{
		FPlatformProcess::ExploreFolder(*GetEffectiveDestination());
	}
	return FReply::Handled();
}

void SBuildSelection::OnOpenDestinationDirectoryClicked()
{
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		const FString Title = LOCTEXT("BuildSelection_DestinationDirectoryBrowserTitle", "Choose destination directory").ToString();

		FString NewDestination = GetUserSelectedDestination();
		if (DesktopPlatform->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			Title,
			GetEffectiveDestination(),
			NewDestination))
		{
			SetUserSelectedDestination(NewDestination);
		}
	}
}

TSharedRef<SWidget> SBuildSelection::GetBuildDestinationPanel()
{
	TSharedPtr<SCheckBox> AppendBuildNameCheckbox;

	return
	SNew(SVerticalBox)

	+ SVerticalBox::Slot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([this]()
		{
			TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();
			return (SelectedItems.Num() == 0) || GetSelectedBuildType() == EBuildType::Oplog ? EVisibility::Collapsed : EVisibility::Visible;
		})
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.OverflowPolicy(ETextOverflowPolicy::MiddleEllipsis)
			.MinDesiredWidth(200.0f)
			.HintText_Lambda([this]()
			{
				return FText::FromString(GetDefaultDestination());
			})
			.Text_Lambda([this]()
			{
				return FText::FromString(GetUserSelectedDestination());
			})
			.OnTextCommitted_Lambda([this](const FText& Text, const ETextCommit::Type CommitType)
			{
				SetUserSelectedDestination(Text.ToString());
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				OnOpenDestinationDirectoryClicked();
				return FReply::Handled();
			})
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Zen.BrowseContent"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([this]()
		{
			TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();
			return (SelectedItems.Num() == 0) || GetSelectedBuildType() != EBuildType::Oplog ? EVisibility::Collapsed : EVisibility::Visible;
		})
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.OverflowPolicy(ETextOverflowPolicy::MiddleEllipsis)
			.MinDesiredWidth(200.0f)
			.HintText_Lambda([this]()
			{
				return FText::FromString(GetDefaultDestination());
			})
			.Text_Lambda([this]()
			{
				return FText::FromString(GetUserSelectedDestination());
			})
			.OnTextCommitted_Lambda([this](const FText& Text, const ETextCommit::Type CommitType)
			{
				SetUserSelectedDestination(Text.ToString());
			})
		]
	]

	+ SVerticalBox::Slot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(AppendBuildNameCheckbox, SCheckBox)
			.IsChecked_Lambda([this]
			{
				EBuildType BuildType = GetSelectedBuildType();
				int BuildTypeIndex = (int)BuildType;
				return bAppendBuildNameToDestinations[BuildTypeIndex] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState InNewState)
			{
				EBuildType BuildType = GetSelectedBuildType();
				int BuildTypeIndex = (int)BuildType;
				if (InNewState == ECheckBoxState::Checked)
				{
					bAppendBuildNameToDestinations[BuildTypeIndex] = true;
				}
				else if (InNewState == ECheckBoxState::Unchecked)
				{
					bAppendBuildNameToDestinations[BuildTypeIndex] = false;
				}
			})
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "InvisibleButton")
			.IsFocusable(false)
			.OnClicked_Lambda([this, AppendBuildNameCheckbox]()
				{
					EBuildType BuildType = GetSelectedBuildType();
					int BuildTypeIndex = (int)BuildType;
					ECheckBoxState NewState = ECheckBoxState::Checked;
					if (AppendBuildNameCheckbox->IsChecked())
					{
						NewState = ECheckBoxState::Unchecked;
					}
					AppendBuildNameCheckbox->SetIsChecked(NewState);
					if (NewState == ECheckBoxState::Checked)
					{
						bAppendBuildNameToDestinations[BuildTypeIndex] = true;
					}
					else if (NewState == ECheckBoxState::Unchecked)
					{
						bAppendBuildNameToDestinations[BuildTypeIndex] = false;
					}
					return FReply::Handled();
				})
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Left)
				.Text(LOCTEXT("BuildSelection_DestinationAppendBuildName", "Append build name"))
			]
		]
	];
}

TSharedRef<SWidget> SBuildSelection::GetGridPanel()
{
	using namespace UE::BuildSelection::Internal;

	TSharedRef<SVerticalBox> Panel =
	SNew(SVerticalBox)
	.IsEnabled_Lambda([this]
	{
		if (TSharedPtr<UE::Zen::Build::FBuildServiceInstance> ServiceInstance = BuildServiceInstance.Get())
		{
			return ServiceInstance->GetConnectionState() == UE::Zen::Build::FBuildServiceInstance::EConnectionState::ConnectionSucceeded && !ServiceInstance->GetNamespacesAndBuckets().IsEmpty();
		}
		return false;
	});

	const float MinDesiredWidth = 50.0f;

	const float RowMargin = 2.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	Panel->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Top)
	[
		SNew(SHorizontalBox)

		// Stream
		+SHorizontalBox::Slot()
		.AutoWidth()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Text(LOCTEXT("BuildSelection_Stream", "Stream"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SAssignNew(StreamWidget, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&StreamList)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
				{
					if (Item.IsValid() && *Item != *SelectedStream)
					{
						SetSelectedStream(Item);
						RebuildLists();
					}
				})
				.OnGenerateWidget(this, &SBuildSelection::OnGenerateTextBlockFromString)
				[
					SNew(STextBlock)
						.MinDesiredWidth(MinDesiredWidth)
						.Text_Lambda([this]()
						{
							return FText::FromString(SelectedStream ? **SelectedStream : TEXT(""));
						})
				]
			]
		]

		// Project
		+SHorizontalBox::Slot()
		.AutoWidth()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Text(LOCTEXT("BuildSelection_Project", "Project"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SAssignNew(ProjectWidget, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&ProjectList)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
				{
					if (Item.IsValid() && *Item != *SelectedProject)
					{
						SetSelectedProject(Item);
						RebuildLists();
					}
				})
				.OnGenerateWidget(this, &SBuildSelection::OnGenerateTextBlockFromString)
				[
					SNew(STextBlock)
						.MinDesiredWidth(MinDesiredWidth)
						.Text_Lambda([this]()
						{
							return FText::FromString(SelectedProject ? **SelectedProject : TEXT(""));
						})
				]
			]
		]

		// Build Type
		+SHorizontalBox::Slot()
		.AutoWidth()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Text(LOCTEXT("BuildSelection_BuildType", "Build Type"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SAssignNew(BuildTypeWidget, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&BuildTypeList)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
				{
					if (Item.IsValid() && *Item != *SelectedBuildType)
					{
						SetSelectedBuildType(Item);
						RebuildLists();
					}
				})
				.OnGenerateWidget(this, &SBuildSelection::OnGenerateBuildTypeTextBlockFromString)
				[
					SNew(STextBlock)
						.MinDesiredWidth(MinDesiredWidth)
						.Text_Lambda([this]()
						{
							if (!SelectedBuildType)
							{
								return FText::GetEmpty();
							}
							return ConvertBuildTypeToText(*SelectedBuildType);
						})
				]
			]
		]

		// Commit
		+SHorizontalBox::Slot()
		.AutoWidth()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Text(LOCTEXT("BuildSelection_Commit", "Commit"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SEditableTextBox)
				.OverflowPolicy(ETextOverflowPolicy::MiddleEllipsis)
				.HintText_Lambda([this]()
				{
					return LOCTEXT("BuildSelection_AnyCommit", "Any");
				})
				.Text_Lambda([this]()
				{
					return FText::FromString(SelectedCommitFilter);
				})
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					SelectedCommitFilter = Text.ToString();
				})
				.OnTextCommitted_Lambda([this](const FText& Text, const ETextCommit::Type CommitType)
				{
					SelectedCommitFilter = Text.ToString();
				})
			]
		]

		// Required Platforms
		+SHorizontalBox::Slot()
		.AutoWidth()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Text(LOCTEXT("BuildSelection_RequiredPlatforms", "Required Platforms"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(ColumnMargin, RowMargin))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SAssignNew(RequiredPlatformsWidget, SMultiSelectComboBox)
				.SelectValues(&PlatformList)
				.OnCheckedValuesChanged_Lambda([this]()
				{
					RegenerateActivePlatformFilters();
					ValidateBuildGroupSelection();
				})
			]
		]
	];

	Panel->AddSlot()
	.Padding(FMargin(ColumnMargin, 10, ColumnMargin, 0))
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Top)
	[
		SNew(STextBlock)
			.ColorAndOpacity(TitleColor)
			.Font(TitleFont)
			.Text(LOCTEXT("BuildSelection_BuildsLabel", "Builds"))
	];

	Panel->AddSlot()
	.Padding(FMargin(ColumnMargin, RowMargin))
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(BuildGroupListView, SListView<FBuildSelectionBuildGroupPtr>)
				.ListItemsSource(&BuildGroups)
				.OnGenerateRow(this, &SBuildSelection::GenerateBuildGroupRow)
				.OnSelectionChanged(this, &SBuildSelection::BuildGroupSelectionChanged)
				.OnContextMenuOpening(this, &SBuildSelection::OnGetBuildGroupContextMenuContent)
				.SelectionMode(ESelectionMode::Single)
				.OnIsSelectableOrNavigable(this, &SBuildSelection::BuildGroupIsSelectableOrNavigable)
				.IsEnabled_Lambda([this]
				{
					return !BuildListRefreshesInProgress;
				})
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(FBuildGroupIds::ColName).DefaultLabel(LOCTEXT("BuildSelection_BuildGroupColName", "Name"))
						.FillWidth(0.4f)
						.SortMode(this, &SBuildSelection::GetColumnSortMode, FBuildGroupIds::ColName)
						.OnSort(this, &SBuildSelection::OnColumnSortModeChanged)
					+ SHeaderRow::Column(FBuildGroupIds::ColCommit).DefaultLabel(LOCTEXT("BuildSelection_BuildGroupColCommit", "Commit"))
						.DefaultTooltip(LOCTEXT("BuildSelection_BuildGroupColCommitTooltip", "Commit/Changelist for the build"))
						.FillWidth(0.10f).HAlignCell(HAlign_Center).HAlignHeader(HAlign_Center).VAlignCell(VAlign_Center)
						.SortMode(this, &SBuildSelection::GetColumnSortMode, FBuildGroupIds::ColCommit)
						.OnSort(this, &SBuildSelection::OnColumnSortModeChanged)
					+ SHeaderRow::Column(FBuildGroupIds::ColSuffix).DefaultLabel(LOCTEXT("BuildSelection_BuildGroupColSuffix", "Suffix"))
						.DefaultTooltip(LOCTEXT("BuildSelection_BuildGroupColSuffixTooltip", "Modifier on top of the commit/changelist for the build"))
						.FillWidth(0.10f).HAlignCell(HAlign_Center).HAlignHeader(HAlign_Center).VAlignCell(VAlign_Center)
						.SortMode(this, &SBuildSelection::GetColumnSortMode, FBuildGroupIds::ColSuffix)
						.OnSort(this, &SBuildSelection::OnColumnSortModeChanged)
					+ SHeaderRow::Column(FBuildGroupIds::ColCategory).DefaultLabel(LOCTEXT("BuildSelection_BuildGroupColCategory", "Category"))
						.DefaultTooltip(LOCTEXT("BuildSelection_BuildGroupColCategoryTooltip", "Category for the build"))
						.SortMode(this, &SBuildSelection::GetColumnSortMode, FBuildGroupIds::ColCategory)
						.OnSort(this, &SBuildSelection::OnColumnSortModeChanged)
						.FillWidth(0.25f).HAlignCell(HAlign_Left).HAlignHeader(HAlign_Center).VAlignCell(VAlign_Center)
					+ SHeaderRow::Column(FBuildGroupIds::ColCreated).DefaultLabel(LOCTEXT("BuildSelection_BuildGroupColCreated", "Created"))
						.DefaultTooltip(LOCTEXT("BuildSelection_BuildGroupColCreatedTooltip", "When the build was created"))
						.FillWidth(0.15f).HAlignCell(HAlign_Left).HAlignHeader(HAlign_Center).VAlignCell(VAlign_Center)
						.SortMode(this, &SBuildSelection::GetColumnSortMode, FBuildGroupIds::ColCreated)
						.OnSort(this, &SBuildSelection::OnColumnSortModeChanged)
				)
		]
	];

	Panel->AddSlot()
	.Padding(FMargin(ColumnMargin, 3, ColumnMargin, 0))
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Bottom)
	[
		SNew(STextBlock)
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.Text_Lambda([this]()
			{
				if (BuildListRefreshesInProgress)
				{
					return LOCTEXT("BuildSelection_ResultLoading", "Loading...");
				}
				int32 VisibleItemCount = 0;
				if (ActivePlatformFilters.IsEmpty())
				{
					VisibleItemCount = BuildGroups.Num();
				}
				else
				{
					for (FBuildSelectionBuildGroupPtr BuildGroup : BuildGroups)
					{
						bool bHasAllRequiredPlatforms = true;
						for (const FString& ActivePlatformFilter : ActivePlatformFilters)
						{
							if (!BuildGroup->PerPlatformBuilds.Contains(ActivePlatformFilter))
							{
								bHasAllRequiredPlatforms = false;
								break;
							}
						}
						if (bHasAllRequiredPlatforms)
						{
							VisibleItemCount++;
						}
					}
				}
				return FText::Format(LOCTEXT("BuildSelection_ResultDescription", "{0} {0}|plural(one=item,other=items)"), FText::AsNumber(VisibleItemCount));
			})
	];

	Panel->AddSlot()
	.Padding(FMargin(ColumnMargin, RowMargin))
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Bottom)
	[

		SNew(SHorizontalBox)
		.Visibility_Lambda([this]()
		{
			return !!BuildListRefreshesInProgress || BuildGroupListView->GetNumItemsSelected() == 0 ? EVisibility::Collapsed : EVisibility::Visible;
		})
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.FillWidth(0.5f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SNew(STextBlock)
					.ColorAndOpacity(TitleColor)
					.Font(TitleFont)
					.Text(LOCTEXT("BuildSelection_AvailablePlatforms", "Available Platforms"))
			]
			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SAssignNew(SelectedGroupPlatformGrid, SGridPanel)
			]
		]
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.FillWidth(0.5f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				.AutoWidth()
				[
					SNew(STextBlock)
						.ColorAndOpacity(TitleColor)
						.Font(TitleFont)
						.Text(LOCTEXT("BuildSelection_Destination", "Destination"))
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ToolTipText(LOCTEXT("BuildSelection_DestinationExploreTooltip", "Explore the destination"))
					.OnClicked(this, &SBuildSelection::ExploreDestination_OnClicked)
					[
						SNew(SImage)
						.Image_Lambda([this]
						{
							return GetSelectedBuildType() == EBuildType::Oplog ? FAppStyle::Get().GetBrush("Zen.BrowserView") : FAppStyle::Get().GetBrush("Zen.FolderView");
						})
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				GetBuildDestinationPanel()
			]
			+SVerticalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(0, RowMargin))
			[
				SNew(SButton)
				.Text(LOCTEXT("BuildSelection_Download", "Download"))
				.ToolTipText(LOCTEXT("BuildSelection_DownloadTooltip", "Start a download of the selected build for the selected platforms"))
				.ButtonStyle(FAppStyle::Get(), "Button")
				.IsEnabled_Lambda([this]()
				{
					const bool bDestinationValid = !GetEffectiveDestination().IsEmpty();
					if (!bDestinationValid)
					{
						return false;
					}

					TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();
					if (SelectedItems.Num() == 0)
					{
						return false;
					}

					for (const FString& PlatformForSelectedGroup : SelectedGroupSelectedPlatforms)
					{
						if (SelectedItems[0]->PerPlatformBuilds.Contains(PlatformForSelectedGroup))
						{
							return true;
						}
					}
					return false;
				})
				.OnClicked_Lambda([this]()
				{
					using namespace UE::Zen::Build;

					if (TSharedPtr<FBuildServiceInstance> ServiceInstance = BuildServiceInstance.Get())
					{
						TArray<FBuildSelectionBuildGroupPtr> SelectedItems = BuildGroupListView->GetSelectedItems();
						if (SelectedItems.Num() == 0)
						{
							return FReply::Handled();
						}

						for (const FString& PlatformForSelectedGroup : SelectedGroupSelectedPlatforms)
						{
							if (FBuildServiceInstance::FBuildRecord* BuildRecord = SelectedItems[0]->PerPlatformBuilds.Find(PlatformForSelectedGroup))
							{
								FString Bucket = FString::Printf(TEXT("%s.%s.%s.%s"), **SelectedProject, **SelectedBuildType, **SelectedStream, *PlatformForSelectedGroup);

								FString DestinationPlatformName = PlatformForSelectedGroup;
								if (FCbFieldView CookPlatformField = BuildRecord->Metadata["cookPlatform"]; CookPlatformField.HasValue() && !CookPlatformField.HasError())
								{
									DestinationPlatformName = *WriteToString<64>(CookPlatformField.AsString());
								}

								FName TargetPlatformName = NAME_None;
								if (FCbFieldView PlatformField = BuildRecord->Metadata["platform"]; PlatformField.HasValue() && !PlatformField.HasError())
								{
									TargetPlatformName = FName(PlatformField.AsString());
								}

								EBuildType BuildType = GetSelectedBuildType();
								int BuildTypeIndex = (int)BuildType;
								const bool bAppendBuildNameToDestination = bAppendBuildNameToDestinations[BuildTypeIndex];

								FString TransferName = FString::Printf(TEXT("%s-%s"), *SelectedItems[0]->DisplayName, *DestinationPlatformName);

								if (BuildType == EBuildType::Oplog)
								{
									FString ProjectFilePath = FUProjectDictionary::GetDefault().GetProjectPathForGame(**SelectedProject);
									FString DestinationProjectId = GetEffectiveDestination();
									FString DestinationOplogId = DestinationPlatformName;
									if (bAppendBuildNameToDestination)
									{
										DestinationOplogId = FString::Printf(TEXT("%s.%s"), *DestinationOplogId, *SanitizeForZenId(SelectedItems[0]->DisplayName));
									}
									FBuildServiceInstance::FBuildTransfer BuildTransfer =
										ServiceInstance->StartOplogBuildTransfer(BuildRecord->BuildId, TransferName, DestinationProjectId, DestinationOplogId, ProjectFilePath, SelectedItems[0]->Namespace, Bucket, TargetPlatformName);
									OnBuildTransferStarted.ExecuteIfBound(BuildTransfer, SelectedItems[0]->DisplayName, PlatformForSelectedGroup);
								}
								else
								{
									FString DestinationFolder;
									if (bAppendBuildNameToDestination)
									{
										DestinationFolder = FPaths::Combine(GetEffectiveDestination(), SanitizeForPath(SelectedItems[0]->DisplayName), DestinationPlatformName);
									}
									else
									{
										DestinationFolder = FPaths::Combine(GetEffectiveDestination(), DestinationPlatformName);
									}
									FBuildServiceInstance::FBuildTransfer BuildTransfer =
										ServiceInstance->StartBuildTransfer(BuildRecord->BuildId, TransferName, DestinationFolder, SelectedItems[0]->Namespace, Bucket);
									OnBuildTransferStarted.ExecuteIfBound(BuildTransfer, SelectedItems[0]->DisplayName, PlatformForSelectedGroup);
								}
							}
						}
					}
					return FReply::Handled();
				})
			]
		]
	];

	return Panel;
}

void
SBuildGroupTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const FBuildSelectionBuildGroupPtr InBuildGroup)
{
	BuildGroup = InBuildGroup;

	SMultiColumnTableRow<FBuildSelectionBuildGroupPtr>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget>
SBuildGroupTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	using namespace UE::BuildSelection::Internal;

	if (ColumnName == FBuildGroupIds::ColName)
	{
		return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1.f,1.f)
				[
					SNew(STextBlock).Text(FText::FromString(BuildGroup->DisplayName))
				];
	}
	else if (ColumnName == FBuildGroupIds::ColCommit)
	{
		return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1.f,1.f)
				[
					SNew(STextBlock).Text(FText::FromString(BuildGroup->CommitIdentifier))
				];
	}
	else if (ColumnName == FBuildGroupIds::ColSuffix)
	{
		return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1.f,1.f)
				[
					SNew(STextBlock).Text(FText::FromString(BuildGroup->Suffix))
				];
	}
	else if (ColumnName == FBuildGroupIds::ColCategory)
	{
		return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1.f,1.f)
				[
					SNew(STextBlock).Text(FText::FromString(BuildGroup->Category))
				];
	}
	else if (ColumnName == FBuildGroupIds::ColCreated)
	{
		if (BuildGroup->CreatedAt.GetTicks() != 0)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1.f,1.f)
				[
					SNew(STextBlock).Text(FText::AsDateTime(BuildGroup->CreatedAt, EDateTimeStyle::Short))
				];
		}
	}

	return SNullWidget::NullWidget;
}

const FSlateBrush*
SBuildGroupTableRow::GetBorder() const
{
	return STableRow<FBuildSelectionBuildGroupPtr>::GetBorder();
}

FReply
SBuildGroupTableRow::OnBrowseClicked()
{
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
