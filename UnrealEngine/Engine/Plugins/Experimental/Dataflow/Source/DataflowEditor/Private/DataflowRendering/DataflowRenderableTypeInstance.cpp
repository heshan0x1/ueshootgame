// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowRendering/DataflowRenderableTypeInstance.h"

#include "Dataflow/DataflowCoreNodes.h"
#include "DataflowRendering/DataflowRenderableComponents.h"
#include "DataflowRendering/DataflowRenderableType.h"
#include "DataflowRendering/DataflowRenderableTypeRegistry.h"

namespace UE::Dataflow
{
	namespace Private
	{
		bool IsVisibleRenderGroup(FName RenderGroup, const FDataflowOutput& Output)
		{
			if (const FProperty* Property = Output.GetProperty())
			{
				static const FName RenderGroupsMetaDataName = TEXT("DataflowRenderGroups");
				const FString RenderGroupsMetaData = Property->GetMetaData(RenderGroupsMetaDataName);
				if (!RenderGroupsMetaData.IsEmpty())
				{
					TArray<FString> RenderGroups;
					RenderGroupsMetaData.ParseIntoArray(RenderGroups, TEXT(","));
					if (RenderGroups.Num() > 0)
					{
						return RenderGroups.Contains(RenderGroup);
					}
				}
			}
			return true;
		}
	}

	/* static */ void FRenderableTypeInstance::GetRenderableInstancesForNode(FContext& Context, const IDataflowConstructionViewMode& ViewMode, const FDataflowNode& Node, TArray<FRenderableTypeInstance>& OutRenderableTypeInstances)
	{
		OutRenderableTypeInstances.Reset();

		// we do not want reroute node to be able to render 
		if (Node.AsType<FDataflowReRouteNode>())
		{
			return;
		}

		TArray<FDataflowOutput*> Outputs = Node.GetOutputs();
		for (const FDataflowOutput* Output : Outputs)
		{
			if (Output)
			{
				const FRenderableTypeRegistry::FRenderableTypes& RenderableTypes = FRenderableTypeRegistry::GetInstance().GetRenderableTypes(Output->GetType());
				for (const IRenderableType* RenderableType : RenderableTypes)
				{
					if (RenderableType  && RenderableType->IsViewModeSupported(ViewMode))
					{
						FRenderableTypeInstance RenderableTypeInstance(Context, Node.AsShared(), Output->GetName(), RenderableType);
						if (RenderableTypeInstance.CanRender())
						{
							if (Private::IsVisibleRenderGroup(RenderableType->GetRenderGroup(), *Output))
							{
								OutRenderableTypeInstances.Emplace(RenderableTypeInstance);
							}
						}
					}
				}
			}
		}
	}

	bool FRenderableTypeInstance::CanRender() const
	{
		return RenderableType ? RenderableType->CanRender(*this) : false;
	}

	FString FRenderableTypeInstance::GetDisplayName() const
	{
		if (RenderableType)
		{
			return FString::Format(TEXT("{0}.{1}"), { RenderableType->GetOutputType().ToString(), RenderableType->GetRenderGroup().ToString() });
		}
		return { TEXT("-- Unknown --") };
	}

	void FRenderableTypeInstance::GetPrimitiveComponents(FRenderableComponents& OutComponents) const
	{
		if (RenderableType)
		{
			RenderableType->GetPrimitiveComponents(*this, OutComponents);
			if (Node)
			{
				Node->OnRenderOutput(EvaluationContext, OutputName, RenderableType->GetRenderGroup(), OutComponents.GetComponents());
			}
		}
	}

	FContextCacheKey  FRenderableTypeInstance::ComputeCacheKey() const
	{
		FContextCacheKey OutputCacheKey = 0;
		if (Node)
		{
			if (const FDataflowOutput* Output = Node->FindOutput(OutputName))
			{
				OutputCacheKey = Output->CacheKey();
			}
		}
		const uint32 RenderGroupHash = GetTypeHash(RenderableType? RenderableType->GetRenderGroup(): NAME_None);
		return ::HashCombine(OutputCacheKey, RenderGroupHash);
	}
}
