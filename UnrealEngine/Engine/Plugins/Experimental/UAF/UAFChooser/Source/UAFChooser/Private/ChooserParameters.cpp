// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserParameters.h"
#include "UAFAssetInstance.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

namespace UE::UAF::Private
{

// Helper function to find the first data interface instance in the context 
FUAFAssetInstance* GetFirstDataInterfaceInstance(FChooserEvaluationContext& Context)
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

bool FBoolAnimProperty::GetValue(FChooserEvaluationContext& Context, bool& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstDataInterfaceInstance(Context))
	{
		return Instance->GetVariable(Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FBoolAnimProperty::SetValue(FChooserEvaluationContext& Context, bool InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstDataInterfaceInstance(Context))
	{
		return Instance->SetVariable(Variable, InValue) == EPropertyBagResult::Success;
	}
	return false;
}

void FBoolAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}

#if WITH_EDITORONLY_DATA
bool FBoolAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FBoolAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif

bool FFloatAnimProperty::GetValue(FChooserEvaluationContext& Context, double& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstDataInterfaceInstance(Context))
	{
		return Instance->GetVariable(Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FFloatAnimProperty::SetValue(FChooserEvaluationContext& Context, double InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstDataInterfaceInstance(Context))
 	{
 		return Instance->SetVariable(Variable, InValue) == EPropertyBagResult::Success;
 	}
 	return false;
}

void FFloatAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}

#if WITH_EDITORONLY_DATA
bool FFloatAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FFloatAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif

bool FEnumAnimProperty::GetValue(FChooserEvaluationContext& Context, uint8& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstDataInterfaceInstance(Context))
	{
		return Instance->GetVariable(Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FEnumAnimProperty::SetValue(FChooserEvaluationContext& Context, uint8 InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstDataInterfaceInstance(Context))
	{
		return Instance->SetVariable(Variable, InValue) == EPropertyBagResult::Success;
	}
	return false;
}

void FEnumAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}

#if WITH_EDITORONLY_DATA
bool FEnumAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FEnumAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif