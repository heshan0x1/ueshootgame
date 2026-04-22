// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"

#include "IpAddressDetailsCustomization.generated.h"

USTRUCT(BlueprintType, meta = (Deprecated = "5.7", DeprecationMessage = "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module"))
struct UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FDeviceAddress
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DeviceAddress)
	FString IpAddress;
};

class FIpAddressDetailsCustomization : public IPropertyTypeCustomization
{
public:
	FIpAddressDetailsCustomization();

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	FText OnGetDeviceAddress() const;
	bool OnDeviceAddressVerify(const FText& InText, FText& OutErrorText);
	void OnDeviceAddressCommited(const FText& InText, ETextCommit::Type CommitInfo);
	bool IsReadOnly() const;

	TSharedPtr<IPropertyHandle> DeviceAddressProperty;
	TRange<int32> IpAddressRange;
};
