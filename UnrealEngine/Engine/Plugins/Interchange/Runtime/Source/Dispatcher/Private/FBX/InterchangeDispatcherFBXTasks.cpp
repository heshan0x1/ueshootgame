// Copyright Epic Games, Inc. All Rights Reserved.

#include "FBX/InterchangeDispatcherFBXTasks.h"

#include "Dom/JsonObject.h"

namespace UE::Interchange
{
	const TCHAR* FJsonFBXLoadSourceCmd::TaskName = TEXT("LoadFBX");
	const TCHAR* FJsonFBXLoadSourceCmd::ConvertSceneJsonKey = TEXT("ConvertScene");
	const TCHAR* FJsonFBXLoadSourceCmd::ForceFrontXAxisJsonKey = TEXT("ForceFrontXAxis");
	const TCHAR* FJsonFBXLoadSourceCmd::ConvertSceneUnitJsonKey = TEXT("ConvertSceneUnit");
	const TCHAR* FJsonFBXLoadSourceCmd::KeepFbxNamespaceJsonKey = TEXT("KeepFbxNamespace");

	TSharedPtr<FJsonObject> FJsonFBXLoadSourceCmd::GetActionDataObject() const
	{
		TSharedPtr<FJsonObject> ActionDataObject = FJsonLoadSourceCmd::GetActionDataObject();
		ActionDataObject->SetBoolField(ConvertSceneJsonKey, bConvertScene);
		ActionDataObject->SetBoolField(ForceFrontXAxisJsonKey, bForceFrontXAxis);
		ActionDataObject->SetBoolField(ConvertSceneUnitJsonKey, bConvertSceneUnit);
		ActionDataObject->SetBoolField(KeepFbxNamespaceJsonKey, bKeepFbxNamespace);

		return ActionDataObject;
	}

	bool FJsonFBXLoadSourceCmd::IsActionDataObjectValid(const FJsonObject& ActionDataObject)
	{
		if (!(ActionDataObject.TryGetBoolField(ConvertSceneJsonKey, bConvertScene)))
		{
			return false;
		}
		if (!(ActionDataObject.TryGetBoolField(ForceFrontXAxisJsonKey, bForceFrontXAxis)))
		{
			return false;
		}
		if (!(ActionDataObject.TryGetBoolField(ConvertSceneUnitJsonKey, bConvertSceneUnit)))
		{
			return false;
		}
		if (!(ActionDataObject.TryGetBoolField(KeepFbxNamespaceJsonKey, bKeepFbxNamespace)))
		{
			return false;
		}

		return true;
	}
}//ns UE
