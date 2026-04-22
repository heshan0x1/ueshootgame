// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuR/Operations.h"
#include "MuR/TVariant.h"

#include "UObject/StrongObjectPtr.h"
#include "Materials/MaterialInterface.h"

#define UE_API MUTABLERUNTIME_API

namespace UE::Mutable::Private
{
	class FImage;

    /** Material type resource */
	class FMaterial : public FResource
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		//! Serialisation
		static UE_API void Serialise(const FMaterial* MaterialPtr, FOutputArchive& Arch);
		static UE_API TSharedPtr<FMaterial> StaticUnserialise(FInputArchive& Arch);

        //! Clone this material
		UE_API TSharedPtr<FMaterial> Clone() const;

		// Resource interface
		virtual int32 GetDataSize() const override { return 0; };

		//-----------------------------------------------------------------------------------------
		// Own interface
		//-----------------------------------------------------------------------------------------
		UE_API bool operator==(const FMaterial& Other) const;


		UE_API void Serialise(FOutputArchive&) const;
		UE_API void Unserialise(FInputArchive&);


	public:

		TStrongObjectPtr<UMaterialInterface> Material;

		int32 ReferenceID = INDEX_NONE;

		TMap<FName, TVariant<OP::ADDRESS, TSharedPtr<const FImage>>> ImageParameters;
		TMap<FName, FVector4f> ColorParameters;
		TMap<FName, float> ScalarParameters;
	};
}

#undef UE_API
