// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorParametricView.h"

#include "MetaHumanCharacterEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorParametricView"

template<typename NumericType>
struct TParametricTypeInterface : TDefaultNumericTypeInterface<NumericType>
{
	TParametricTypeInterface(TAttribute<NumericType> InMinValue, TAttribute<NumericType> InMaxValue, float InSliderDistance)
		: MinValue(InMinValue), MaxValue(InMaxValue), SliderDistance(InSliderDistance)
	{
	}

	/** Convert the type to/from a string */
	virtual FString ToString(const NumericType& SliderValue) const override
	{
		float Fraction = SliderValue / SliderDistance;
		NumericType OutValue = Fraction * (MaxValue.Get() - MinValue.Get()) + MinValue.Get();
		return TDefaultNumericTypeInterface<NumericType>::ToString(OutValue);
	}

	virtual TOptional<NumericType> FromString(const FString& InString, const NumericType& InExistingValue) override
	{
		TOptional<NumericType> Result = TDefaultNumericTypeInterface<NumericType>::FromString(InString, InExistingValue);
		if (Result.IsSet())
		{
			float Fraction = FMath::GetRangePct(FVector2f(MinValue.Get(), MaxValue.Get()), Result.GetValue());
			Result = static_cast<NumericType>(Fraction * SliderDistance);
		}
		return Result;
	}

	TAttribute<NumericType> MinValue;
	TAttribute<NumericType> MaxValue;
	float SliderDistance = 100.f;
};

template<typename NumericType>
void SMetaHumanCharacterEditorParametricSpinBox<NumericType>::Construct(const FArguments& InArgs)
{
	ValueAttribute = InArgs._Value;
	OnValueChanged = InArgs._OnValueChanged;
	OnGetDisplayValue = InArgs._OnGetDisplayValue;

	MinValue = InArgs._MinValue;
	MaxValue = InArgs._MaxValue;

	InterfaceAttr = MakeShared<TParametricTypeInterface<NumericType>>(MinValue, MaxValue, SliderDistance);

	ChildSlot
	[
		SAssignNew(SpinBox, SSpinBox<float>)
		.Style(InArgs._SpinBoxStyle)
		.Font(InArgs._Font)
		.Value(this, &SMetaHumanCharacterEditorParametricSpinBox::GetSliderValue)
		.OnValueChanged(this, &SMetaHumanCharacterEditorParametricSpinBox::OnSliderValueChanged, false)
		.OnValueCommitted(this, &SMetaHumanCharacterEditorParametricSpinBox::OnSliderValueCommitted)
		.MaxFractionalDigits(2)
		.MaxValue(SliderMaxValue)
		.MinValue(SliderMinValue)
		.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
		.OnGetDisplayValue(this, &SMetaHumanCharacterEditorParametricSpinBox::GetDisplayText)
		.ToolTipText(InArgs._ToolTip)
		.TypeInterface(InterfaceAttr)
		.IsEnabled(InArgs._IsEnabled)
	];
}

template<typename NumericType>
float SMetaHumanCharacterEditorParametricSpinBox<NumericType>::GetSliderValue() const
{
	float Fraction = FMath::GetRangePct(FVector2f(MinValue.Get(), MaxValue.Get()), ValueAttribute.Get());
	return FMath::Clamp(Fraction * SliderDistance, SliderMinValue, SliderMaxValue);
}

template<typename NumericType>
void SMetaHumanCharacterEditorParametricSpinBox<NumericType>::OnSliderValueChanged(float NewValue, bool bCommit) const
{
	float PrevValue = SpinBox->GetValue();
	if (FMath::Abs(NewValue- PrevValue) > UE_KINDA_SMALL_NUMBER || bCommit)
	{
		float Fraction = NewValue / SliderDistance;
		NumericType OutValue = Fraction * (MaxValue.Get() - MinValue.Get()) + MinValue.Get();
		OnValueChanged.ExecuteIfBound(OutValue, bCommit);
	}
}

template<typename NumericType>
void SMetaHumanCharacterEditorParametricSpinBox<NumericType>::OnSliderValueCommitted(float NewValue, ETextCommit::Type CommitType) const
{
	OnSliderValueChanged(NewValue, true);
}

template<typename NumericType>
FText SMetaHumanCharacterEditorParametricSpinBox<NumericType>::GetOutputValueText() const
{
	return FText::AsNumber(ValueAttribute.Get());
}

template<typename NumericType>
TOptional<FText> SMetaHumanCharacterEditorParametricSpinBox<NumericType>::GetDisplayText(float Value) const
{
	float Fraction = Value / SliderDistance;
	NumericType OutValue = Fraction * (MaxValue.Get() - MinValue.Get()) + MinValue.Get();

	if (OnGetDisplayValue.IsBound())
	{
		return OnGetDisplayValue.Execute(OutValue);
	}

	return FText::AsNumber(OutValue);
}

void SMetaHumanCharacterEditorParametricConstraintView::Construct(const FArguments& InArgs)
{
	ConstraintName = InArgs._ConstraintName;
	TargetMeasurement = InArgs._TargetMeasurement;
	ActualMeasurement = InArgs._ActualMeasurement;
	IsPinned = InArgs._IsPinned;
	OnBeginConstraintEditingDelegate = InArgs._OnBeginConstraintEditing;
	OnParametricConstraintChangedDelegate = InArgs._OnParametricConstraintChanged;

	ChildSlot
		[
			SNew(SHorizontalBox)
			.ToolTipText(InArgs._ToolTip)
			
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(.2f)
			.Padding(10.f, 0.f)
			[
				SNew(STextBlock)
				.Text(InArgs._Label)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.FillWidth(.8f)
			.Padding(3.f, 0.f)
			[
				SNew(SMetaHumanCharacterEditorParametricSpinBox<float>)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.MinValue(InArgs._MinValue)
				.MaxValue(InArgs._MaxValue)
				.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
				.Value(this, &SMetaHumanCharacterEditorParametricConstraintView::GetParametricValue)
				.OnBeginSliderMovement(this, &SMetaHumanCharacterEditorParametricConstraintView::OnBeginConstraintEditing)
				.OnValueChanged(this, &SMetaHumanCharacterEditorParametricConstraintView::OnConstraintTargetChanged)
				.OnEndSliderMovement(this, &SMetaHumanCharacterEditorParametricConstraintView::OnConstraintTargetChanged)
				.OnGetDisplayValue(this, &SMetaHumanCharacterEditorParametricConstraintView::GetDisplayText)
				.IsEnabled(InArgs._IsEnabled)
			]
		
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.f, 2.f)
			[
				SNew(SCheckBox)
				.Style(FMetaHumanCharacterEditorStyle::Get(), TEXT("MetaHumanCharacterEditorTools.ParametricBody.CheckBox"))
				.Visibility(InArgs._PinVisibility)
				.IsChecked(this, &SMetaHumanCharacterEditorParametricConstraintView::GetConstraintChecked)
				.OnCheckStateChanged(this, &SMetaHumanCharacterEditorParametricConstraintView::OnConstraintPinnedChanged)
			]
		];
}

TOptional<FText> SMetaHumanCharacterEditorParametricConstraintView::GetDisplayText(float TargetValue) const
{
	FText DisplayText;

	FNumberFormattingOptions FormatOptions;
	FormatOptions.MinimumIntegralDigits = 1;
	FormatOptions.MaximumFractionalDigits = 2;
	FormatOptions.MinimumFractionalDigits = 2;

	if (IsPinned.Get() && ActualMeasurement.IsSet())
	{
		FText TargetValueText = FText::AsNumber(TargetValue, &FormatOptions);
		FText ActualValueText = FText::AsNumber(ActualMeasurement.Get(), &FormatOptions);
		DisplayText = FText::Format(LOCTEXT("ParametricConstraintValueDisplay", "{0} ({1} actual)"), TargetValue, ActualValueText);
	}
	else if(ActualMeasurement.IsSet())
	{
		DisplayText = FText::AsNumber(ActualMeasurement.Get(), &FormatOptions);
	}
	else
	{
		DisplayText = FText::AsNumber(TargetValue, &FormatOptions);
	}

	return DisplayText;
}

float SMetaHumanCharacterEditorParametricConstraintView::GetParametricValue() const
{
	float MeasurementValue = IsPinned.Get() ? TargetMeasurement.Get() : ActualMeasurement.Get();
	return MeasurementValue;
}

void SMetaHumanCharacterEditorParametricConstraintView::OnBeginConstraintEditing() const
{
	OnBeginConstraintEditingDelegate.ExecuteIfBound();
}

void SMetaHumanCharacterEditorParametricConstraintView::OnConstraintTargetChanged(const float Value, bool bCommit) const
{
	const bool bIsPinned = true;
	OnParametricConstraintChangedDelegate.ExecuteIfBound(Value, bIsPinned, bCommit);
}

ECheckBoxState SMetaHumanCharacterEditorParametricConstraintView::GetConstraintChecked() const
{
	return IsPinned.Get() ? (ECheckBoxState::Checked) : ECheckBoxState::Unchecked;
}

void SMetaHumanCharacterEditorParametricConstraintView::OnConstraintPinnedChanged(ECheckBoxState CheckState) const
{
	bool bIsChecked = CheckState == ECheckBoxState::Checked;
	const bool bCommit = true;
	OnParametricConstraintChangedDelegate.ExecuteIfBound(TargetMeasurement.Get(), bIsChecked, bCommit);
}

void SMetaHumanCharacterEditorParametricConstraintsPanel::Construct(const FArguments& InArgs)
{
	if (InArgs._ListItemsSource)
	{
		ItemsSource = *InArgs._ListItemsSource;
	}

	OnBeginConstraintEditingDelegate = InArgs._OnBeginConstraintEditing;
	OnConstraintsChangedDelegate = InArgs._OnConstraintsChanged;
	DiagnosticView = InArgs._DiagnosticsView;

	ChildSlot
	[
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(InArgs._Label)
		.Padding(InArgs._Padding)
		.Content()
		[
			SAssignNew(ListView, SListView<FMetaHumanCharacterBodyConstraintItemPtr>)
			.ListItemsSource(&ItemsSource)
			.SelectionMode(ESelectionMode::None)
			.ListViewStyle(FMetaHumanCharacterEditorStyle::Get(), TEXT("MetaHumanCharacterEditorTools.ParametricBody.TableView"))
			.OnGenerateRow(this, &SMetaHumanCharacterEditorParametricConstraintsPanel::MakeConstraintRowWidget)
		]
		.HeaderContent()
		[
			SNew(SCheckBox)
			.Style(FMetaHumanCharacterEditorStyle::Get(), TEXT("MetaHumanCharacterEditorTools.ParametricBody.CheckBox"))
			.Visibility(DiagnosticView ? EVisibility::Hidden : EVisibility::Visible)
			.OnCheckStateChanged(this, &SMetaHumanCharacterEditorParametricConstraintsPanel::OnGroupPinCheckStateChanged)
			.IsChecked(this, &SMetaHumanCharacterEditorParametricConstraintsPanel::GetGroupPinCheckState)
		]
	];
}

static FText GetLabelForConstraintName(const FName& ConstraintName)
{
	FText LabelText;
	if (ConstraintName == "Masculine/Feminine")
	{
		LabelText = LOCTEXT("Masculine/FeminineLabelText", "Masculine/Feminine");
	}
	else if (ConstraintName == "Muscularity")
	{
		LabelText = LOCTEXT("MuscularityLabelText", "Muscularity");
	}
	else if (ConstraintName == "Fat")
	{
		LabelText = LOCTEXT("FatLabelText", "Fat");
	}
	else if (ConstraintName == "Height")
	{
		LabelText = LOCTEXT("HeightLabelText", "Height");
	}
	else if (ConstraintName == "Across Shoulder")
	{
		LabelText = LOCTEXT("AcrossShoulderLabelText", "Across Shoulder");
	}
	else if (ConstraintName == "Shoulder to Apex")
	{
		LabelText = LOCTEXT("ShoulderToApexLabelText", "Shoulder to Apex");
	}
	else if (ConstraintName == "Front Interscye Length")
	{
		LabelText = LOCTEXT("FrontInterscyeLengthLabelText", "Front Interscye Length");
	}
	else if (ConstraintName == "Chest")
	{
		LabelText = LOCTEXT("ChestLabelText", "Chest");
	}
	else if (ConstraintName == "Bust")
	{
		LabelText = LOCTEXT("BustLabelText", "Bust");
	}
	else if (ConstraintName == "Bust Span")
	{
		LabelText = LOCTEXT("BustSpanLabelText", "Bust Span");
	}
	else if (ConstraintName == "Underbust")
	{
		LabelText = LOCTEXT("UnderbustLabelText", "Underbust");
	}
	else if (ConstraintName == "Neck to Waist")
	{
		LabelText = LOCTEXT("NeckToWaistLabelText", "Neck to Waist");
	}
	else if (ConstraintName == "Waist")
	{
		LabelText = LOCTEXT("WaistLabelText", "Waist");
	}
	else if (ConstraintName == "High Hip")
	{
		LabelText = LOCTEXT("HighHipLabelText", "High Hip");
	}
	else if (ConstraintName == "Hip")
	{
		LabelText = LOCTEXT("HipLabelText", "Hip");
	}
	else if (ConstraintName == "Neck")
	{
		LabelText = LOCTEXT("NeckLabelText", "Neck");
	}
	else if (ConstraintName == "Neck Base")
	{
		LabelText = LOCTEXT("NeckBaseLabelText", "Neck Base");
	}
	else if (ConstraintName == "Neck Length")
	{
		LabelText = LOCTEXT("NeckLengthLabelText", "Neck Length");
	}
	else if (ConstraintName == "Upper Arm Length")
	{
		LabelText = LOCTEXT("UpperArmLengthLabelText", "Upper Arm Length");
	}
	else if (ConstraintName == "Lower Arm Length")
	{
		LabelText = LOCTEXT("LowerArmLengthLabelText", "Lower Arm Length");
	}
	else if (ConstraintName == "Forearm")
	{
		LabelText = LOCTEXT("ForearmLabelText", "Forearm");
	}
	else if (ConstraintName == "Bicep")
	{
		LabelText = LOCTEXT("BicepLabelText", "Bicep");
	}
	else if (ConstraintName == "Elbow")
	{
		LabelText = LOCTEXT("ElbowLabelText", "Elbow");
	}
	else if (ConstraintName == "Wrist")
	{
		LabelText = LOCTEXT("WristLabelText", "Wrist");
	}
	else if (ConstraintName == "Inseam")
	{
		LabelText = LOCTEXT("InseamLabelText", "Inseam");
	}
	else if (ConstraintName == "Thigh")
	{
		LabelText = LOCTEXT("ThighLabelText", "Thigh");
	}
	else if (ConstraintName == "Knee")
	{
		LabelText = LOCTEXT("KneeLabelText", "Knee");
	}
	else if (ConstraintName == "Calf")
	{
		LabelText = LOCTEXT("CalfLabelText", "Calf");
	}
	else if (ConstraintName == "Shoulder Height")
	{
		LabelText = LOCTEXT("ShoulderHeightLabelText", "Shoulder Height");
	}
	else if (ConstraintName == "Rise")
	{
		LabelText = LOCTEXT("RiseLabelText", "Rise");
	}
	else
	{
		// Default to constraint name
		LabelText = FText::FromName(ConstraintName);
	}

	return LabelText;
}

static FText GetToolTipForConstraintName(const FName& ConstraintName)
{
	FText ToolTipText;
	if (ConstraintName == "Masculine/Feminine")
	{
		ToolTipText = LOCTEXT("Masculine/FeminineToolTipText", "Broadly define masculine or feminine traits");
	}
	else if (ConstraintName == "Muscularity")
	{
		ToolTipText = LOCTEXT("MuscularityToolTipText", "Makes changes to global muscle mass");
	}
	else if (ConstraintName == "Fat")
	{
		ToolTipText = LOCTEXT("FatToolTipText", "Makes changes to global fat mass");
	}
	else if (ConstraintName == "Height")
	{
		ToolTipText = LOCTEXT("HeightToolTipText", "Specify height (cm)");
	}
	else if (ConstraintName == "Across Shoulder")
	{
		ToolTipText = LOCTEXT("AcrossShoulderToolTipText", "Specify shoulder width (cm). When used in conjunction with Front Interscye, can help define shoulder shaping.");
	}
	else if (ConstraintName == "Shoulder to Apex")
	{
		ToolTipText = LOCTEXT("ShoulderToApexToolTipText", "Specify shoulder to apex (cm). Effects chest shaping.");
	}
	else if (ConstraintName == "Front Interscye Length")
	{
		ToolTipText = LOCTEXT("FrontInterscyeLengthToolTipText", "Specify front interscye width (cm). When used in conjunction with Across Shoulders, can help define chest shaping.");
	}
	else if (ConstraintName == "Bust " || ConstraintName == "Chest")
	{
		ToolTipText = LOCTEXT("ChestToolTipText", "Specify chest/bust circumference (cm). When used in conjunction with Underbust helps separate back and cup measurements.");
	}
	else if (ConstraintName == "Bust Span")
	{
		ToolTipText = LOCTEXT("BustSpanToolTipText", "Specify bust span (cm)");
	}
	else if (ConstraintName == "Underbust")
	{
		ToolTipText = LOCTEXT("UnderbustToolTipText", "Specify underbust circumference (cm). When used in conjunction with Bust helps separate back and cup measurements.");
	}
	else if (ConstraintName == "Neck to Waist")
	{
		ToolTipText = LOCTEXT("NeckToWaistToolTipText", "Specify neck to waist length (cm).");
	}
	else if (ConstraintName == "Waist")
	{
		ToolTipText = LOCTEXT("WaistToolTipText", "Specify waist circumference (cm)");
	}
	else if (ConstraintName == "High Hip")
	{
		ToolTipText = LOCTEXT("HighHipToolTipText", "Specify high hip circumference (cm). Useful as a shaping modifier in conjunction with Hip.");
	}
	else if (ConstraintName == "Hip")
	{
		ToolTipText = LOCTEXT("HipToolTipText", "Specify hip circumference (cm)");
	}
	else if (ConstraintName == "Neck")
	{
		ToolTipText = LOCTEXT("NeckToolTipText", "Specify neck circumference (cm)");
	}
	else if (ConstraintName == "Neck Base")
	{
		ToolTipText = LOCTEXT("NeckBaseToolTipText", "Specify neck base circumference (cm)");
	}
	else if (ConstraintName == "Neck Length")
	{
		ToolTipText = LOCTEXT("NeckLengthToolTipText", "Specify neck length (cm)");
	}
	else if (ConstraintName == "Upper Arm Length")
	{
		ToolTipText = LOCTEXT("UpperArmLengthToolTipText", "Specify upper arm length (cm)");
	}
	else if (ConstraintName == "Lower Arm Length")
	{
		ToolTipText = LOCTEXT("LowerArmLengthToolTipText", "Specify lower arm length (cm)");
	}
	else if (ConstraintName == "Forearm")
	{
		ToolTipText = LOCTEXT("ForearmToolTipText", "Specify forearm circumference (cm)");
	}
	else if (ConstraintName == "Bicep")
	{
		ToolTipText = LOCTEXT("BicepToolTipText", "Specify bicep circumference (cm)");
	}
	else if (ConstraintName == "Elbow")
	{
		ToolTipText = LOCTEXT("ElbowToolTipText", "Specify elbow circumference (cm)");
	}
	else if (ConstraintName == "Wrist")
	{
		ToolTipText = LOCTEXT("WristToolTipText", "Specify wrist circumference (cm)");
	}
	else if (ConstraintName == "Inseam")
	{
		ToolTipText = LOCTEXT("InseamToolTipText", "Specify floor to crotch length (cm). When used in conjunction with Height, can be used to define upper/lower body height ratio.");
	}
	else if (ConstraintName == "Thigh")
	{
		ToolTipText = LOCTEXT("ThighToolTipText", "Specify thigh circumference (cm)");
	}
	else if (ConstraintName == "Knee")
	{
		ToolTipText = LOCTEXT("KneeToolTipText", "Specify knee circumference (cm)");
	}
	else if (ConstraintName == "Calf")
	{
		ToolTipText = LOCTEXT("CalfToolTipText", "Specify calf circumference (cm)");
	}
	else if (ConstraintName == "Shoulder Height")
	{
		ToolTipText = LOCTEXT("ShoulderHeightToolTipText", "Floor to shoulder height (read only)");
	}
	else if (ConstraintName == "Rise")
	{
		ToolTipText = LOCTEXT("RiseToolTipText", "Top of waistband in front, to top of waistband at the back (read only)");
	}
	else
	{
		// Default to constraint name
		ToolTipText = FText::FromName(ConstraintName);
	}

	return ToolTipText;
}

class SParametricConstraintTableRow
		: public STableRow<FMetaHumanCharacterBodyConstraintItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SParametricConstraintTableRow) { }

		SLATE_ARGUMENT(FMetaHumanCharacterBodyConstraintItemPtr, ConstraintItem)

		SLATE_ARGUMENT(EVisibility, PinVisibility)

		SLATE_ARGUMENT(bool, IsEnabled)

		SLATE_EVENT(FSimpleDelegate, OnBeginConstraintEditing)
		SLATE_EVENT(SMetaHumanCharacterEditorParametricConstraintsPanel::FOnConstraintChanged, OnConstraintsChanged)

		SLATE_STYLE_ARGUMENT( FTableRowStyle, Style )

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		ConstraintItem = InArgs._ConstraintItem;
		OnBeginConstraintEditingDelegate = InArgs._OnBeginConstraintEditing;
		OnConstraintsChangedDelegate = InArgs._OnConstraintsChanged;
		
		if (ConstraintItem.IsValid())
		{
			STableRow<FMetaHumanCharacterBodyConstraintItemPtr>::Construct(
			STableRow<FMetaHumanCharacterBodyConstraintItemPtr>::FArguments()
			.ShowSelection(false)
			.Style(InArgs._Style)
			.Content()
			[
				SNew(SMetaHumanCharacterEditorParametricConstraintView)
				.ConstraintName(ConstraintItem->Name)
				.Label(GetLabelForConstraintName(ConstraintItem->Name))
				.ToolTip(GetToolTipForConstraintName(ConstraintItem->Name))
				.PinVisibility(InArgs._PinVisibility)
				.IsEnabled(InArgs._IsEnabled)
				.MinValue_Lambda([this]()
				{
					return ConstraintItem->MinMeasurement;
				})
				.MaxValue_Lambda([this]()
				{
					return ConstraintItem->MaxMeasurement;
				})
				.TargetMeasurement_Lambda([this]()
				{
					return ConstraintItem->TargetMeasurement;
				})
				.OnBeginConstraintEditing_Lambda([this]()
				{
					OnBeginConstraintEditingDelegate.ExecuteIfBound();
				})
				.OnParametricConstraintChanged_Lambda([this](float NewValue, bool bIsPinned, bool bCommit)
				{
					ConstraintItem->TargetMeasurement = NewValue;
					ConstraintItem->bIsActive = bIsPinned;
					OnConstraintsChangedDelegate.ExecuteIfBound(bCommit);
				})
				.ActualMeasurement_Lambda([this]()
				{
					return ConstraintItem->ActualMeasurement;
				})
				.IsPinned_Lambda([this]()
				{
					return ConstraintItem->bIsActive;
				})
			], InOwnerTableView);
		}
	}
private:
	FMetaHumanCharacterBodyConstraintItemPtr ConstraintItem;
	FSimpleDelegate OnBeginConstraintEditingDelegate;
	SMetaHumanCharacterEditorParametricConstraintsPanel::FOnConstraintChanged OnConstraintsChangedDelegate;
	FTableRowStyle TransparentTableRowStyle;
};

TSharedRef<ITableRow> SMetaHumanCharacterEditorParametricConstraintsPanel::MakeConstraintRowWidget(FMetaHumanCharacterBodyConstraintItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SParametricConstraintTableRow, OwnerTable)
		.ConstraintItem(Item)
		.Style(FMetaHumanCharacterEditorStyle::Get(), TEXT("MetaHumanCharacterEditorTools.ParametricBody.TableRow"))
		.OnBeginConstraintEditing(this, &SMetaHumanCharacterEditorParametricConstraintsPanel::OnBeginConstraintEditing)
		.OnConstraintsChanged(this, &SMetaHumanCharacterEditorParametricConstraintsPanel::OnConstraintChanged)
		.PinVisibility(DiagnosticView ? EVisibility::Hidden : EVisibility::Visible)
		.IsEnabled(!DiagnosticView);
}

void SMetaHumanCharacterEditorParametricConstraintsPanel::OnGroupPinCheckStateChanged(ECheckBoxState CheckState)
{
	bool bGroupHasPinnedItems = false;
	bool bGroupHasUnpinnedItems = false;
	for (const FMetaHumanCharacterBodyConstraintItemPtr& Item : ItemsSource)
	{
		if (Item->bIsActive)
		{
			bGroupHasPinnedItems = true;
		}
		else
		{
			bGroupHasUnpinnedItems = true;
		}
	}

	// Pin group if checked or if current state has mix of pinned and unpinned always pin 
	bool bGroupActive = CheckState == ECheckBoxState::Checked;
	if (bGroupHasPinnedItems && bGroupHasUnpinnedItems)
	{
		bGroupActive = true;
	}

	for (const FMetaHumanCharacterBodyConstraintItemPtr& Item : ItemsSource)
	{
		Item->bIsActive = bGroupActive;
	}

	const bool bCommit = true;
	OnConstraintChanged(bCommit);
}

void SMetaHumanCharacterEditorParametricConstraintsPanel::OnBeginConstraintEditing()
{
	OnBeginConstraintEditingDelegate.ExecuteIfBound();
}

void SMetaHumanCharacterEditorParametricConstraintsPanel::OnConstraintChanged(bool bCommit)
{
	OnConstraintsChangedDelegate.ExecuteIfBound(bCommit);
}

ECheckBoxState SMetaHumanCharacterEditorParametricConstraintsPanel::GetGroupPinCheckState() const
{
	ECheckBoxState GroupPinState = ECheckBoxState::Unchecked;
	bool bAllPinned = true;
	for (const FMetaHumanCharacterBodyConstraintItemPtr& Item : ItemsSource)
	{
		if (Item->bIsActive)
		{
			GroupPinState = ECheckBoxState::Undetermined;
		}
		bAllPinned = bAllPinned & Item->bIsActive;
	}

	if (bAllPinned)
	{
		GroupPinState = ECheckBoxState::Checked;
	}

	return GroupPinState;
}

#undef LOCTEXT_NAMESPACE
