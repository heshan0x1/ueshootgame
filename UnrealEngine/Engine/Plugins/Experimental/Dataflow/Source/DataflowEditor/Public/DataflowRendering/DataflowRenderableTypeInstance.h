// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"

#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

class UPrimitiveComponent;
class AActor;

#define UE_API DATAFLOWEDITOR_API

namespace UE::Dataflow
{
	struct IRenderableType;
	struct FRenderableComponents;
	class IDataflowConstructionViewMode;

	/**
	* This describe a renderable type instance which match to a specific node + output(s)
	* And to be rendered in the context a specific Actor and Dataflow context
	*/
	struct FRenderableTypeInstance final
	{
	public:
		/** find all renderable instances for a specific node and a specific view mode */
		static void GetRenderableInstancesForNode(FContext& Context, const IDataflowConstructionViewMode& ViewMode, const FDataflowNode& Node, TArray<FRenderableTypeInstance>& OutRenderableInstances);

		/** Whether this instance can render */
		bool CanRender() const;

		/** Returns the display name of this instance */
		FString GetDisplayName() const;

		/** Get the corresponding  */
		void GetPrimitiveComponents(FRenderableComponents& OutComponents) const;

		/** 
		* Get value for the primary output to render 
		* Returns the default value if not found of this fails internally 
		*/
		template <typename T>
		const T& GetOutputValue(const T& Default = T()) const
		{
			return GetOutputValue(OutputName, Default);
		}

		bool HasUptoDateCachedValue() const
		{
			if (Node)
			{
				const FContextCacheKey Key = ComputeCacheKey();
				return EvaluationContext.HasData(Key, Node->GetTimestamp());
			}
			return false;
		}

		template<typename T>
		const T& GetCachedValue() const
		{
			static const T StaticDefaultValue;
			const FContextCacheKey Key = ComputeCacheKey();
			return EvaluationContext.GetData<T>(Key, nullptr, StaticDefaultValue);
		}

		template<typename T>
		void CacheValue(T&& InValue) const
		{
			if (Node)
			{
				const FContextCacheKey Key = ComputeCacheKey();
				EvaluationContext.SetData(Key, nullptr, Forward<T>(InValue), Node->GetGuid(), /*Node->GetValueHash()*/0, Node->GetTimestamp());
			}
		}

	private:
		template <typename T>
		const T& GetOutputValue(FName Name, const T& Default = T()) const
		{
			if (Node)
			{
				if (const FDataflowOutput* Output = Node->FindOutput(Name))
				{
					if (EvaluationContext.IsThreaded())
					{
						return Output->ReadValue<T>(EvaluationContext, Default);
					}
					return Output->GetValue<T>(EvaluationContext, Default);
				}
			}
			return Default;
		}

		UE_API FContextCacheKey ComputeCacheKey() const;

		FRenderableTypeInstance(FContext& InContext, const TSharedPtr<const FDataflowNode>& InNode, const FName InOutputName, const IRenderableType* InRenderableType)
			: EvaluationContext(InContext)
			, Node(InNode)
			, OutputName(InOutputName)
			, RenderableType(InRenderableType)
		{}

	private:
		FContext& EvaluationContext;
		TSharedPtr<const FDataflowNode> Node;
		const FName OutputName;
		const IRenderableType* RenderableType;
	};
}


#undef UE_API

