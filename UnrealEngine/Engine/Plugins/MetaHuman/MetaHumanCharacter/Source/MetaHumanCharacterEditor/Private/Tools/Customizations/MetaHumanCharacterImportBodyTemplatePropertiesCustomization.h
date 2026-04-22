// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"

/**
 * Detail Customization for the UMetaHumanCharacterImportDNAProperties class, which is 
 * used for importing from template
 */
class FMetaHumanCharacterImportBodyTemplatePropertiesCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~Begin IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~End IDetailCustomization interface
};
