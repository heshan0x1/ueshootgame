// Copyright Epic Games, Inc. All Rights Reserved.

#include "../Public/Nodes/COInstanceGeneratorNode.h"

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "Engine/SkeletalMesh.h"
#include "MutableDataflowParameters.h"


FCOInstanceGetComponentMesh::FCOInstanceGetComponentMesh(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: Super(InParam, InGuid)
{
	RegisterInputConnection(&GeneratedResources);
	RegisterInputConnection(&ComponentName);

	RegisterOutputConnection(&ComponentSkeletalMesh);
}


void FCOInstanceGetComponentMesh::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	const TArray<FMutableGeneratedResource> InputGeneratedResources = GetValue(Context, &GeneratedResources);
	const FString TargetComponentName = GetValue(Context, &ComponentName);

	TObjectPtr<USkeletalMesh> OutputSkeletalMesh = nullptr;
	if (!TargetComponentName.IsEmpty())
	{
		for (const FMutableGeneratedResource& Resource : InputGeneratedResources)
		{
			if (Resource.ComponentName.Equals(TargetComponentName))
			{
				OutputSkeletalMesh = Resource.SkeletalMesh;
				break;
			}
		}
	}
	
	SetValue(Context, OutputSkeletalMesh, &ComponentSkeletalMesh);
}



FCOInstanceGeneratorNode::FCOInstanceGeneratorNode(const UE::Dataflow::FNodeParameters& InParam,FGuid InGuid)
: FDataflowNode(InParam, InGuid),
	GenerateInstanceResources( FDataflowFunctionProperty::FDelegate::CreateRaw(this, &FCOInstanceGeneratorNode::OnGenerateInstanceResourcesRequested))
{
	// Inputs
	RegisterInputConnection(&CustomizableObject);
	RegisterInputConnection(&SkeletalMeshParameters);
	RegisterInputConnection(&TextureParameters);
	RegisterInputConnection(&MaterialParameters);
	
	// Outputs
	RegisterOutputConnection(&GeneratedResources);
	RegisterOutputConnection(&GeneratedSkeletalMeshes);
}


void FCOInstanceGeneratorNode::OnPropertyChanged(UE::Dataflow::FContext& Context, const FPropertyChangedEvent& InPropertyChangedEvent)
{
	FDataflowNode::OnPropertyChanged(Context, InPropertyChangedEvent);

	if (const FProperty* ModifiedProperty = InPropertyChangedEvent.Property)
	{
		if (ModifiedProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FCOInstanceGeneratorNode, CustomizableObject))
		{
			ClearCachedParameters();
			
			CustomizableObjectInstance = nullptr;
			
			GeneratedResources.Reset();
			GeneratedSkeletalMeshes.Reset();
			Invalidate();
		}
	}
}


void FCOInstanceGeneratorNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (!GetValue(Context, &CustomizableObject))
	{
		Context.Error(TEXT("Unable to evaluate : No Customizable Object has been set"), this);
		return;
	}
	
	if (!CustomizableObjectInstance)
	{
		Context.Error(TEXT("Unable to evaluate : Press \"Generate Resources\" before evaluating the node"), this);
		return;
	}

	if (bIsGeneratingResources)
	{
		Context.Warning(TEXT("Unable to evaluate : Resource generation is in process. Please hold"), this);
		return;
	}
	
	if (Out->IsA<TArray<FMutableGeneratedResource>>(&GeneratedResources))
	{
		SetValue(Context, GeneratedResources, &GeneratedResources);
	}
	else if (Out->IsA<TArray<TObjectPtr<USkeletalMesh>>>(&GeneratedSkeletalMeshes))
	{
		// Extract the generated meshes from the generated resources collection as there we have them already set
		TArray<TObjectPtr<USkeletalMesh>> SkeletalMeshes;
		SkeletalMeshes.Reserve(GeneratedResources.Num());
		for (const FMutableGeneratedResource& Resource : GeneratedResources)
		{
			SkeletalMeshes.Add(Resource.SkeletalMesh);
		}
		SetValue(Context, SkeletalMeshes, &GeneratedSkeletalMeshes);
	}
}


void FCOInstanceGeneratorNode::ClearCachedParameters()
{
	CachedSkeletalMeshParameters.Reset();
	CachedMaterialParameters.Reset();
	CachedTextureParameters.Reset();
}


void FCOInstanceGeneratorNode::OnGenerateInstanceResourcesRequested(UE::Dataflow::FContext& Context)
{
	if (bIsGeneratingResources)
	{
		Context.Warning(TEXT("Unable to generate resources. Resource generation is already in process"), this);
		return;
	}
	
	if (GetValue(Context, &CustomizableObject))
	{
		// Prevent the scheduling or more compilations while the generation of resources is in process
		bIsGeneratingResources = true;
		RequestCompilation(Context);
	}
	else
	{
		Context.Error(TEXT("Unable to generate resources. The Customizable object has not been set"), this);
	}
}


void FCOInstanceGeneratorNode::RequestCompilation(UE::Dataflow::FContext& Context)
{
	// Capture the inputs of the node (target parameters) so we can apply them after the compilation without us having to rely on the context (as it may be gone)
	CacheNodeInputs(Context);
	check(CustomizableObject);
	
	FCompileNativeDelegate CompilationDelegate;
	TSharedRef<FDataflowNode> This = AsShared();
	CompilationDelegate.BindLambda([This](const FCompileCallbackParams& CallbackParams)->void
	{
		FCOInstanceGeneratorNode* HostNode = static_cast<FCOInstanceGeneratorNode*>(&This.Get());
		check(HostNode);

		// Early out if the request failed
		if (CallbackParams.bRequestFailed || CallbackParams.bErrors)
		{
			// Tell the system it is ok to generate resources again
			HostNode->bIsGeneratingResources = false;
			return;
		}
			
		// First generate the instance
		HostNode->CustomizableObjectInstance = HostNode->CustomizableObject->CreateInstance();
		check(HostNode->CustomizableObjectInstance)
			
		// Apply the parameters set in the node into the instance
		HostNode->ApplyInstanceParameters(*HostNode->CustomizableObjectInstance);

		// once the compilation has been performed (if required) ask for the update of the COI
		HostNode->RequestUpdate(*HostNode->CustomizableObjectInstance);
	});

	// Request the compilation of the CO
	FCompileParams CompilationParams;
	CompilationParams.bAsync = true;
	CompilationParams.CallbackNative = CompilationDelegate;
	
	// todo: UE-313428 Expose compilation errors and warnings to the Dataflow context
	CustomizableObject->Compile(CompilationParams);

	// Prevent the scheduling or more compilations while the generation of resources is in process
	bIsGeneratingResources = true;
}


void FCOInstanceGeneratorNode::CacheNodeInputs(UE::Dataflow::FContext& Context)
{
	ClearCachedParameters();

	// Customizable Object
	CustomizableObject = GetValue(Context, &CustomizableObject);

	// Cache the parameters now that we have the context. Do not cache duplicate entries (based on the param name)
	
	// Texture Parameters
	{
		const TArray<FMutableTextureParameter>& TempTextureParametersArray = GetValue(Context, &TextureParameters);
		for (const FMutableTextureParameter& TextureParameter : TempTextureParametersArray)
		{
			if (CachedTextureParameters.Contains(TextureParameter))
			{
				Context.Warning(FString::Printf( TEXT("Multiple Texture Parameters have been found sharing the \"%s\" name , only the first parameter will be used."), *TextureParameter.Name), this);
				continue;
			}
		
			CachedTextureParameters.Add(TextureParameter);
		}
	}
	
	// Mesh Parameters
	{
		const TArray<FMutableSkeletalMeshParameter>& TempMeshParametersArray = GetValue(Context, &SkeletalMeshParameters);
		for (const FMutableSkeletalMeshParameter& MeshParameter : TempMeshParametersArray)
		{
			if (CachedSkeletalMeshParameters.Contains(MeshParameter))
			{
				Context.Warning(FString::Printf( TEXT("Multiple Skeletal Mesh Parameters have been found sharing the \"%s\" name , only the first parameter will be used."), *MeshParameter.Name), this);
				continue;
			}
		
			CachedSkeletalMeshParameters.Add(MeshParameter);
		}
	}
	
	// Material parameters
	{
		const TArray<FMutableMaterialParameter>& TempMaterialParametersArray = GetValue(Context, &MaterialParameters);
		for (const FMutableMaterialParameter& MaterialParameter : TempMaterialParametersArray)
		{
			if (CachedMaterialParameters.Contains(MaterialParameter))
			{
				Context.Warning(FString::Printf( TEXT("Multiple Material Parameters have been found sharing the \"%s\" name , only the first parameter will be used."), *MaterialParameter.Name), this);
				continue;
			}
		
			CachedMaterialParameters.Add(MaterialParameter);
		}
	}
}


void FCOInstanceGeneratorNode::ApplyInstanceParameters(UCustomizableObjectInstance& Instance)
{
	// todo. notify when a parameter does not exist :
	// In order to do that we will require to have async node evaluations and then access to the
	// context to log the warning
	
	// Texture Parameters
	for (const FMutableTextureParameter& TextureParameter : CachedTextureParameters)
	{
		const FString ParameterName = TextureParameter.Name;
		if (Instance.ContainsTextureParameter(ParameterName))
		{
			Instance.SetTextureParameterSelectedOption(ParameterName, TextureParameter.Texture);
		}
	}
	
	// Mesh Parameters
	for (const FMutableSkeletalMeshParameter& MeshParameter : CachedSkeletalMeshParameters)
	{
		const FString ParameterName = MeshParameter.Name;
		if (Instance.ContainsSkeletalMeshParameter(ParameterName))
		{
			Instance.SetSkeletalMeshParameterSelectedOption(ParameterName, MeshParameter.Mesh);
		}
	}
	
	// Material parameters
	for (const FMutableMaterialParameter& MaterialParameter : CachedMaterialParameters)
	{
		const FString ParameterName = MaterialParameter.Name;
		if (Instance.ContainsMaterialParameter(ParameterName))
		{
			Instance.SetMaterialParameterSelectedOption(ParameterName, MaterialParameter.Material);				
		}
	}
}


void FCOInstanceGeneratorNode::RequestUpdate(UCustomizableObjectInstance& Instance)
{
	TSharedRef<FDataflowNode> This = AsShared();
	
	// Instance update delegate	
	FInstanceUpdateNativeDelegate InstanceUpdateNativeDelegate;
	InstanceUpdateNativeDelegate.AddLambda([This](FUpdateContext OutUpdateContext) -> void
	{
		FCOInstanceGeneratorNode* HostNode = static_cast<FCOInstanceGeneratorNode*>(&This.Get());
		check(HostNode);
			
		HostNode->GeneratedResources = HostNode->GetInstanceGeneratedMeshes(*OutUpdateContext.Instance);
			
		// Tell the system it is ok to generate resources again
		HostNode->bIsGeneratingResources = false;
			
		This->Invalidate();
	});
	 		
	Instance.UpdateSkeletalMeshAsyncResult(InstanceUpdateNativeDelegate,true, true);
}


TArray<FMutableGeneratedResource> FCOInstanceGeneratorNode::GetInstanceGeneratedMeshes(const UCustomizableObjectInstance& Instance) const
{
	TArray<FMutableGeneratedResource> GeneratedMeshesData;
			
	const TArray<FName> InstanceComponentNames = Instance.GetComponentNames();
	for (const FName& ComponentName : InstanceComponentNames)
	{
		if (const TObjectPtr<USkeletalMesh> SkeletalMesh = Instance.GetSkeletalMeshComponentSkeletalMesh(ComponentName))
		{
			FMutableGeneratedResource ComponentData;
			ComponentData.ComponentName = ComponentName.ToString();
			ComponentData.SkeletalMesh = SkeletalMesh;
			
			GeneratedMeshesData.Add(ComponentData);
		}
	}

	return GeneratedMeshesData;
}

