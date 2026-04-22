// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ILiveLinkSource.h"

#include "LiveLinkFaceSourceBlueprint.generated.h"



UCLASS(Blueprintable)
class LIVELINKFACESOURCE_API ULiveLinkFaceSourceBlueprint : public UBlueprintFunctionLibrary
{

public:

	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "Live Link Face")
	static void CreateLiveLinkFaceSource(FLiveLinkSourceHandle& LiveLinkFaceSource, bool& Succeeded);

	UFUNCTION(BlueprintCallable, Category = "Live Link Face")
	static void Connect(const FLiveLinkSourceHandle& LiveLinkFaceSource, const FString& SubjectName, const FString& Address, bool& Succeeded, int32 Port = 14785);
};
