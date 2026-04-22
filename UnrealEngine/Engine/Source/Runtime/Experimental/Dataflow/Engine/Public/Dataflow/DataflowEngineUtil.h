// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowEngineTypes.h"
#include "Math/Transform.h"
#include "ReferenceSkeleton.h"
#include "StructUtils/PropertyBag.h"
#include "Templates/Casts.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"

#define UE_API DATAFLOWENGINE_API

struct FDataflowConnection;
struct FInstancedPropertyBag;

namespace UE::Dataflow
{
	namespace Reflection
	{
		template<class T>
		const T* FindObjectPtrProperty(const UObject* Owner, FName Name)
		{
			if (Owner && Owner->GetClass())
			{
				if (const ::FProperty* UEProperty = Owner->GetClass()->FindPropertyByName(Name))
				{
					if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(UEProperty))
					{
						if (const void* ObjectContainer = ObjectProperty->ContainerPtrToValuePtr<void>(Owner))
						{
							if (const UObject* Value = ObjectProperty->GetObjectPropertyValue(ObjectContainer))
							{
								return Cast<T>(Value);
							}
						}
					}
				}
			}
			return nullptr;
		}

		template<class T>
		const T FindOverrideProperty(const UObject* Owner, FName PropertyName, FName ArrayKey, const T& Default = T())
		{
			if (Owner && Owner->GetClass())
			{
				if (FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName))
				{
					if (FStructProperty* ArrayInnerProperty = CastFieldChecked<FStructProperty>(ArrayProperty->Inner))
					{
						if (ArrayInnerProperty->Struct == FStringValuePair::StaticStruct())
						{
							if (const TArray<FStringValuePair>* ContainerData = ArrayProperty->ContainerPtrToValuePtr< TArray<FStringValuePair> >(Owner))
							{
								for (const FStringValuePair& Pair : *ContainerData)
								{
									if (Pair.Key.Equals(ArrayKey.ToString()))
									{
										return Pair.Value;
									}
								}
							}
						}
					}
				}
			}
			return Default;
		}
	}

	namespace Animation
	{
		UE_API void GlobalTransforms(const FReferenceSkeleton&, TArray<FTransform>& GlobalTransforms);
	}

	namespace Color
	{
		UE_API FLinearColor GetRandomColor(const int32 RandomSeed, int32 Idx);
	}

	namespace Type
	{
		UE_API FPropertyBagPropertyDesc GetPropertyBagPropertyDescFromDataflowType(const FName Name, const FName Type);
		UE_API FPropertyBagPropertyDesc GetPropertyBagPropertyDescFromDataflowConnection(const FDataflowConnection& Connection);
		UE_API FName MakeUniqueNameForPropertyBag(FName OriginalName, const FInstancedPropertyBag& PropertyBag);
	}
}

#undef UE_API
