// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotorSimComponentDetailWindow.h"

#include "AudioMotorModelDebugWidget.h"
#include "Engine/Engine.h"
#include "IAudioMotorModelDebugger.h"
#include "IAudioMotorSim.h"
#include "SlateIM.h"
#include "Styling/AppStyle.h"

FMotorSimComponentDetailWindow::FMotorSimComponentDetailWindow(const FStringView& WindowTitle)
	: FAudioMotorModelDebugDetailWindow(WindowTitle, FVector2f(600,400))
{
	EnableMotorSimComponentDetails();
}

void FMotorSimComponentDetailWindow::SendDebugData(TConstArrayView<FInstancedStruct> DebugDataView)
{
	if(!IsWidgetEnabled())
	{
		return;
	}
	
	for(const FInstancedStruct& InstancedStruct : DebugDataView)
	{
		if(const FAudioMotorSimDebugDataBase* MotorSimDebugData = InstancedStruct.GetPtr<FAudioMotorSimDebugDataBase>())
		{
			for(const TPair<FName, double>& ParameterNameAndValue : MotorSimDebugData->ParameterValues)
			{
				ParameterValues.Add(ParameterNameAndValue.Key, ParameterNameAndValue.Value);
			}
		}
	}
}

void FMotorSimComponentDetailWindow::DrawWindow(float DeltaTime)
{
	SlateIM::BeginBorder(FAppStyle::GetBrush("ToolPanel.GroupBorder"));
	SlateIM::Fill();
	SlateIM::HAlign(HAlign_Fill);
	SlateIM::VAlign(VAlign_Fill);
	SlateIM::Padding({ 5.f });

	SlateIM::BeginScrollBox();
	{
		SlateIM::BeginHorizontalStack();
		{
			DrawParameterSelection();
			SlateIM::Spacer({ 10, 1 });
			DrawParameterGraphs();
		}
		SlateIM::EndHorizontalStack();
	}
	SlateIM::EndScrollBox();
}

void FMotorSimComponentDetailWindow::GenerateParamSelector(const TPair<FName, double>& ParamNameValue)
{
	const FName& ParamName = ParamNameValue.Key;
	const double& ParamValue = ParamNameValue.Value;
	
	const bool bIsParamDrawn = ParamGraphs.Find(ParamName) != nullptr;
	bool CheckState = bIsParamDrawn;
	
	SlateIM::BeginHorizontalStack();
	{
		SlateIM::VAlign(VAlign_Center);
		SlateIM::CheckBox(TEXT(""), CheckState);

		
		if(CheckState != bIsParamDrawn)
		{
			if(!bIsParamDrawn)
			{
				ParamGraphs.Add(ParamName, AudioMotorModelDebug::FParamPtrGraph<const double>(ParamName, &ParamValue));
			}
			else
			{
				ParamGraphs.Remove(ParamName);
			}
		}
		
		
		SlateIM::VAlign(VAlign_Center);
		SlateIM::Text(FString::Printf(TEXT("%s: %f"), *ParamName.ToString(), ParamValue));
	}
	SlateIM::EndHorizontalStack();		
}

void FMotorSimComponentDetailWindow::DrawParameterSelection()
{
	SlateIM::BeginHorizontalStack();
	{
		SlateIM::BeginScrollBox();
		{
			SlateIM::Text(TEXT("Params"), FColorList::Green);
			
			for (const TPair<FName, double>& NameValuePair : ParameterValues)
			{
				GenerateParamSelector(NameValuePair);
			}
		}
		SlateIM::EndScrollBox();
	}
	SlateIM::EndHorizontalStack();
}

void FMotorSimComponentDetailWindow::DrawParameterGraphs()
{
	SlateIM::BeginVerticalStack();
	{
		SlateIM::BeginVerticalStack();
		{
			SlateIM::Text(TEXT("Parameter Graphs"), FColorList::Green);

			SlateIM::BeginHorizontalStack();
			{
				SlateIM::Text(TEXT("Num Graphs in Row "));
				SlateIM::MinWidth(50.f);
				
				if (SlateIM::EditableText(GraphRowSizeText))
				{
					LiveGraphRowSize = FMath::Clamp(FCString::Atoi(*GraphRowSizeText), 1, MaxGraphRowSize);
				}
			}
			SlateIM::EndHorizontalStack();
		
			SlateIM::Text(FString::Printf(TEXT("Scale: %f"), ParamGraphScale));
			SlateIM::Slider(ParamGraphScale, 0, ParamGraphScaleMax, 1);
		
			SlateIM::Text(FString::Printf(TEXT("NumFrames: %0.f"), ParamGraphNumFrames));
			SlateIM::Slider(ParamGraphNumFrames, 1, ParamGraphNumFramesMax, 1);
			
		}
		SlateIM::EndVerticalStack();
	
		SlateIM::BeginVerticalStack();
		{
			int32 NumDrawnGraphs = 0;
			for(TPair<FName, AudioMotorModelDebug::FParamPtrGraph<const double>>& PropertyGraph : ParamGraphs)
			{
				
				if(NumDrawnGraphs % LiveGraphRowSize == 0)
				{
					SlateIM::BeginHorizontalStack();
				}
				
				PropertyGraph.Value.Draw();
				PropertyGraph.Value.SetScale(ParamGraphScale);
				PropertyGraph.Value.SetNumFrames(ParamGraphNumFrames);
				NumDrawnGraphs++;

				if(NumDrawnGraphs % LiveGraphRowSize == 0)
				{
					SlateIM::EndHorizontalStack();
				}
			}

			if(NumDrawnGraphs % LiveGraphRowSize != 0)
			{
				SlateIM::EndHorizontalStack();
			}
		}
		SlateIM::EndVerticalStack();
	}
	SlateIM::EndVerticalStack();
}

void FMotorSimComponentDetailWindow::EnableMotorSimComponentDetails() const
{
	IConsoleVariable* MotorSimComponentDetailsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Fort.VehicleAudio.MotorSimDebugDataCollection"));

	if (MotorSimComponentDetailsCVar)
	{
		bool IsEnabled = MotorSimComponentDetailsCVar->GetBool();
		if(!IsEnabled)
		{
			MotorSimComponentDetailsCVar->Set(true);
		}
	}
	else
	{
		UE_LOG(LogInit, Log, TEXT("MotorSimComponentDetailsCVar not found. Detail window will not work"));
	}
}