// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseHistoryChooserParameter.h"
#include "UAFAssetInstance.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

namespace 
{

// Helper function to find the first data interface instance in the context 
FUAFAssetInstance* GetFirstAnimNextDataInterfaceInstance(FChooserEvaluationContext& Context)
{
	for(const FStructView& Param : Context.Params)
	{
		if(Param.GetScriptStruct() == FUAFAssetInstance::StaticStruct())
		{
			return Param.GetPtr<FUAFAssetInstance>();
		}
	}

	return nullptr;
}

}

bool FPoseHistoryAnimProperty::GetValue(FChooserEvaluationContext& Context, FPoseHistoryReference& OutResult) const
{
	if(FUAFAssetInstance* Instance = GetFirstAnimNextDataInterfaceInstance(Context))
	{
		return Instance->GetVariable(Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

#if WITH_EDITORONLY_DATA
bool FPoseHistoryAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FPoseHistoryAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif