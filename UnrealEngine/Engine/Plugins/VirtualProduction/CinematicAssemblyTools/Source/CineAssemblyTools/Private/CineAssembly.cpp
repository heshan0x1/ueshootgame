// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineAssembly.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CineAssemblyNamingTokens.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "LevelSequenceShotMetaDataLibrary.h"
#include "MovieScene.h"
#include "NamingTokensEngineSubsystem.h"
#include "Sections/MovieSceneSubSection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "UniversalObjectLocator.h"
#include "UObject/AssetRegistryTagsContext.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "MovieSceneMetaData.h"
#endif

#define LOCTEXT_NAMESPACE "CineAssembly"

const FName UCineAssembly::AssetRegistryTag_AssemblyType = "AssemblyType";
const FName UCineAssembly::AssemblyGuidPropertyName = GET_MEMBER_NAME_CHECKED(UCineAssembly, AssemblyGuid);

namespace UE::CineAssembly
{
	int32 GetLocalTimezone()
	{
		const FTimespan DifferentLocalToUTC = FDateTime::Now() - FDateTime::UtcNow();
		const int32 DifferenceMinutes = FMath::RoundToInt(DifferentLocalToUTC.GetTotalMinutes());

		const int32 Hours = DifferenceMinutes / 60;
		const int32 Minutes = DifferenceMinutes % 60;

		const int32 Timezone = (Hours * 100) + Minutes;
		return Timezone;
	}

	// Taken from FDateTimeStructCustomization
	int32 ConvertShortTimezone(int32 ShortTimezone)
	{
		// Convert timezones from short-format into long format, -5 -> -0500
		// Timezone Hour ranges go from -12 to +14 from UTC
		if (ShortTimezone >= -12 && ShortTimezone <= 14)
		{
			return ShortTimezone * 100;
		}

		// Not a short-form timezone
		return ShortTimezone;
	}

	// Taken from FDateTimeStructCustomization
	FDateTime ConvertTime(const FDateTime& InDate, int32 InTimezone, int32 OutTimezone)
	{
		if (InTimezone == OutTimezone)
		{
			return InDate;
		}

		// Timezone Hour ranges go from -12 to +14 from UTC
		// Convert from whole-hour to the full-format HHMM (-5 -> -0500, 0 -> +0000, etc)
		InTimezone = ConvertShortTimezone(InTimezone);
		OutTimezone = ConvertShortTimezone(OutTimezone);

		// Extract timezone minutes
		const int32 InTimezoneMinutes = (FMath::Abs(InTimezone) % 100);
		const int32 OutTimezoneMinutes = (FMath::Abs(OutTimezone) % 100);

		// Calculate our Minutes difference
		const int32 MinutesDifference = OutTimezoneMinutes - InTimezoneMinutes;

		// Calculate our Hours difference
		const int32 HoursDifference = (OutTimezone / 100) - (InTimezone / 100);

		return FDateTime(InDate + FTimespan(HoursDifference, MinutesDifference, 0));
	}
} // namespace UE::CineAssembly

UCineAssembly::UCineAssembly()
{
	MetadataJsonObject = MakeShared<FJsonObject>();
}

void UCineAssembly::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_WasLoaded) && !AssemblyGuid.IsValid())
	{
		AssemblyGuid = FGuid::NewGuid();
	}
}

void UCineAssembly::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		AssemblyGuid = FGuid::NewGuid();
	}
}

void UCineAssembly::PostLoad()
{
	Super::PostLoad();

	if (!AssemblyGuid.IsValid())
	{
		AssemblyGuid = FGuid::NewGuid();
	}
}

bool UCineAssembly::IsCompatibleAsSubSequence(const UMovieSceneSequence& ParentSequence) const
{
	return !bIsDataOnly;
}

void UCineAssembly::Initialize()
{
	Super::Initialize();
}

void UCineAssembly::InitializeFromTemplate(ULevelSequence* Template)
{
	Initialize();

	if (Template == nullptr)
	{
		return;
	}

	if (UMovieScene* TemplateMovieScene = Template->GetMovieScene())
	{
		// Attempt to duplicate the inner MovieScene. Assign only if duplication is a success to avoid leaving the assembly in a partially valid state.
		// Duplicating the MovieScene will null any event track bindings which is the currently supported behaviour.
		UMovieScene* DuplicateMovieScene = DuplicateObject(TemplateMovieScene, this);

		if (DuplicateMovieScene == nullptr)
		{
			return;
		}

		MovieScene = DuplicateMovieScene;

		// Fix up supported bindings
		if (const FMovieSceneBindingReferences* TemplateBindingReferences = Template->GetBindingReferences())
		{
			FMovieSceneBindingReferences& ThisBindingReferencesRef = BindingReferences;

			for (const FMovieSceneBindingReference& Reference : TemplateBindingReferences->GetAllReferences())
			{
				FUniversalObjectLocator NewLocator = Reference.Locator;

				UMovieSceneCustomBinding* CustomBindingDuplicate = Reference.CustomBinding != nullptr 
					? Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(Reference.CustomBinding, MovieScene))
					: nullptr;
				
				ThisBindingReferencesRef.AddBinding(Reference.ID, MoveTemp(NewLocator), Reference.ResolveFlags, CustomBindingDuplicate);
			}
		}
	}
}

FGuid UCineAssembly::GetAssemblyGuid() const
{
	return AssemblyGuid;
}

const UCineAssemblySchema* UCineAssembly::GetSchema() const
{
	return BaseSchema.Get();
}

void UCineAssembly::SetSchema(UCineAssemblySchema* InSchema)
{
	if (BaseSchema == nullptr)
	{
		BaseSchema = InSchema;
		ChangeSchema(InSchema);
	}
}

void UCineAssembly::ChangeSchema(UCineAssemblySchema* InSchema)
{
	// Remove all metadata associated with the old schema before changing it
	if (BaseSchema)
	{
		for (const FAssemblyMetadataDesc& MetadataDesc : BaseSchema->AssemblyMetadata)
		{
			MetadataJsonObject->RemoveField(MetadataDesc.Key);
		}
	}

	BaseSchema = InSchema;

	// Reset the assembly's name based on the schema template
	if (BaseSchema)
	{
		AssemblyName.Template = BaseSchema->DefaultAssemblyName;
	}
	else
	{
		AssemblyName.Template = TEXT("");
	}

	// Add all metadata associated with the new schema (initialized to the default values for each field)
	if (BaseSchema)
	{
		for (const FAssemblyMetadataDesc& MetadataDesc : BaseSchema->AssemblyMetadata)
		{
			if (MetadataDesc.DefaultValue.IsType<FString>())
			{
				if (MetadataDesc.bEvaluateTokens)
				{
					FTemplateString DefaultTemplateString;
					DefaultTemplateString.Template = MetadataDesc.DefaultValue.Get<FString>();
					DefaultTemplateString.Resolved = UCineAssemblyNamingTokens::GetResolvedText(DefaultTemplateString.Template, this);
					SetMetadataAsTokenString(MetadataDesc.Key, DefaultTemplateString);
				}
				else
				{
					const FString& DefaultValue = MetadataDesc.DefaultValue.Get<FString>();
					SetMetadataAsString(MetadataDesc.Key, DefaultValue);
				}
			}
			else if (MetadataDesc.DefaultValue.IsType<bool>())
			{
				const bool& DefaultValue = MetadataDesc.DefaultValue.Get<bool>();
				SetMetadataAsBool(MetadataDesc.Key, DefaultValue);
			}
			else if (MetadataDesc.DefaultValue.IsType<int32>())
			{
				const int32& DefaultValue = MetadataDesc.DefaultValue.Get<int32>();
				SetMetadataAsInteger(MetadataDesc.Key, DefaultValue);
			}
			else if (MetadataDesc.DefaultValue.IsType<float>())
			{
				const float& DefaultValue = MetadataDesc.DefaultValue.Get<float>();
				SetMetadataAsFloat(MetadataDesc.Key, DefaultValue);
			}
		}
	}

	// Reset the list of SubAssembly names to create from the Schema
	SubAssemblyNames.Reset();
	if (BaseSchema)
	{
		for (const FString& SubAssemblyName : BaseSchema->SubsequencesToCreate)
		{
			FTemplateString NewTemplateName;
			NewTemplateName.Template = SubAssemblyName;
			SubAssemblyNames.Add(NewTemplateName);
		}
	}

	// Reset the list of folders names to create from the Schema
	DefaultFolderNames.Reset();
	if (BaseSchema)
	{
		for (const FString& FolderName : BaseSchema->FoldersToCreate)
		{
			FTemplateString NewTemplateName;
			NewTemplateName.Template = FolderName;
			DefaultFolderNames.Add(NewTemplateName);
		}
	}

	// Update the Data Only flag on the assembly
	if (BaseSchema)
	{
		bIsDataOnly = BaseSchema->bIsDataOnly;
	}
}

#if WITH_EDITOR
void UCineAssembly::CreateSubAssemblies()
{
	if (!BaseSchema)
	{
		return;
	}

	// Get the path where the top-level assembly will be created so we can create other assets relative to it
	FString AssemblyPath;
	FString AssemblyRootFolder;
	GetAssetPathAndRootFolder(AssemblyPath, AssemblyRootFolder);

	// Create the default folders for this assembly, based on the schema
	for (FTemplateString& FolderPath : DefaultFolderNames)
	{
		// Resolve any tokens found in the name of the SubAssembly before attempting to create it 
		FolderPath.Resolved = UCineAssemblyNamingTokens::GetResolvedText(FolderPath.Template, this);

		if (FolderPath.Resolved.IsEmpty())
		{
			continue;
		}

		const FString PathToCreate = AssemblyRootFolder / FolderPath.Resolved.ToString();
		const FString RelativeFilePath = FPackageName::LongPackageNameToFilename(PathToCreate);
		const FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(RelativeFilePath);

		// Create the directory on disk, then add its path to the asset registry so it appears in Content Browser
		if (!IFileManager::Get().DirectoryExists(*AbsoluteFilePath))
		{
			constexpr bool bCreateParentFoldersIfMissing = true;
			if (IFileManager::Get().MakeDirectory(*AbsoluteFilePath, bCreateParentFoldersIfMissing))
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
				AssetRegistryModule.Get().AddPath(PathToCreate);
			}
		}
	}

	// Early-out if the subassemblies have already been created (as would be the case if this was a duplicated assembly)
	if (!SubAssemblies.IsEmpty())
	{
		return;
	}

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Create a new CineAssembly for each subsequence, set its playback range to match the parent sequence, and add it to the subsequence track
	for (FTemplateString& SubAssemblyName : SubAssemblyNames)
	{
		// Resolve any tokens found in the name of the SubAssembly before attempting to create it 
		SubAssemblyName.Resolved = UCineAssemblyNamingTokens::GetResolvedText(SubAssemblyName.Template, this);

		const FString SubAssemblyFilename = FPaths::GetBaseFilename(SubAssemblyName.Resolved.ToString());

		if (SubAssemblyFilename.IsEmpty())
		{
			continue;
		}

		// Add a subsequence track to the assembly's sequence
		UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(MovieScene->AddTrack(UMovieSceneSubTrack::StaticClass()));
		SubTrack->SetDisplayName(FText::FromString(SubAssemblyFilename));

		// Before creating each subassembly, sanity check that each one will actually have a unique name (in case there are duplicates in the schema description)
		FString UniquePackageName;
		FString UniqueAssetName;
		AssetTools.CreateUniqueAssetName(AssemblyRootFolder / SubAssemblyName.Resolved.ToString(), TEXT(""), UniquePackageName, UniqueAssetName);

		const FString SubAssemblyPath = FPaths::GetPath(UniquePackageName);

		UPackage* SubAssemblyPackage = CreatePackage(*UniquePackageName);

		const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
		UCineAssembly* SubAssembly = NewObject<UCineAssembly>(SubAssemblyPackage, UCineAssembly::StaticClass(), FName(*UniqueAssetName), Flags);

		if (SubAssembly)
		{
			ULevelSequence* Template = nullptr;
			// Check to see if there is a supplied templated for this Subsequence.
			if (FSoftObjectPath* TemplatePath = BaseSchema->SubsequenceTemplates.Find(SubAssemblyName.Template))
			{
				Template = Cast<ULevelSequence>(TemplatePath->TryLoad());
			}

			if (Template != nullptr)
			{
				SubAssembly->InitializeFromTemplate(Template);
			}
			else
			{
				SubAssembly->Initialize();
			}

			const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
			SubAssembly->GetMovieScene()->SetPlaybackRange(PlaybackRange);

			SubAssembly->Level = Level;
			SubAssembly->ParentAssembly = this;
			SubAssembly->Production = Production;
			SubAssembly->ProductionName = ProductionName;

			ULevelSequenceShotMetaDataLibrary::SetIsSubSequence(SubAssembly, true);

			const FFrameNumber StartFrame = PlaybackRange.GetLowerBoundValue();
			const int32 Duration = PlaybackRange.Size<FFrameNumber>().Value;

			UMovieSceneSubSection* SubSection = SubTrack->AddSequence(SubAssembly, StartFrame, Duration);
			SubAssemblies.Add(SubSection);

			// Notify the asset registry about the new subassembly
			FAssetRegistryModule::AssetCreated(SubAssembly);

			// Mark the package dirty
			SubAssemblyPackage->MarkPackageDirty();
		}
	}
}
#endif

void UCineAssembly::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	const FString AssemblyType = BaseSchema ? BaseSchema->SchemaName : TEXT("");
	Context.AddTag(FAssetRegistryTag(AssetRegistryTag_AssemblyType, AssemblyType, FAssetRegistryTag::ETagType::TT_Alphabetical, FAssetRegistryTag::TD_None));

	// Add tags associated with the assembly metadata
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : MetadataJsonObject->Values)
	{
		if (!Pair.Key.IsEmpty())
		{
			FString ValueString;
			if (MetadataJsonObject->TryGetStringField(Pair.Key, ValueString))
			{
				Context.AddTag(FAssetRegistryTag(*Pair.Key, ValueString, FAssetRegistryTag::ETagType::TT_Alphabetical, FAssetRegistryTag::TD_None));
			}
		}
	}
}

#if WITH_EDITOR

void UCineAssembly::GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const
{
	Super::GetAssetRegistryTagMetadata(OutMetadata);

	OutMetadata.Add(AssetRegistryTag_AssemblyType, FAssetRegistryTagMetadata()
		.SetDisplayName(LOCTEXT("AssemblyType_Label", "AssemblyType"))
		.SetTooltip(LOCTEXT("AssemblyType_Tooltip", "The assembly type of this instance"))
	);
}

void UCineAssembly::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCineAssembly, InstanceMetadata))
	{
		UpdateInstanceMetadata();
	}
}

#endif

void UCineAssembly::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	FString JsonString;

	if (Ar.IsSaving())
 	{
 		TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
 		FJsonSerializer::Serialize(MetadataJsonObject.ToSharedRef(), JsonWriter);
 	}

	Ar << JsonString;

	if (Ar.IsLoading())
	{
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
		FJsonSerializer::Deserialize(JsonReader, MetadataJsonObject);

		// After the JsonObject has been loaded, add a naming token for each of its keys
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : MetadataJsonObject->Values)
		{
			AddMetadataNamingToken(Pair.Key);
		}
	}
}

TSoftObjectPtr<UWorld> UCineAssembly::GetLevel()
{
	if (Level.IsValid())
	{
		return TSoftObjectPtr<UWorld>(Level);
	}
	return nullptr;
}

void UCineAssembly::SetLevel(TSoftObjectPtr<UWorld> InLevel)
{
	Level = InLevel.ToSoftObjectPath();
}

FString UCineAssembly::GetNoteText()
{
	return AssemblyNote;
}

void UCineAssembly::SetNoteText(FString InNote)
{
	AssemblyNote = InNote;
}

void UCineAssembly::AppendToNoteText(FString InNote)
{
	AssemblyNote.Append(TEXT("\n"));
	AssemblyNote.Append(InNote);
}

FGuid UCineAssembly::GetProductionID()
{
	return Production;
}

FString UCineAssembly::GetProductionName()
{
	return ProductionName;
}

TSoftObjectPtr<UCineAssembly> UCineAssembly::GetParentAssembly()
{
	if (ParentAssembly.IsValid())
	{
		return TSoftObjectPtr<UCineAssembly>(ParentAssembly);
	}
	return nullptr;
}

void UCineAssembly::SetParentAssembly(TSoftObjectPtr<UCineAssembly> InParent)
{
	ParentAssembly = InParent.ToSoftObjectPath();
}

#if WITH_EDITOR

FString UCineAssembly::GetAuthor() const
{
	UMovieSceneMetaData* MetaData = FindMetaData<UMovieSceneMetaData>();
	return MetaData != nullptr ? MetaData->GetAuthor() : FString();
}

void UCineAssembly::SetAuthor(const FString& InAuthor)
{
	UMovieSceneMetaData* MetaData = FindOrAddMetaData<UMovieSceneMetaData>();
	MetaData->Modify();
	MetaData->SetAuthor(InAuthor);
}

FString UCineAssembly::GetCreatedString() const
{
	if (TOptional<FDateTime> CreatedLocalTime = TryGetCreatedAsLocalTime(); CreatedLocalTime.IsSet())
	{
		return CreatedLocalTime->ToString();
	}
	
	return FString();
}

FString UCineAssembly::GetDateCreatedString() const
{
	if (TOptional<FDateTime> CreatedLocalTime = TryGetCreatedAsLocalTime(); CreatedLocalTime.IsSet())
	{
		return CreatedLocalTime->ToFormattedString(TEXT("%Y-%m-%d"));
	}

	return FString();
}

FString UCineAssembly::GetTimeCreatedString() const
{
	if (TOptional<FDateTime> CreatedLocalTime = TryGetCreatedAsLocalTime(); CreatedLocalTime.IsSet())
	{
		return CreatedLocalTime->ToFormattedString(TEXT("%H:%M:%S"));
	}

	return FString();
}

#endif // WITH_EDITOR

FString UCineAssembly::GetFullMetadataString() const
{
	FString JsonString;

	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(MetadataJsonObject.ToSharedRef(), JsonWriter);

	return JsonString;
}

TArray<FString> UCineAssembly::GetMetadataKeys() const
{
	TArray<FString> Keys;
	MetadataJsonObject->Values.GetKeys(Keys);
	return Keys;
}

void UCineAssembly::SetMetadataAsString(FString InKey, FString InValue)
{
	Modify();
	MetadataJsonObject->SetStringField(InKey, InValue);
	AddMetadataNamingToken(InKey);
}

void UCineAssembly::SetMetadataAsTokenString(FString InKey, FTemplateString InValue)
{
	Modify();
	TSharedPtr<FJsonObject> TemplateStringObject = FJsonObjectConverter::UStructToJsonObject<FTemplateString>(InValue);
	MetadataJsonObject->SetObjectField(InKey, TemplateStringObject);
	AddMetadataNamingToken(InKey);
}

void UCineAssembly::SetMetadataAsBool(FString InKey, bool InValue)
{
	Modify();
	MetadataJsonObject->SetBoolField(InKey, InValue);
	AddMetadataNamingToken(InKey);
}

void UCineAssembly::SetMetadataAsInteger(FString InKey, int32 InValue)
{
	Modify();
	MetadataJsonObject->SetNumberField(InKey, InValue);
	AddMetadataNamingToken(InKey);
}

void UCineAssembly::SetMetadataAsFloat(FString InKey, float InValue)
{
	Modify();
	MetadataJsonObject->SetNumberField(InKey, InValue);
	AddMetadataNamingToken(InKey);
}

bool UCineAssembly::GetMetadataAsString(FString InKey, FString& OutValue) const
{
	if (!MetadataJsonObject->TryGetStringField(InKey, OutValue))
	{
		FTemplateString TemplateString;
		if (!GetMetadataAsTokenString(InKey, TemplateString))
		{
			OutValue = TEXT("");
			return false;
		}

		OutValue = TemplateString.Resolved.ToString();
	}
	return true;
}

bool UCineAssembly::GetMetadataAsTokenString(FString InKey, FTemplateString& OutValue) const
{
	const TSharedPtr<FJsonObject>* TemplateStringObject;
	if (!MetadataJsonObject->TryGetObjectField(InKey, TemplateStringObject))
	{
		OutValue.Template = TEXT("");
		OutValue.Resolved = FText::GetEmpty();
		return false;
	}

	FJsonObjectConverter::JsonObjectToUStruct<FTemplateString>(TemplateStringObject->ToSharedRef(), &OutValue);
	return true;
}

bool UCineAssembly::GetMetadataAsBool(FString InKey, bool& OutValue) const
{
	if (!MetadataJsonObject->TryGetBoolField(InKey, OutValue))
	{
		OutValue = false;
		return false;
	}
	return true;
}

bool UCineAssembly::GetMetadataAsInteger(FString InKey, int32& OutValue) const
{
	if (!MetadataJsonObject->TryGetNumberField(InKey, OutValue))
	{
		OutValue = 0;
		return false;
	}
	return true;
}

bool UCineAssembly::GetMetadataAsFloat(FString InKey, float& OutValue) const
{
	if (!MetadataJsonObject->TryGetNumberField(InKey, OutValue))
	{
		OutValue = 0;
		return false;
	}
	return true;
}

void UCineAssembly::UpdateInstanceMetadata()
{
	// Copy our metadata key list so that we can remove keys as we encounter them in the map non-destructively
	TArray<FName> InstanceMetadataKeysCopy = InstanceMetadataKeys;
	for (const TPair<FName, FString>& Pair : InstanceMetadata)
	{
		if (!Pair.Key.IsNone())
		{
			if (InstanceMetadataKeys.Contains(Pair.Key))
			{
				// This is an existing metadata key that we are already tracking
				InstanceMetadataKeysCopy.Remove(Pair.Key);
			}
			else
			{
				// This is a new metadata key that we were not previously tracking
				InstanceMetadataKeys.Add(Pair.Key);
			}

			SetMetadataAsString(Pair.Key.ToString(), Pair.Value);
		}
	}

	// If there are any keys remaining in our copy of the metadata key list, then those keys must have been removed from the instance metadata map
	for (const FName& Key : InstanceMetadataKeysCopy)
	{
		MetadataJsonObject->RemoveField(Key.ToString());
	}
}

TOptional<FDateTime> UCineAssembly::TryGetCreatedAsLocalTime() const
{
#if WITH_EDITOR
	// Assuming that the Created field from the Metadata is in UTC.
	if (UMovieSceneMetaData* MetaData = FindMetaData<UMovieSceneMetaData>(); MetaData != nullptr)
	{
		return UE::CineAssembly::ConvertTime(MetaData->GetCreated(), 0 /* UTC */, UE::CineAssembly::GetLocalTimezone());
	}
#endif // WITH_EDITOR

	return TOptional<FDateTime>();
}

void UCineAssembly::AddMetadataNamingToken(const FString& InKey)
{
	UNamingTokensEngineSubsystem* NamingTokensSubsystem = GEngine->GetEngineSubsystem<UNamingTokensEngineSubsystem>();
	UCineAssemblyNamingTokens* CineAssemblyNamingTokens = Cast<UCineAssemblyNamingTokens>(NamingTokensSubsystem->GetNamingTokens(UCineAssemblyNamingTokens::TokenNamespace));

	CineAssemblyNamingTokens->AddMetadataToken(InKey);
}

void UCineAssembly::GetAssetPathAndRootFolder(FString& OutAssetPath, FString& OutRootFolder)
{
	const FAssetData AssemblyAssetData = FAssetData(this);
	OutAssetPath = AssemblyAssetData.PackagePath.ToString();

	OutRootFolder = OutAssetPath;
	if (BaseSchema)
	{
		const FString DefaultAssemblyPath = BaseSchema->DefaultAssemblyPath;
		const FString ResolvedAssemblyPath = UCineAssemblyNamingTokens::GetResolvedText(DefaultAssemblyPath, this).ToString();

		OutRootFolder = OutRootFolder.Replace(*ResolvedAssemblyPath, TEXT(""));
	}
}

#undef LOCTEXT_NAMESPACE
