// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"

struct FMetaHumanAssetDescription;

namespace UE::MetaHuman
{
class SAssetGroupItemDetails;

DECLARE_DELEGATE(FOnVerify);
DECLARE_DELEGATE(FOnPackage);

// Widget to display details of an AssetGroup: name, icon, contents, verification report etc.
class SAssetGroupItemView final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetGroupItemView)
		{
		}
		SLATE_EVENT(FOnVerify, OnVerify)
		SLATE_EVENT(FOnPackage, OnPackage)
		SLATE_ATTRIBUTE(bool, EnablePackageButton)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetItem(TSharedRef<FMetaHumanAssetDescription> AssetDescription);

private:
	// UI Handlers
	FReply OnVerify() const;
	FReply OnPackage() const;

	// Data
	TSharedPtr<SAssetGroupItemDetails> ItemDetails;

	//Callbacks
	FOnVerify OnVerifyCallback;
	FOnPackage OnPackageCallback;
};
}
