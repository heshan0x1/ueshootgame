// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Templates/UnrealTypeTraits.h"
#include "UObject/UnrealNames.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

namespace UE::Dataflow
{
	/**
	* List of renderable components for a sepcific root actor
	* this class is used by IRenderableType to collect the components to render 
	*/
	struct FRenderableComponents
	{
	public:
		FRenderableComponents(AActor* InRootActor)
			: RootActor(InRootActor)
		{}

		/**
		* Create a new component of a specific type ( needs to inherit from UPrimitiveComponent )
		* and add it to the list 
		* returns  the newly added component so that it can be configured by the caller 
		*/
		template<typename T UE_REQUIRES(TIsDerivedFrom<T, UPrimitiveComponent>::Value)>
		T* AddNewComponent(FName ComponentName = NAME_None, UObject* ComponentParent = nullptr)
		{
			UObject* ParentObject = ComponentParent ? ComponentParent : RootActor;

			const FName ObjectName = MakeUniqueObjectName(ParentObject, T::StaticClass(), FName(ComponentName));
			
			// for now always allocate a new one - in the future we could use a pool 
			T* NewComponent = NewObject<T>(ParentObject, ObjectName);
			if (NewComponent)
			{
				Components.Add(NewComponent);
			}
			return NewComponent;
		}

		const TArray<UPrimitiveComponent*>& GetComponents() const { return Components; }

	private:
		AActor* RootActor = nullptr;
		TArray<UPrimitiveComponent*> Components;
	};
}
