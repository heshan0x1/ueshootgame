// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectionSetsSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SelectionSetsSettings)

USelectionSetsSettings::USelectionSetsSettings()
{
	CategoryName = TEXT("General");
	SectionName = TEXT("Animation Selection Sets");

	if (CustomColors.Num() == 0)
	{
		CustomColors.Add(FLinearColor(.904, .323, .539)); //pastel red
		CustomColors.Add(FLinearColor(.552, .737, .328)); //pastel green
		CustomColors.Add(FLinearColor(.947, .418, .219)); //pastel orange
		CustomColors.Add(FLinearColor(.156, .624, .921)); //pastel blue
		CustomColors.Add(FLinearColor(.921, .314, .337)); //pastel red 2
		CustomColors.Add(FLinearColor(.361, .651, .332)); //pastel green 2
		CustomColors.Add(FLinearColor(.982, .565, .254)); //pastel orange 2
		CustomColors.Add(FLinearColor(.246, .223, .514)); //pastel purple
		CustomColors.Add(FLinearColor(.208, .386, .687)); //pastel blue2
		CustomColors.Add(FLinearColor(.223, .590, .337)); //pastel green 3
		CustomColors.Add(FLinearColor(.230, .291, .591)); //pastel blue 3
	}
}

