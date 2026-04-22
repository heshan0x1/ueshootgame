// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMotorModelDebugWidget.h"
#include "MotorSimComponentDetailWindow.h"
#include "SlateIMWidgetBase.h"

class FAudioMotorModelSlateIMWindow : public FSlateIMWindowBase
{
public:
	FAudioMotorModelSlateIMWindow(const TCHAR* Command, const TCHAR* CommandHelp);

	virtual void DrawWindow(float DeltaTime) override;

	FAudioMotorModelDebugWidget DebugWidget;
};

class FAudioMotorModelSlateIMViewportWidget : public FSlateIMWidgetWithCommandBase              
{                                                                                    
public:                                                                              
	FAudioMotorModelSlateIMViewportWidget(const TCHAR* Command, const TCHAR* CommandHelp);

	virtual void DrawWidget(float DeltaTime) override;

	FAudioMotorModelDebugWidget DebugWidget;                                                   

private:                                                                             
	SlateIM::FViewportRootLayout Layout;                                             
};                                                                                   