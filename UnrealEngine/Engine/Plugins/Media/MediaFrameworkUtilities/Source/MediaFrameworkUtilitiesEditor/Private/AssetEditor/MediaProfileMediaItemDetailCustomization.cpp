// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaProfileMediaItemDetailCustomization.h"

#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "IStructureDataProvider.h"
#include "MediaFrameworkWorldSettingsAssetUserData.h"
#include "MediaProfileEditorUserSettings.h"
#include "Algo/Count.h"
#include "Profile/MediaProfilePlaybackManager.h"
#include "UI/MediaFrameworkUtilitiesEditorStyle.h"

#define LOCTEXT_NAMESPACE "FMediaProfileMediaItemDetailCustomization"

FText FMediaProfileMediaSourceDetailCustomization::GetMediaItemTypeText() const
{
	return LOCTEXT("MediaSourceTypeText", "Media Source");
}

void FMediaProfileMediaSourceDetailCustomization::RegisterMediaTypeSection(FPropertyEditorModule& PropertyModule, const FName& MediaTypeCategory)
{
	static bool bRegistered = false;
	if (!bRegistered)
	{
		const TSharedPtr<FPropertySection> Section = PropertyModule.FindOrCreateSection(
			UMediaSource::StaticClass()->GetFName(),
			TEXT("MediaSourceSection"),
			GetMediaItemTypeText());

		Section->AddCategory(MediaTypeCategory);
		bRegistered = true;
	}
}

FText FMediaProfileMediaSourceDetailCustomization::GetMediaObjectLabel(UMediaSource* InMediaObject) const
{
	if (!MediaProfile.IsValid())
	{
		return FText::GetEmpty();
	}

	int32 Index = MediaProfile->FindMediaSourceIndex(InMediaObject);
	if (InMediaObject->IsA<UDummyMediaSource>())
	{
		Index = Cast<UDummyMediaSource>(InMediaObject)->MediaProfileIndex;
	}
		
	return FText::FromString(MediaProfile->GetLabelForMediaSource(Index));
}

void FMediaProfileMediaSourceDetailCustomization::SetMediaObjectLabel(UMediaSource* InMediaObject, const FText& InLabel)
{
	if (!MediaProfile.IsValid())
	{
		return;
	}
	
	int32 Index = MediaProfile->FindMediaSourceIndex(InMediaObject);
	if (InMediaObject->IsA<UDummyMediaSource>())
	{
		Index = Cast<UDummyMediaSource>(InMediaObject)->MediaProfileIndex;
	}
		
	MediaProfile->SetLabelForMediaSource(Index, InLabel.ToString());
}

void FMediaProfileMediaSourceDetailCustomization::SetMediaObject(UMediaSource* InOriginalMediaObject, UMediaSource* InNewMediaObject)
{
	if (!MediaProfile.IsValid())
    {
    	return;
    }
    	
	int32 Index = MediaProfile->FindMediaSourceIndex(InOriginalMediaObject);
	if (InOriginalMediaObject->IsA<UDummyMediaSource>())
	{
		Index = Cast<UDummyMediaSource>(InOriginalMediaObject)->MediaProfileIndex;
	}
		
	MediaProfile->SetMediaSource(Index, InNewMediaObject);
}

class SMediaCaptureMethodComboButton : public SComboButton
{
public:
	SLATE_BEGIN_ARGS(SMediaCaptureMethodComboButton) { }
		SLATE_EVENT(FSimpleDelegate, OnMethodChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UObject>>& InObjects)
	{
		OnMethodChanged = InArgs._OnMethodChanged;
		Objects = InObjects;
		
		SComboButton::Construct(SComboButton::FArguments()
			.OnGetMenuContent_Lambda([this]
			{
				FMenuBuilder MenuBuilder(true, nullptr);

				MenuBuilder.AddMenuEntry(LOCTEXT("MediaCaptureCurrentViewportMethod", "Current Viewport"),
					LOCTEXT("MediaCaptureCurrentViewportMethodToolTip", "Capture from the current viewport"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SMediaCaptureMethodComboButton::OnCurrentViewportMethodSelected)));
				
				MenuBuilder.AddMenuEntry(LOCTEXT("MediaCaptureViewportMethod", "Media Viewport"),
					LOCTEXT("MediaCaptureViewportMethodToolTip", "Capture from a viewport"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SMediaCaptureMethodComboButton::OnViewportMethodSelected)));

				MenuBuilder.AddMenuEntry(LOCTEXT("MediaCaptureRenderTargetMethod", "Render Target"),
					LOCTEXT("MediaCaptureRenderTargetMethodToolTip", "Capture from a render target"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SMediaCaptureMethodComboButton::OnRenderTargetMethodSelected)));
				
				return MenuBuilder.MakeWidget();
			})
			.ContentPadding(0.f)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &SMediaCaptureMethodComboButton::GetButtonText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]);
	}

private:
	void OnCurrentViewportMethodSelected()
	{
		UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
		if (!MediaCaptureSettings)
		{
			return;
		}
		
		for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
		{
			TStrongObjectPtr<UObject> Object = WeakObject.Pin();
			UMediaOutput* MediaOutput = Object.IsValid() ? Cast<UMediaOutput>(Object.Get()) : nullptr;
			if (!MediaOutput)
			{
				continue;
			}

			if (MediaOutput->IsA<UDummyMediaOutput>())
			{
				continue;
			}
			
			// Now, if the object has an existing viewport or render target capture config, we want to copy that config over to the current viewport config, so find it
			if (FMediaFrameworkCaptureCameraViewportCameraOutputInfo* ExistingViewportOutputInfo =
				MediaCaptureSettings->ViewportCaptures.FindByPredicate(GetFindOutputInfoPredicate<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>(MediaOutput)))
			{
				MediaCaptureSettings->CurrentViewportMediaOutput.CaptureOptions = ExistingViewportOutputInfo->CaptureOptions;
			}
			else if (FMediaFrameworkCaptureRenderTargetCameraOutputInfo* ExistingRenderTargetOutputInfo =
				MediaCaptureSettings->RenderTargetCaptures.FindByPredicate(GetFindOutputInfoPredicate<FMediaFrameworkCaptureRenderTargetCameraOutputInfo>(MediaOutput)))
			{
				MediaCaptureSettings->CurrentViewportMediaOutput.CaptureOptions = ExistingRenderTargetOutputInfo->CaptureOptions;
			}

			// We expect there to be only one capture configuration per media output, so clear
			// any existing viewport and render target capture configurations before adding the new one
			MediaCaptureSettings->ViewportCaptures.RemoveAll(GetFindOutputInfoPredicate<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>(MediaOutput));
			MediaCaptureSettings->RenderTargetCaptures.RemoveAll(GetFindOutputInfoPredicate<FMediaFrameworkCaptureRenderTargetCameraOutputInfo>(MediaOutput));
			
			MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput = MediaOutput;
		}

		MediaCaptureSettings->SaveConfig();
		OnMethodChanged.ExecuteIfBound();
	}
	
	void OnViewportMethodSelected()
	{
		UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
		if (!MediaCaptureSettings)
		{
			return;
		}
		
		for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
		{
			TStrongObjectPtr<UObject> Object = WeakObject.Pin();
			UMediaOutput* MediaOutput = Object.IsValid() ? Cast<UMediaOutput>(Object.Get()) : nullptr;
			if (!MediaOutput)
			{
				continue;
			}

			if (MediaOutput->IsA<UDummyMediaOutput>())
			{
				continue;
			}
			
			// We expect there to be only one capture configuration per media output, so clear
			// any existing viewport capture configurations before adding the new one
			MediaCaptureSettings->ViewportCaptures.RemoveAll(GetFindOutputInfoPredicate<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>(MediaOutput));

			FMediaFrameworkCaptureCameraViewportCameraOutputInfo NewViewportOutputInfo;
			NewViewportOutputInfo.MediaOutput = MediaOutput;
			
			// Now, if the object has an existing render target capture config, we want to copy that config over to the new viewport config, so find it
			FMediaFrameworkCaptureRenderTargetCameraOutputInfo* ExistingRenderTargetOutputInfo =
				MediaCaptureSettings->RenderTargetCaptures.FindByPredicate(GetFindOutputInfoPredicate<FMediaFrameworkCaptureRenderTargetCameraOutputInfo>(MediaOutput));
			
			if (ExistingRenderTargetOutputInfo)
			{
				NewViewportOutputInfo.CaptureOptions = ExistingRenderTargetOutputInfo->CaptureOptions;
			}

			if (MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput)
			{
				MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput = nullptr;
			}

			MediaCaptureSettings->ViewportCaptures.Add(NewViewportOutputInfo);
			MediaCaptureSettings->RenderTargetCaptures.RemoveAll(GetFindOutputInfoPredicate<FMediaFrameworkCaptureRenderTargetCameraOutputInfo>(MediaOutput));
		}

		MediaCaptureSettings->SaveConfig();
		OnMethodChanged.ExecuteIfBound();
	}

	void OnRenderTargetMethodSelected()
	{
		UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
		if (!MediaCaptureSettings)
		{
			return;
		}
		
		for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
		{
			TStrongObjectPtr<UObject> Object = WeakObject.Pin();
			UMediaOutput* MediaOutput = Object.IsValid() ? Cast<UMediaOutput>(Object.Get()) : nullptr;
			if (!MediaOutput)
			{
				continue;
			}

			if (MediaOutput->IsA<UDummyMediaOutput>())
			{
				continue;
			}

			// We expect there to be only one capture configuration per media output, so clear
			// any existing render target capture configurations before adding the new one
			MediaCaptureSettings->RenderTargetCaptures.RemoveAll(GetFindOutputInfoPredicate<FMediaFrameworkCaptureRenderTargetCameraOutputInfo>(MediaOutput));

			FMediaFrameworkCaptureRenderTargetCameraOutputInfo NewRenderTargetOutputInfo;
			NewRenderTargetOutputInfo.MediaOutput = MediaOutput;
			
			// Now, if the object has an existing render target capture config, we want to copy that config over to the new viewport config, so find it
			FMediaFrameworkCaptureCameraViewportCameraOutputInfo* ExistingViewportOutputInfo =
				MediaCaptureSettings->ViewportCaptures.FindByPredicate(GetFindOutputInfoPredicate<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>(MediaOutput));
			
			if (ExistingViewportOutputInfo)
			{
				NewRenderTargetOutputInfo.CaptureOptions = ExistingViewportOutputInfo->CaptureOptions;
			}

			if (MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput)
			{
				MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput = nullptr;
			}

			MediaCaptureSettings->RenderTargetCaptures.Add(NewRenderTargetOutputInfo);
			MediaCaptureSettings->ViewportCaptures.RemoveAll(GetFindOutputInfoPredicate<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>(MediaOutput));
		}

		MediaCaptureSettings->SaveConfig();
		OnMethodChanged.ExecuteIfBound();
	}

	FText GetButtonText() const
	{
		UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
		if (!MediaCaptureSettings)
		{
			return LOCTEXT("MediaCaptureMethodNotConfigured", "Not Configured");
		}
		
		bool bCurrentViewport = false;
		int32 NumViewports = 0;
		int32 NumRenderTargets = 0;

		for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
		{
			TStrongObjectPtr<UObject> Object = WeakObject.Pin();
			UMediaOutput* MediaOutput = Object.IsValid() ? Cast<UMediaOutput>(Object.Get()) : nullptr;
			if (!MediaOutput)
			{
				continue;
			}

			bCurrentViewport = MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput;
			NumViewports += Algo::CountIf(MediaCaptureSettings->ViewportCaptures, GetFindOutputInfoPredicate<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>(MediaOutput));
			NumRenderTargets += Algo::CountIf(MediaCaptureSettings->RenderTargetCaptures, GetFindOutputInfoPredicate<FMediaFrameworkCaptureRenderTargetCameraOutputInfo>(MediaOutput));
		}

		const bool bMultipleTypes = (bCurrentViewport && (NumViewports > 0 || NumRenderTargets > 0)) || (NumViewports > 0 && NumRenderTargets > 0);
		if (bMultipleTypes)
		{
			return LOCTEXT("MediaCaptureMethodMultipleValues", "Multiple Values");
		}
		if (bCurrentViewport)
		{
			return LOCTEXT("MediaCaptureCurrentViewportMethod", "Current Viewport");
		}
		if (NumViewports > 0)
		{
			return LOCTEXT("MediaCaptureViewportMethod", "Media Viewport");
		}
		if (NumRenderTargets > 0)
		{
			return LOCTEXT("MediaCaptureRenderTargetMethod", "Render Target");
		}

		return LOCTEXT("MediaCaptureMethodNotConfigured", "Not Configured");
	}
	
	template<typename TOutputInfo>
	TFunction<bool(const TOutputInfo&)> GetFindOutputInfoPredicate(UMediaOutput* InMediaOutput) const
	{
		return [InMediaOutput](const TOutputInfo& OutputInfo)
		{
			return OutputInfo.MediaOutput == InMediaOutput;
		};
	}
	
private:
	TArray<TWeakObjectPtr<UObject>> Objects;
	FSimpleDelegate OnMethodChanged;
};

class FMediaFrameworkCurrentViewportOutputInfoCustomization : public IPropertyTypeCustomization
{
protected:
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override { }

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		// Manually add all capture options properties to avoid an additional property group getting added
		TSharedPtr<IPropertyHandle> CaptureOptionsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMediaFrameworkCaptureCurrentViewportOutputInfo, CaptureOptions));
		uint32 NumChildren;
		CaptureOptionsHandle->GetNumChildren(NumChildren);
		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = CaptureOptionsHandle->GetChildHandle(Index);
			ChildHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FMediaFrameworkCurrentViewportOutputInfoCustomization::PropertyValueChange));			
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
	
	void PropertyValueChange(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		if (UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings())
		{
			MediaCaptureSettings->SaveConfig();
			FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(MediaCaptureSettings, const_cast<FPropertyChangedEvent&>(PropertyChangedEvent));
		}
	}
};

class FMediaFrameworkViewportOutputInfoCustomization : public IPropertyTypeCustomization
{
protected:
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override { }

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TSharedPtr<IPropertyHandle> LockedActorsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMediaFrameworkCaptureCameraViewportCameraOutputInfo, Cameras));
		LockedActorsHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FMediaFrameworkViewportOutputInfoCustomization::PropertyValueChange));
		LockedActorsHandle->SetOnChildPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FMediaFrameworkViewportOutputInfoCustomization::PropertyValueChange));
		ChildBuilder.AddProperty(LockedActorsHandle.ToSharedRef());

		// Manually add all capture options properties to avoid an additional property group getting added
		TSharedPtr<IPropertyHandle> CaptureOptionsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMediaFrameworkCaptureCameraViewportCameraOutputInfo, CaptureOptions));
		uint32 NumChildren;
		CaptureOptionsHandle->GetNumChildren(NumChildren);
		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = CaptureOptionsHandle->GetChildHandle(Index);
			ChildHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FMediaFrameworkViewportOutputInfoCustomization::PropertyValueChange));			
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
	
	void PropertyValueChange(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		if (UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings())
		{
			MediaCaptureSettings->SaveConfig();
			FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(MediaCaptureSettings, const_cast<FPropertyChangedEvent&>(PropertyChangedEvent));
		}
	}
};

class FMediaFrameworkRenderTargetOutputInfoCustomization : public IPropertyTypeCustomization
{
protected:
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override { }

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TSharedPtr<IPropertyHandle> RenderTargetHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMediaFrameworkCaptureRenderTargetCameraOutputInfo, RenderTarget));
		RenderTargetHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FMediaFrameworkRenderTargetOutputInfoCustomization::PropertyValueChange));
		ChildBuilder.AddProperty(RenderTargetHandle.ToSharedRef());

		// Manually add all capture options properties to avoid an additional property group getting added
		TSharedPtr<IPropertyHandle> CaptureOptionsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMediaFrameworkCaptureRenderTargetCameraOutputInfo, CaptureOptions));
		uint32 NumChildren;
		CaptureOptionsHandle->GetNumChildren(NumChildren);
		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = CaptureOptionsHandle->GetChildHandle(Index);
			ChildHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FMediaFrameworkRenderTargetOutputInfoCustomization::PropertyValueChange));
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}

	void PropertyValueChange(const FPropertyChangedEvent& InPropertyChangedEvent)
	{
		if (UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings())
		{
			MediaCaptureSettings->SaveConfig();
			FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(MediaCaptureSettings, const_cast<FPropertyChangedEvent&>(InPropertyChangedEvent));
		}
	}
};

FMediaProfileMediaOutputDetailCustomization::~FMediaProfileMediaOutputDetailCustomization()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

void FMediaProfileMediaOutputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FMediaProfileMediaItemDetailCustomization::CustomizeDetails(DetailBuilder);
	
	// Hack to avoid the details builder separating out categories that only have advanced properties in them and putting them at the bottom.
	// If there is any sort function provided, the builder lumps all categories together and sorts via their sort order. We don't need to manually
	// adjust the sort order, but we do need the details builder to lump all categories together, hence the empty sort function
	DetailBuilder.SortCategories([](const TMap<FName, IDetailCategoryBuilder*>& CategoryMap) { });
	
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FMediaProfileMediaOutputDetailCustomization::OnObjectPropertyChanged);
	
	TArray<TWeakObjectPtr<UObject>> Objects;
	CachedDetailBuilder.Pin()->GetObjectsBeingCustomized(Objects);

	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(
		FMediaFrameworkCaptureCurrentViewportOutputInfo::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FMediaFrameworkCurrentViewportOutputInfoCustomization>();
		}));
	
	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(
		FMediaFrameworkCaptureCameraViewportCameraOutputInfo::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FMediaFrameworkViewportOutputInfoCustomization>();
		}));

	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(
		FMediaFrameworkCaptureRenderTargetCameraOutputInfo::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([]
		{
			return MakeShared<FMediaFrameworkRenderTargetOutputInfoCustomization>();
		}));
	
	// For media outputs, add the corresponding capture info to the details panel
	const FName CaptureCategoryName = TEXT("MediaCaptureCategory");
	IDetailCategoryBuilder& MediaCaptureCategory = DetailBuilder.EditCategory(
		CaptureCategoryName,
		LOCTEXT("MediaCaptureCategoryLabel", "Capture"),
		ECategoryPriority::Important);

	RegisterCaptureSection(FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor"), CaptureCategoryName);
		
	MediaCaptureCategory
		.AddCustomRow(LOCTEXT("MediaCaptureMethodFilter", "Capture Method"))
        .Visibility(TAttribute<EVisibility>::CreateSP(this, &FMediaProfileMediaOutputDetailCustomization::GetValidObjectVisibility))
        .NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MediaCaptureMethodLabel", "Capture Method"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(125.f)
		.MaxDesiredWidth(250.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SMediaCaptureMethodComboButton, Objects)
				.OnMethodChanged(this, &FMediaProfileMediaOutputDetailCustomization::OnCaptureMethodChanged)
			]
		];

	TArray<TSharedPtr<FStructOnScope>> CaptureSettingStructs;
	bool bHasCurrentViewportCapture = false;
	bool bHasViewportCaptures = false;
	bool bHasRenderTargetCaptures = false;
	if (UMediaProfileEditorCaptureSettings* MediaCaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings())
	{
		for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
		{
			TStrongObjectPtr<UObject> Object = WeakObject.Pin();			
			UMediaOutput* MediaOutput = Object.IsValid() ? Cast<UMediaOutput>(Object.Get()) : nullptr;
			if (!MediaOutput)
			{
				continue;
			}

			if (MediaOutput->IsA<UDummyMediaOutput>())
			{
				continue;
			}

			if (MediaCaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput)
			{
				TSharedPtr<FStructOnScope> Struct = MakeShared<FStructOnScope>(
					FMediaFrameworkCaptureCurrentViewportOutputInfo::StaticStruct(),
					reinterpret_cast<uint8*>(&MediaCaptureSettings->CurrentViewportMediaOutput));
				Struct->SetPackage(MediaCaptureSettings->GetPackage());
					
				CaptureSettingStructs.Add(Struct);
				bHasCurrentViewportCapture = true;
			}
			
			for (FMediaFrameworkCaptureCameraViewportCameraOutputInfo& ViewportOutputInfo : MediaCaptureSettings->ViewportCaptures)
			{
				if (ViewportOutputInfo.MediaOutput == MediaOutput)
				{
					TSharedPtr<FStructOnScope> Struct = MakeShared<FStructOnScope>(FMediaFrameworkCaptureCameraViewportCameraOutputInfo::StaticStruct(), reinterpret_cast<uint8*>(&ViewportOutputInfo));
					Struct->SetPackage(MediaCaptureSettings->GetPackage());
					
					CaptureSettingStructs.Add(Struct);
					bHasViewportCaptures = true;
				}
			}

			for (FMediaFrameworkCaptureRenderTargetCameraOutputInfo& RenderTargetOutputInfo : MediaCaptureSettings->RenderTargetCaptures)
			{
				if (RenderTargetOutputInfo.MediaOutput == MediaOutput)
				{
					TSharedPtr<FStructOnScope> Struct = MakeShared<FStructOnScope>(FMediaFrameworkCaptureRenderTargetCameraOutputInfo::StaticStruct(), reinterpret_cast<uint8*>(&RenderTargetOutputInfo));
					Struct->SetPackage(MediaCaptureSettings->GetPackage());
					
					CaptureSettingStructs.Add(Struct);
					bHasRenderTargetCaptures = true;
				}
			}
		}
	}

	// Only show the capture settings if all objects have capture settings of the same type (current viewport, media viewport, or render target)
	const bool bHasMultipleValues = (bHasCurrentViewportCapture && (bHasViewportCaptures || bHasRenderTargetCaptures)) || (bHasViewportCaptures && bHasRenderTargetCaptures);
	if (CaptureSettingStructs.Num() && !bHasMultipleValues)
	{
		FAddPropertyParams AddPropertyParams;
		AddPropertyParams.HideRootObjectNode(true);
			
		MediaCaptureStruct = MakeShared<FStructOnScopeStructureDataProvider>(CaptureSettingStructs);
		MediaCaptureCategory.AddExternalStructureProperty(MediaCaptureStruct, NAME_None, EPropertyLocation::Default, AddPropertyParams);
	}
}

FText FMediaProfileMediaOutputDetailCustomization::GetMediaItemTypeText() const
{
	return LOCTEXT("MediaOutputTypeText", "Media Output");
}

void FMediaProfileMediaOutputDetailCustomization::RegisterMediaTypeSection(FPropertyEditorModule& PropertyModule, const FName& MediaTypeCategory)
{
	static bool bRegistered = false;
	if (!bRegistered)
	{
		const TSharedPtr<FPropertySection> Section = PropertyModule.FindOrCreateSection(
			UMediaOutput::StaticClass()->GetFName(),
			TEXT("MediaOutputSection"),
			GetMediaItemTypeText());

		Section->AddCategory(MediaTypeCategory);
		bRegistered = true;
	}
}

FText FMediaProfileMediaOutputDetailCustomization::GetMediaObjectLabel(UMediaOutput* InMediaObject) const
{
	if (!MediaProfile.IsValid())
	{
		return FText::GetEmpty();
	}

	int32 Index = MediaProfile->FindMediaOutputIndex(InMediaObject);
	if (InMediaObject->IsA<UDummyMediaOutput>())
	{
		Index = Cast<UDummyMediaOutput>(InMediaObject)->MediaProfileIndex;
	}
		
	return FText::FromString(MediaProfile->GetLabelForMediaOutput(Index));
}

void FMediaProfileMediaOutputDetailCustomization::SetMediaObjectLabel(UMediaOutput* InMediaObject, const FText& InLabel)
{
	if (!MediaProfile.IsValid())
	{
		return;
	}
	
	int32 Index = MediaProfile->FindMediaOutputIndex(InMediaObject);
	if (InMediaObject->IsA<UDummyMediaOutput>())
	{
		Index = Cast<UDummyMediaOutput>(InMediaObject)->MediaProfileIndex;
	}
		
	MediaProfile->SetLabelForMediaOutput(Index, InLabel.ToString());
}

void FMediaProfileMediaOutputDetailCustomization::SetMediaObject(UMediaOutput* InOriginalMediaObject, UMediaOutput* InNewMediaObject)
{
	TSharedPtr<FMediaProfileEditor> PinnedMediaProfileEditor = MediaProfileEditor.Pin();
	if (!PinnedMediaProfileEditor.IsValid())
	{
		return;
	}

	TStrongObjectPtr<UMediaProfile> PinnedMediaProfile = MediaProfile.Pin();
	if (!PinnedMediaProfile.IsValid())
	{
		return;
	}
	
	int32 Index = PinnedMediaProfile->FindMediaOutputIndex(InOriginalMediaObject);
	if (InOriginalMediaObject->IsA<UDummyMediaOutput>())
	{
		Index = Cast<UDummyMediaOutput>(InOriginalMediaObject)->MediaProfileIndex;
	}

	PinnedMediaProfile->GetPlaybackManager()->CloseOutputFromIndex(Index);
	
	UMediaProfileEditorCaptureSettings* CaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
	if (!CaptureSettings)
	{
		return;
	}

	bool bHasExistingCaptureInfo = false;
	if (CaptureSettings->CurrentViewportMediaOutput.MediaOutput == InOriginalMediaObject)
	{
		CaptureSettings->CurrentViewportMediaOutput.MediaOutput = InNewMediaObject;
		bHasExistingCaptureInfo = true;
	}
	
	for (FMediaFrameworkCaptureCameraViewportCameraOutputInfo& ViewportOutputInfo : CaptureSettings->ViewportCaptures)
	{
		if (ViewportOutputInfo.MediaOutput != InOriginalMediaObject)
		{
			continue;
		}

		ViewportOutputInfo.MediaOutput = InNewMediaObject;
		bHasExistingCaptureInfo = true;
	}

	for (FMediaFrameworkCaptureRenderTargetCameraOutputInfo& RenderTargetOutputInfo : CaptureSettings->RenderTargetCaptures)
	{
		if (RenderTargetOutputInfo.MediaOutput != InOriginalMediaObject)
		{
			continue;
		}

		RenderTargetOutputInfo.MediaOutput = InNewMediaObject;
		bHasExistingCaptureInfo = true;
	}

	if (!bHasExistingCaptureInfo)
	{
		CaptureSettings->CurrentViewportMediaOutput.MediaOutput = InNewMediaObject;
	}
	
	CaptureSettings->SaveConfig();
	PinnedMediaProfile->SetMediaOutput(Index, InNewMediaObject);
}

void FMediaProfileMediaOutputDetailCustomization::RegisterCaptureSection(FPropertyEditorModule& PropertyModule, const FName& CaptureCategory)
{
	static bool bRegistered = false;
	if (!bRegistered)
	{
		const TSharedPtr<FPropertySection> Section = PropertyModule.FindOrCreateSection(
			UMediaOutput::StaticClass()->GetFName(),
			TEXT("CaptureSection"),
			LOCTEXT("CaptureSection", "Capture"));

		Section->AddCategory(CaptureCategory);
		bRegistered = true;
	}
}

void FMediaProfileMediaOutputDetailCustomization::OnCaptureMethodChanged()
{
	TSharedPtr<FMediaProfileEditor> PinnedMediaProfileEditor = MediaProfileEditor.Pin();
	if (!PinnedMediaProfileEditor.IsValid())
	{
		return;
	}

	TStrongObjectPtr<UMediaProfile> PinnedMediaProfile = MediaProfile.Pin();
	if (!PinnedMediaProfile.IsValid())
	{
		return;
	}
	
	TArray<TWeakObjectPtr<UObject>> Objects;
	CachedDetailBuilder.Pin()->GetObjectsBeingCustomized(Objects);

	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		TStrongObjectPtr<UObject> PinnedObject = Object.Pin();
		if (!PinnedObject.IsValid())
		{
			continue;
		}

		if (!PinnedObject->IsA<UMediaOutput>())
		{
			continue;
		}

		if (PinnedObject->IsA<UDummyMediaOutput>())
		{
			continue;
		}

		UMediaOutput* MediaOutput = Cast<UMediaOutput>(PinnedObject.Get());
		PinnedMediaProfile->GetPlaybackManager()->CloseOutput(MediaOutput);
		PinnedMediaProfileEditor->GetOnCaptureMethodChanged().Broadcast(MediaOutput);
	}
	
	CachedDetailBuilder.Pin()->ForceRefreshDetails();
}

void FMediaProfileMediaOutputDetailCustomization::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (UMediaProfileEditorCaptureSettings* CaptureSettings = Cast<UMediaProfileEditorCaptureSettings>(InObject))
	{
		if (CaptureSettings != FMediaProfileEditor::GetMediaFrameworkCaptureSettings())
		{
			return;
		}
		
		const FName PropertyName = InPropertyChangedEvent.GetPropertyName();
		
		// Refresh the details panel if the MediaOutput property on any captures or the viewport or render target captures lists have changed,
		// as the capture properties may now be invalid
		if (PropertyName == TEXT("MediaOutput") ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UMediaFrameworkWorldSettingsAssetUserData, ViewportCaptures) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UMediaFrameworkWorldSettingsAssetUserData, RenderTargetCaptures))
		{
			CachedDetailBuilder.Pin()->ForceRefreshDetails();
		}
	}
}

#undef LOCTEXT_NAMESPACE
