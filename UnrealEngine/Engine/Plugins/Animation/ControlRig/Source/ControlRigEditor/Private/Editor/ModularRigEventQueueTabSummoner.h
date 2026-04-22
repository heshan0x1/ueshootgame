// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

#define UE_API CONTROLRIGEDITOR_API

class IControlRigBaseEditor;

struct FModularRigEventQueueTabSummoner : public FWorkflowTabFactory
{
public:

	static inline const FLazyName TabID = FLazyName(TEXT("Event Queue"));
	
public:
	UE_API FModularRigEventQueueTabSummoner(const TSharedRef<IControlRigBaseEditor>& InControlRigEditor);
	
	UE_API virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	
protected:
	TWeakPtr<IControlRigBaseEditor> ControlRigEditor;
};

#undef UE_API
