// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Material.h"

#include "HAL/LowLevelMemTracker.h"
#include "MuR/MutableTrace.h"
#include "MuR/Serialisation.h"
#include "MuR/SerialisationPrivate.h"
#include "MuR/Image.h"


namespace UE::Mutable::Private
{
	void FMaterial::Serialise(const FMaterial* MaterialPtr, FOutputArchive& Arch)
	{
		Arch << *MaterialPtr;
	}


	TSharedPtr<FMaterial> FMaterial::StaticUnserialise(FInputArchive& Arch)
	{
		MUTABLE_CPUPROFILER_SCOPE(MaterialUnserialise)
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));

		TSharedPtr<FMaterial> Result = MakeShared<FMaterial>();
		Arch >> *Result;

		return Result;
	}


	void FMaterial::Serialise(FOutputArchive& Arch) const
	{
		Arch << ReferenceID;
		Arch << ImageParameters;
		Arch << ColorParameters;
		Arch << ScalarParameters;
	}


	void FMaterial::Unserialise(FInputArchive& Arch)
	{
		Arch >>ReferenceID;
		Arch >> ImageParameters;
		Arch >> ColorParameters;
		Arch >> ScalarParameters;
	}


	TSharedPtr<FMaterial> FMaterial::Clone() const
	{
		TSharedPtr<FMaterial> Result = MakeShared<FMaterial>();

		Result->Material = Material;
		Result->ReferenceID = ReferenceID;
		Result->ImageParameters = ImageParameters;
		Result->ColorParameters = ColorParameters;
		Result->ScalarParameters = ScalarParameters;

		return Result;
	}


	bool FMaterial::operator==(const FMaterial& Other) const
	{
		return ReferenceID == Other.ReferenceID && Material == Other.Material	&&
			ImageParameters == Other.ImageParameters &&	ColorParameters == Other.ColorParameters && ScalarParameters == Other.ScalarParameters;
	};
}
