// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Struct of static delegates for editor tools. Delegates intended to be used by projects to send tool telemetry events
 */

#pragma once
#include "Delegates/Delegate.h"

struct FEditorToolDelegates
{

public:
	/**Broadcasted for editor tools OnLoad or OnInitialize, if available to notify when tool used */
	DECLARE_MULTICAST_DELEGATE_OneParam(FEditorToolStarted, const FString &/*class name*/);
	static EDITORTOOLEVENTS_API FEditorToolStarted OnEditorToolStarted;
	
	/*More delegates can be added, starting minimal so only events we use are added*/
};
