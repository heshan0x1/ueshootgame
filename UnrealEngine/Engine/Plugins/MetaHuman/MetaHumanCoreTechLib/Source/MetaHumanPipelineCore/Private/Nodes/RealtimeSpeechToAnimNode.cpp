// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/RealtimeSpeechToAnimNode.h"

#include "Pipeline/Log.h"

#include "UObject/Package.h"

#include "NNE.h"
#include "NNETypes.h"
#include "NNEModelData.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeCPU.h"

#include "GuiToRawControlsUtils.h"



namespace UE::MetaHuman::Pipeline
{

static TArray<FString> CurveNames = {
"CTRL_L_brow_down.ty",
"CTRL_R_brow_down.ty",
"CTRL_L_brow_lateral.ty",
"CTRL_R_brow_lateral.ty",
"CTRL_L_brow_raiseIn.ty",
"CTRL_R_brow_raiseIn.ty",
"CTRL_L_brow_raiseOut.ty",
"CTRL_R_brow_raiseOut.ty",
"CTRL_L_eye_blink.ty",
"CTRL_R_eye_blink.ty",
"CTRL_L_eye_squintInner.ty",
"CTRL_R_eye_squintInner.ty",
"CTRL_L_eye_cheekRaise.ty",
"CTRL_R_eye_cheekRaise.ty",
"CTRL_L_nose.ty",
"CTRL_R_nose.ty",
"CTRL_L_nose.tx",
"CTRL_R_nose.tx",
"CTRL_L_nose_nasolabialDeepen.ty",
"CTRL_R_nose_nasolabialDeepen.ty",
"CTRL_C_mouth.tx",
"CTRL_L_mouth_upperLipRaise.ty",
"CTRL_R_mouth_upperLipRaise.ty",
"CTRL_L_mouth_lowerLipDepress.ty",
"CTRL_R_mouth_lowerLipDepress.ty",
"CTRL_L_mouth_cornerPull.ty",
"CTRL_R_mouth_cornerPull.ty",
"CTRL_L_mouth_stretch.ty",
"CTRL_R_mouth_stretch.ty",
"CTRL_L_mouth_dimple.ty",
"CTRL_R_mouth_dimple.ty",
"CTRL_L_mouth_cornerDepress.ty",
"CTRL_R_mouth_cornerDepress.ty",
"CTRL_L_mouth_purseU.ty",
"CTRL_R_mouth_purseU.ty",
"CTRL_L_mouth_purseD.ty",
"CTRL_R_mouth_purseD.ty",
"CTRL_L_mouth_towardsU.ty",
"CTRL_R_mouth_towardsU.ty",
"CTRL_L_mouth_towardsD.ty",
"CTRL_R_mouth_towardsD.ty",
"CTRL_L_mouth_funnelU.ty",
"CTRL_R_mouth_funnelU.ty",
"CTRL_L_mouth_funnelD.ty",
"CTRL_R_mouth_funnelD.ty",
"CTRL_L_mouth_lipsTogetherU.ty",
"CTRL_R_mouth_lipsTogetherU.ty",
"CTRL_L_mouth_lipsTogetherD.ty",
"CTRL_R_mouth_lipsTogetherD.ty",
"CTRL_L_mouth_lipBiteU.ty",
"CTRL_R_mouth_lipBiteU.ty",
"CTRL_L_mouth_lipBiteD.ty",
"CTRL_R_mouth_lipBiteD.ty",
"CTRL_L_mouth_sharpCornerPull.ty",
"CTRL_R_mouth_sharpCornerPull.ty",
"CTRL_L_mouth_pushPullU.ty",
"CTRL_R_mouth_pushPullU.ty",
"CTRL_L_mouth_pushPullD.ty",
"CTRL_R_mouth_pushPullD.ty",
"CTRL_L_mouth_cornerSharpnessU.ty",
"CTRL_R_mouth_cornerSharpnessU.ty",
"CTRL_L_mouth_cornerSharpnessD.ty",
"CTRL_R_mouth_cornerSharpnessD.ty",
"CTRL_L_mouth_lipsRollU.ty",
"CTRL_R_mouth_lipsRollU.ty",
"CTRL_L_mouth_lipsRollD.ty",
"CTRL_R_mouth_lipsRollD.ty",
"CTRL_C_jaw.ty",
"CTRL_C_jaw.tx",
"CTRL_C_jaw_fwdBack.ty",
"CTRL_L_jaw_ChinRaiseD.ty",
"CTRL_R_jaw_ChinRaiseD.ty",
"CTRL_C_tongue_move.ty",
"CTRL_C_tongue_move.tx",
"CTRL_C_tongue_inOut.ty",
"CTRL_C_tongue_tipMove.ty",
"CTRL_C_tongue_tipMove.tx",
"CTRL_C_tongue_wideNarrow.ty",
"CTRL_C_tongue_press.ty",
"CTRL_C_tongue_roll.ty",
"CTRL_C_tongue_thickThin.ty"
};

FRealtimeSpeechToAnimNode::FRealtimeSpeechToAnimNode(const FString& InName) : FNode("RealtimeSpeechToAnimNode", InName)
{
	check(CurveNames.Num() == 81);
	check(StaticEnum<EAudioDrivenAnimationMood>()->NumEnums() == 14); // 12 regular, auto-detect plus UHT added MAX item. 14 in total.

	Pins.Add(FPin("Audio In", EPinDirection::Input, EPinType::Audio));
	Pins.Add(FPin("Animation Out", EPinDirection::Output, EPinType::Animation));
}

bool FRealtimeSpeechToAnimNode::Start(const TSharedPtr<FPipelineData>& InPipelineData)
{
	if (!Model)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToInitialize);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to initialize"));
		return false;
	}

	InputBuffer.Reset();
	AudioBuffer.SetNumZeroed(16000); // The first input to the model is 1.0s of audio (=16000 mono samples at 16kHz). Fill with zeros so first second of audio is not dead animation.
	CurveValues.SetNumZeroed(81); // The second input to the model is state of the 81 curves from the previous iteration. Initially zero
	Step.SetNumZeroed(1); // The 4th input to the model is a step counter that increments every frame. Initially zero.
	FrameBuffer.SetNumUninitialized(320); // A buffer to hold the 20ms (=320 mono samples at 16kHz) of audio required to advance the model to the next interaction

	AnimOut = FFrameAnimationData();

	return true;
}

bool FRealtimeSpeechToAnimNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FAudioDataType& Audio = InPipelineData->GetData<FAudioDataType>(Pins[0]);

	if (Audio.NumChannels != 1)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::UnsupportedNumberOfChannels);
		InPipelineData->SetErrorNodeMessage(TEXT("Unsupported number of channels"));
		return false;
	}

	if (Audio.SampleRate != 16000)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::UnsupportedSampleRate);
		InPipelineData->SetErrorNodeMessage(TEXT("Unsupported sample rate"));
		return false;
	}

	if (!Audio.bContiguous)
	{
		InputBuffer.Reset();
		AudioBuffer.SetNumZeroed(16000);
		CurveValues.SetNumZeroed(81);
		Step.SetNumZeroed(1);
		FrameBuffer.SetNumUninitialized(320);
	}

	InputBuffer.Append(Audio.Data);

	while (InputBuffer.Num() >= 320) // Do we have enough data to pass to the speech solve
	{
		FMemory::Memcpy(FrameBuffer.GetData(), InputBuffer.GetData(), 320 * sizeof(float)); // Copy first 320 samples from InputBuffer into FrameBuffer

		InputBuffer = TArray<float>(&InputBuffer.GetData()[320], InputBuffer.Num() - 320); // The remaining samples becomes the new InputBuffer

		if (AudioBuffer.Num() == 16000) // Do we have the required 16000 sample audio buffer to run the next interaction of the speech solver. Currently always true to avoid initial deadtime but that may change.
		{
			// Yes, shift AudioBuffer down and add FrameBuffer to end of AudioBuffer to maintain 16000 samples
			FMemory::Memcpy(AudioBuffer.GetData(), &AudioBuffer.GetData()[320], (16000 - 320) * sizeof(float));
			FMemory::Memcpy(&AudioBuffer.GetData()[16000 - 320], FrameBuffer.GetData(), 320 * sizeof(float));

			// Prepare the mood input array
			TArray<float> MoodArray;
			MoodArray.SetNumZeroed(21);
			MoodMutex.Lock();
			MoodArray[(uint8)Mood] = MoodIntensity;
			MoodMutex.Unlock();

			// Prepare the lookahead array
			TArray<int64> LookaheadArray;
			LookaheadArray.SetNumZeroed(1);
			LookaheadArray[0] = GetLookahead() / 20; // lookahead needs to be in frames, 1 frame = 20ms

			// Run the speech solver
			TArray<UE::NNE::FTensorBindingCPU> Inputs, Outputs;
			Inputs = { {(void*)AudioBuffer.GetData(), AudioBuffer.Num() * sizeof(float)}, {(void*)CurveValues.GetData(), CurveValues.Num() * sizeof(float)}, {(void*)MoodArray.GetData(), MoodArray.Num() * sizeof(float)}, {(void*)LookaheadArray.GetData(), LookaheadArray.Num() * sizeof(int64)}, {(void*)Step.GetData(), Step.Num() * sizeof(int64)} };
			Outputs = { {(void*)CurveValues.GetData(), CurveValues.Num() * sizeof(float)}, {(void*)Step.GetData(), Step.Num() * sizeof(int64)} };

			if (Model->RunSync(Inputs, Outputs) != UE::NNE::EResultStatus::Ok)
			{
				InPipelineData->SetErrorNodeCode(ErrorCode::FailedToRun);
				InPipelineData->SetErrorNodeMessage(TEXT("Failed to run"));
				return false;
			}

			check(CurveNames.Num() == CurveValues.Num());

			TMap<FString, float> SolverControlMap;
			for (int32 Index = 0; Index < CurveValues.Num(); ++Index)
			{
				SolverControlMap.Add(CurveNames[Index], CurveValues[Index]);
			}

			AnimOut.AnimationData = GuiToRawControlsUtils::ConvertGuiToRawControls(SolverControlMap);
			AnimOut.AnimationQuality = EFrameAnimationQuality::PostFiltered;
			check(AnimOut.AnimationData.Num() == 251);
		}
		else
		{
			// No, keep accumulating FrameBuffer in AudioBuffer
			AudioBuffer.Append(FrameBuffer);
		}
	}

	InPipelineData->SetData<FFrameAnimationData>(Pins[1], AnimOut);

	return true;
}

bool FRealtimeSpeechToAnimNode::End(const TSharedPtr<FPipelineData>& InPipelineData)
{
	AudioBuffer.Reset();
	CurveValues.Reset();
	Step.Reset();
	InputBuffer.Reset();
	FrameBuffer.Reset();

	AnimOut = FFrameAnimationData();

	return true;
}

bool FRealtimeSpeechToAnimNode::LoadModels()
{
	using namespace UE::NNE;

	UNNEModelData* ModelData = LoadObject<UNNEModelData>(GetTransientPackage(), TEXT("/MetaHumanCoreTech/RealtimeAudio/solar-1-2700_blend-0v65.solar-1-2700_blend-0v65"));
	if (!ModelData)
	{
		UE_LOG(LogMetaHumanPipeline, Warning, TEXT("Failed to load streaming speech to animation model"));
		return false;
	}

	TWeakInterfacePtr<INNERuntimeGPU> Runtime = GetRuntime<INNERuntimeGPU>("NNERuntimeORTDml");
	if (!Runtime.IsValid())
	{
		UE_LOG(LogMetaHumanPipeline, Warning, TEXT("Failed to get runtime"));
		return false;
	}

	TSharedPtr<IModelGPU> GPUModel = Runtime->CreateModelGPU(ModelData);
	if (!GPUModel)
	{
		UE_LOG(LogMetaHumanPipeline, Warning, TEXT("Failed to create GPU model"));
		return false;
	}

	Model = GPUModel->CreateModelInstanceGPU();
	if (!Model)
	{
		UE_LOG(LogMetaHumanPipeline, Warning, TEXT("Failed to create streaming speech to animation model"));
		return false;
	}

	FTensorShape TensorShape1 = FTensorShape::Make({ 16000 });
	FTensorShape TensorShape2 = FTensorShape::Make({ 81 });
	FTensorShape TensorShape3 = FTensorShape::Make({ 21 });
	FTensorShape TensorShape4 = FTensorShape::Make({ 1 });
	FTensorShape TensorShape5 = FTensorShape::Make({ 1 });

	if (Model->SetInputTensorShapes({ TensorShape1, TensorShape2, TensorShape3, TensorShape4, TensorShape5 }) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogMetaHumanPipeline, Warning, TEXT("Failed to set model input"));
		Model.Reset();
		return false;
	}

	return true;
}

void FRealtimeSpeechToAnimNode::SetMood(EAudioDrivenAnimationMood InMood)
{
	FScopeLock Lock(&MoodMutex);

	Mood = InMood;
}

EAudioDrivenAnimationMood FRealtimeSpeechToAnimNode::GetMood(void)
{
	FScopeLock Lock(&MoodMutex);

	return Mood;
}

void FRealtimeSpeechToAnimNode::SetMoodIntensity(float InMoodIntensity)
{
	FScopeLock Lock(&MoodMutex);

	MoodIntensity = InMoodIntensity;
}

float FRealtimeSpeechToAnimNode::GetMoodIntensity(void)
{
	FScopeLock Lock(&MoodMutex);

	return MoodIntensity;
}

void FRealtimeSpeechToAnimNode::SetLookahead(int32 InLookahead)
{
	FScopeLock Lock(&LookaheadMutex);

	Lookahead = InLookahead;
}

int32 FRealtimeSpeechToAnimNode::GetLookahead(void)
{
	FScopeLock Lock(&LookaheadMutex);

	return Lookahead;
}

}