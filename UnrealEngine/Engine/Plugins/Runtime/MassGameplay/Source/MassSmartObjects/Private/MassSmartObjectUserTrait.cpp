// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectUserTrait.h"

#include "MassSmartObjectFragments.h"
#include "MassEntityTemplateRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassSmartObjectUserTrait)

void UMassSmartObjectUserTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassSmartObjectUserFragment SmartObjectUser;
	SmartObjectUser.UserTags = UserTags;
	
	BuildContext.AddFragment(FConstStructView::Make(SmartObjectUser));
}
