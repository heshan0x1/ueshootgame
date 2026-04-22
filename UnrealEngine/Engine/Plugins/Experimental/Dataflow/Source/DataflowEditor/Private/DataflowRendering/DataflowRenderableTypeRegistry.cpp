// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowRendering/DataflowRenderableTypeRegistry.h"

#include "DataflowRendering/DataflowRenderableType.h"

namespace UE::Dataflow
{
	FRenderableTypeRegistry& FRenderableTypeRegistry::GetInstance()
	{
		static FRenderableTypeRegistry Instance;
		return Instance;
	}

	void FRenderableTypeRegistry::Register(const IRenderableType* RenderableType)
	{
		if (RenderableType)
		{
			RenderableTypesByPrimaryType
				.FindOrAdd(RenderableType->GetOutputType())
				.Add(RenderableType);
		}
	}

	const FRenderableTypeRegistry::FRenderableTypes& FRenderableTypeRegistry::GetRenderableTypes(FName PrimaryType) const
	{
		static const FRenderableTypes EmptyTypes;

		if (const FRenderableTypes* RenderableTypes = RenderableTypesByPrimaryType.Find(PrimaryType))
		{
			return *RenderableTypes;
		}
		return EmptyTypes;
	}
}
