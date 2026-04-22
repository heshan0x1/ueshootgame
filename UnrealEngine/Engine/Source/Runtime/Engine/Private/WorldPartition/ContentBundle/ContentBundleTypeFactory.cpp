// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundleTypeFactory.h"

#include "WorldPartition/ContentBundle/ContentBundleClient.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentBundleTypeFactory)

TSharedPtr<FContentBundleClient> UContentBundleTypeFactory::CreateClient(const UContentBundleDescriptor* Descriptor, const FString& ClientDisplayName)
{
	return MakeShared<FContentBundleClient>(Descriptor, ClientDisplayName);
}
