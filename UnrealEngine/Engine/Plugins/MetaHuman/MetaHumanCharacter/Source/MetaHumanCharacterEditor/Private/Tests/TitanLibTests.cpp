// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ConstructorHelpers.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/SkeletalMesh.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "SkelMeshDNAUtils.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "InterchangeDnaModule.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "MetaHumanCommonDataUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCoreTechLibTest, Verbose, All)

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTestMetaHumanCharacterIdentityTest, "MetaHumanCreator.CoreTech.MetaHumanCharacterIdentityTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FTestMetaHumanCharacterIdentityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add("Basic test of MetaHumanCharacterIdentity API for full MH");
	OutTestCommands.Add("MetaHumanCharacterIdentity_MH");
}

bool CheckNumVertices(TSharedPtr<FMetaHumanCharacterIdentity::FState> InState, int32 InExpectedNumVertices)
{
	bool bResult = true;
	TArray<FVector3f> Vertices = InState->Evaluate().Vertices;
	if (Vertices.Num() != InExpectedNumVertices)
	{
		UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of vertices in FState::Evaluate. Expected %d, got %d."), InExpectedNumVertices, Vertices.Num());
		bResult = false;
	}

	return bResult;
}

bool CheckValues(const TArray<FVector3f> & InNewPositions, const TArray<FVector3f>& InOldPositions, const FString & InLabel, float InMaxTolerance = 0.000001f, float InMedianTolerance = 0.000001f)
{
	bool bResult = true;
	TArray<float> Diffs;
	Diffs.SetNumUninitialized(InOldPositions.Num());

	if (InNewPositions.Num() != InOldPositions.Num())
	{
		UE_LOG(LogMetaHumanCoreTechLibTest, Display, TEXT("Unexpected number of %s. Expected %d, got %d."), *InLabel, InOldPositions.Num(), InNewPositions.Num());
		bResult = false;
	}
	else
	{
		float MaxDiff = 0;
		int32 MaxDiffInd = -1;

		for (int32 I = 0; I < InOldPositions.Num(); ++I)
		{
			const float Diff = (InOldPositions[I] - InNewPositions[I]).Length();
			if (Diff > MaxDiff)
			{
				MaxDiff = Diff;
				MaxDiffInd = I;
			}
			Diffs[I] = Diff;
		}

		if (MaxDiff > InMaxTolerance)
		{
			UE_LOG(LogMetaHumanCoreTechLibTest, Display, TEXT("Max difference of %f between point in %s exceeds tolerance of %f for vertex %d"), MaxDiff, *InLabel, InMaxTolerance, MaxDiffInd);
			bResult = false;
		}

		const float MedianDiff = Diffs[Diffs.Num() / 2];
		if (MedianDiff > InMedianTolerance)
		{
			UE_LOG(LogMetaHumanCoreTechLibTest, Display, TEXT("Median difference of %f between point in %s exceeds tolerance of %f"), MedianDiff, *InLabel, InMedianTolerance);
			bResult = false;
		}
	}

	return bResult;
}

TArray<FVector3f> GetHeadMeshVertices(UDNAAsset* InDNAAsset)
{
#if WITH_EDITORONLY_DATA // can only use InDNAAsset->GetGeometryReader() in editor
	TArray<FVector3f> HeadMeshVertices;
	TSharedPtr<IDNAReader> GeometryReader = InDNAAsset->GetGeometryReader();
	HeadMeshVertices.SetNum(GeometryReader->GetVertexPositionCount(0));
	for (int32 I = 0; I < static_cast<int32>(GeometryReader->GetVertexPositionCount(0)); ++I)
	{
		HeadMeshVertices[I] = FVector3f{ static_cast<float>(GeometryReader->GetVertexPosition(0, I).X),
			static_cast<float>(GeometryReader->GetVertexPosition(0, I).Y), 
			static_cast<float>(GeometryReader->GetVertexPosition(0, I).Z) };
	}
	return HeadMeshVertices;
#else
	UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("MHC API only works with EditorOnly Data "));
	{
		return {};
	}
#endif
}


bool FTestMetaHumanCharacterIdentityTest::RunTest(const FString& InParameters)
{
	bool bResult = true;

	if (InParameters == "MetaHumanCharacterIdentity_MH") // potentially extend this to other supported variants of MH going forward
	{
		// load a Skel Mesh with an embedded DNA
		FString ExampleSkelMeshPath = FMetaHumanCommonDataUtils::GetFaceDNAFilesystemPath();
		FString MHCDataRelativePath = TEXT("/Content/Face/IdentityTemplate/");
		FString MHCBodyDataRelativePath = TEXT("/Content/Body/IdentityTemplate/");
		int32 ExpectedNumberOfPresets = 0;
		int32 ExpectedNumVertices = 69614;
		int32 ExpectedNumGizmos = 22;
		int32 ExpectedNumLandmarks = 79;

		USkeletalMesh* ExampleSkelMesh = UMetaHumanCharacterEditorSubsystem::GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType::MetaHuman);
		if (!ExampleSkelMesh)
		{
			UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to load SkeletalMesh asset from path: %s"), *ExampleSkelMeshPath);
			bResult = false;
		}
		else
		{
			// extract the DNA
			UAssetUserData* UserData = ExampleSkelMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass());
			if (UserData)
			{
				UDNAAsset* DNAAsset = Cast<UDNAAsset>(UserData);

				if (!DNAAsset)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to extract UDNAAsset data from SkeletalMesh asset from path: %s"), *ExampleSkelMeshPath);
					bResult = false;
				}
				else
				{
					const auto HeadOrientation = EMetaHumanCharacterOrientation::Y_UP; 
					FMetaHumanCharacterIdentity MetaHumanCharacterIdentity;
					FString MHCDataPath;
					FString MHCBodyDataPath;
					FString PluginDir = FPaths::ConvertRelativePathToFull(*IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetBaseDir());				

					if (!PluginDir.IsEmpty())
					{
						MHCDataPath = PluginDir + MHCDataRelativePath;
						MHCBodyDataPath = FMetaHumanCommonDataUtils::GetCombinedDNAFilesystemPath();
						TArray<FVector3f> OrigHeadMeshVertices = GetHeadMeshVertices(DNAAsset);

						bool bInitializedAPI = MetaHumanCharacterIdentity.Init(MHCDataPath, MHCBodyDataPath, DNAAsset, HeadOrientation);

						if (bInitializedAPI)
						{
							TArray<FString> PresetNames = MetaHumanCharacterIdentity.GetPresetNames();
							if (PresetNames.Num() != ExpectedNumberOfPresets)
							{
								UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Incorrect number of presets. Found %d, expected %d"), PresetNames.Num(), ExpectedNumberOfPresets);
								bResult = false;
							}

							TSharedPtr<FMetaHumanCharacterIdentity::FState> State = MetaHumanCharacterIdentity.CreateState();

							if (!State)
							{
								UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to create FMetaHumaneCharacterIdentity state"));
								bResult = false;
							}
							else
							{

								// test / exercise the API
								TArray<FVector3f> OrigVertices = State->Evaluate().Vertices;

								TArray<FVector3f> NewLandmarks, NewVertices;
								if (OrigVertices.Num() != ExpectedNumVertices)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of vertices in FState::Evaluate. Expected %d, got %d."), ExpectedNumVertices, OrigVertices.Num());
									bResult = false;
								}

								TArray<FVector3f> OrigGizmoPositions = State->EvaluateGizmos(OrigVertices);
								if (OrigGizmoPositions.Num() != ExpectedNumGizmos)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of gizmo positions in FState::EvaluateGizmos. Expected %d, got %d."), ExpectedNumGizmos, OrigGizmoPositions.Num());
									bResult = false;
								}

								TArray<FVector3f> OrigLandmarks = State->EvaluateLandmarks(OrigVertices);
								if (OrigLandmarks.Num() != ExpectedNumLandmarks)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of landmarks in FState::EvaluateLandmarks. Expected %d, got %d."), ExpectedNumLandmarks, OrigLandmarks.Num());
									bResult = false;
								}

								check(!State->HasLandmark(2)); // check there is not a landmark there already
								State->AddLandmark(2);
								NewLandmarks = State->EvaluateLandmarks(OrigVertices);
								if (NewLandmarks.Num() != ExpectedNumLandmarks + 2) // note that 2 landmarks are added due to use of symmetry
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of landmarks in FState::EvaluateLandmarks. Expected %d, got %d."), ExpectedNumLandmarks + 2, NewLandmarks.Num());
									bResult = false;
								}

								// add another landmark and then take it away
								check(!State->HasLandmark(1));
								State->AddLandmark(1);
								State->RemoveLandmark(State->NumLandmarks() - 1); // remove the last one we added; note that RemoveLandmark uses landmark indices and not vertex indices
								check(!State->HasLandmark(1));
								NewLandmarks = State->EvaluateLandmarks(OrigVertices);
								if (NewLandmarks.Num() != ExpectedNumLandmarks + 2)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of landmarks in FState::EvaluateLandmarks. Expected %d, got %d."), ExpectedNumLandmarks + 2, NewLandmarks.Num());
									bResult = false;
								}

								State->Reset();
								NewVertices = State->Evaluate().Vertices;
								bResult &= CheckValues(NewVertices, OrigVertices, FString(TEXT("vertices")), 0.000001f, 0.000001f);

								TArray<FVector3f> NewGizmoPositions = State->EvaluateGizmos(NewVertices);
								bResult &= CheckValues(NewGizmoPositions, OrigGizmoPositions, FString(TEXT("gizmos")), 0.000001f, 0.000001f);

								// at the moment we don't expect landmarks to be reset in terms of number, so should still be the same number as before the reset
								NewLandmarks = State->EvaluateLandmarks(OrigVertices);
								if (NewLandmarks.Num() != ExpectedNumLandmarks + 2)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of landmarks in FState::EvaluateLandmarks. Expected %d, got %d."), ExpectedNumLandmarks + 2, NewLandmarks.Num());
									bResult = false;
								}

								State->Randomize(/*magnitude*/ 0.5f);
								bResult &= CheckNumVertices(State, ExpectedNumVertices);

								FVector3f GizmoPosition;
								State->GetGizmoPosition(1, GizmoPosition);
								GizmoPosition += FVector3f{ 1.0f, 1.0f, 11.0f };
								State->SetGizmoPosition(1 /*gizmo index*/, GizmoPosition, /*bInSymmetric*/ true, /*bInEnforceBounds*/ true); // delta translation
								bResult &= CheckNumVertices(State, ExpectedNumVertices);

								FVector3f GizmoRotation;
								State->GetGizmoRotation(2, GizmoRotation);
								GizmoRotation += FVector3f{ 5.0f, -10.0f, 0.0f };
								State->SetGizmoRotation(2 /*gizmo index*/, GizmoRotation, /*bInSymmetric*/ true, /*bInEnforceBounds*/ true); // delta eulers
								bResult &= CheckNumVertices(State, ExpectedNumVertices);

								State->TranslateLandmark(10 /*landmark index*/, { 0.5f, -0.1f, 1.0f }, /*bInSymmetric*/ true); // delta translation
								bResult &= CheckNumVertices(State, ExpectedNumVertices);

								// reset the neck exclusion mask so fitting to target should give the same result for the head mesh vertices
								State->ResetNeckExclusionMask();

								// test FitToTarget to the original vertices to check that this is still giving correct results
								FFitToTargetOptions FitToTargetOptions;
								FitToTargetOptions.AlignmentOptions = EAlignmentOptions::None;
								FitToTargetOptions.bDisableHighFrequencyDelta = true;
								State->FitToTarget(TMap<int, TArray<FVector3f>>{ { 0, OrigHeadMeshVertices } }, FitToTargetOptions);
								NewVertices = State->Evaluate().Vertices;

								// use GetVertex to extract the head mesh vertices from the state and check that they are the same as originally
								TArray<FVector3f> NewHeadMeshVertices = OrigHeadMeshVertices; // just take a copy of the correct size
								for (int32 I = 0; I < NewHeadMeshVertices.Num(); ++I)
								{
									NewHeadMeshVertices[I] = State->GetVertex(NewVertices, /*DNAMeshIndex*/ 0, /*DNAVertexIndex*/ I);
								}

								bResult &= CheckValues(NewHeadMeshVertices, OrigHeadMeshVertices, FString(TEXT("head mesh vertices extracted from state after FitToTarget")), 0.001f, 0.001f);

								// test FitToTarget from DNA
								UDNAAsset* FaceDNAAsset = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Face, GetTransientPackage());
								check(FaceDNAAsset);
								TSharedRef<IDNAReader> FaceDNAReader = FaceDNAAsset->GetDnaReaderFromAsset();

								bResult &= State->FitToFaceDna(FaceDNAReader, FitToTargetOptions);

								// again compare against the original vertices
								for (int32 I = 0; I < NewHeadMeshVertices.Num(); ++I)
								{
									NewHeadMeshVertices[I] = State->GetVertex(NewVertices, /*DNAMeshIndex*/ 0, /*DNAVertexIndex*/ I);
								}
								bResult &= CheckValues(NewHeadMeshVertices, OrigHeadMeshVertices, FString(TEXT("head mesh vertices extracted from state after FitToTarget from DNA")), 0.001f, 0.001f);


								// test serialization and deserialization
								// IMPORTANT: variants, expressions, HF variant, face scale and global scale are not currently serialized
								// we may address this in future. For now, we test before we have changed any of these things
								FSharedBuffer Buffer;
								State->Serialize(Buffer);
								TSharedPtr<FMetaHumanCharacterIdentity::FState> State2 = MetaHumanCharacterIdentity.CreateState();
								bool bDeserialized = State2->Deserialize(Buffer);
								if (bDeserialized)
								{
									// compare the results of evaluating the state
									OrigVertices = State->Evaluate().Vertices;
									TArray<FVector3f> NewVertices2 = State2->Evaluate().Vertices;
									bool bCompare = CheckValues(OrigVertices, NewVertices2, FString(TEXT("state vertices")), 0.001f, 0.001f);
									bResult &= bCompare;
									if (!bCompare)
									{
										UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Deserialized face state vertices does not match original face state"));
									}

									TArray<FVector3f> OrigVertexNormals = State->Evaluate().VertexNormals;
									TArray<FVector3f> NewVertexNormals2 = State2->Evaluate().VertexNormals;
									bCompare = CheckValues(OrigVertexNormals, NewVertexNormals2, FString(TEXT("state vertex normals")), 0.001f, 0.001f);
									bResult &= bCompare;
									if (!bCompare)
									{
										UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Deserialized face state vertex normals does not match original face state"));
									}
								}
								else
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to deserialize the face state"));
									bResult = false;
								}

								// simple test of setting and getting gizmo scale
								const float GizmoScale = 0.9f;
								State->SetGizmoScale(/*InGizmoIndex*/1, GizmoScale, /*bInSymmetric*/ true, /*bInEnforceBounds*/ true);
								float GotGizmoScale;
								State->GetGizmoScale(/*InGizmoIndex*/1, GotGizmoScale);
								if (FMath::Abs(GizmoScale - GotGizmoScale) > 0.000001f)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected GizmoScale does not match actual GizmoScale"));
									bResult = false;
								}

								// simple test of setting and getting face scale 
								const float FaceScale = 0.9f;
								State->SetFaceScale(FaceScale);
								const float GotFaceScale = State->GetFaceScale();
								if (FMath::Abs(FaceScale - GotFaceScale) > 0.000001f)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected FaceScale does not match actual FaceScale"));
									bResult = false;
								}

								// simple test of getting global scale
								float GotGlobalScale;

								bool bGotGlobalScale = State->GetGlobalScale(GotGlobalScale);
								const float ExpectedGlobalScale = 0.9f; // same as for face scale above in this case
								if (!bGotGlobalScale || FMath::Abs(ExpectedGlobalScale - GotGlobalScale) > 0.000001f)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to get GlobalScale which matches expected GlobalScale"));
									bResult = false;
								}

								// simple test of GetModelIdentifier
								FString ModelIdentifier;
								State->GetModelIdentifier(ModelIdentifier);
								if (ModelIdentifier.IsEmpty())
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected a non-empty ModelIdentifier"));
									bResult = false;
								}

								// SelectFaceVertex, checking a case where we expect an intersection and one where we don't
								const FVector3f Origin = { 0, 0, 0 };
								FVector3f Direction = { 0, 0, 1 };
								FVector3f Vertex;
								FVector3f Normal;

								int32 VertexID = State->SelectFaceVertex(Origin, Direction, Vertex, Normal);
								if (VertexID == -1)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected a value for selected face vertex not of -1"));
									bResult = false;
								}

								Direction = { 0, 0, -1 };
								VertexID = State->SelectFaceVertex(Origin, Direction, Vertex, Normal);
								if (VertexID != -1)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected a value for selected face vertex of -1"));
									bResult = false;
								}

								// test HF variants
								const int32 NumHFVariants = State->GetNumHighFrequencyVariants();
								if (NumHFVariants == ExpectedNumberOfPresets)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Number of HF varients is incorrect"));
									bResult = false;
								}
								State->SetHighFrequencyVariant(4);
								if (State->GetHighFrequencyVariant() != 4)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("High Frequency variant is incorrect"));
									bResult = false;
								}

								// test setting expressions and variants; mainly simple smoke tests as no functions to get variants in the UE side wrapper
								const int32 NumEyelashVariants = State->GetVariantsCount("eyelashes");
								if (NumEyelashVariants != 6)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of eyelash variants"));
									bResult = false;
								}
								const int32 NumTeethVariants = State->GetVariantsCount("teeth");
								if (NumTeethVariants != 23)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of eyelash variants"));
									bResult = false;
								}

								TMap<FString, float> ExpressionActivations;
								ExpressionActivations.Add("jaw_open", 0.3f);
								ExpressionActivations.Add("McornerPull_Mstretch_MupperLipRaise_MlowerLipDepress_tgt", 0.5f);
								State->SetExpressionActivations(ExpressionActivations);

								TArray<float> EyelashesVariantsWeights = { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
								State->SetVariant("eyelashes", EyelashesVariantsWeights);
								TArray<float> TeethVariantsWeights = { 0.0f, 0.1f, 0.2f, 0.0f, 0.5f, 1.0f, 0.1f, 0.0f, 0.1f, 0.2f, 0.0f, 0.5f, 1.0f, 0.1f, 0.0f, 0.1f, 0.2f, 0.0f, 0.5f, 1.0f, 0.1f, 0.2f, 0.5f };
								State->SetVariant("teeth", TeethVariantsWeights);


								// simple test of GetCoefficients
								TArray<float> Coefficients;
								State->GetCoefficients(Coefficients);
								const int32 ExpectedNumCoefficients = 1397;
								if (Coefficients.Num() != ExpectedNumCoefficients)
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of Coefficients"));
									bResult = false;
								}

								// simple smoke test of blending with various options
								TSharedPtr<FMetaHumanCharacterIdentity::FState> State1 = MetaHumanCharacterIdentity.CreateState();
								State1->SetGizmoPosition(2 /*gizmo index*/, /*InGizmoPosition*/ { 0.5f, 1.0f, 2.0f }, /*bInSymmetric*/ true, /*bInEnforceBounds*/ true); // delta translation
								TSharedPtr<FMetaHumanCharacterIdentity::FState> State3 = MetaHumanCharacterIdentity.CreateState();
								State3->SetGizmoPosition(3 /*gizmo index*/, /*InGizmoPosition*/{ 0.1f, -1.0f, -3.0f }, /*bInSymmetric*/ true, /*bInEnforceBounds*/ true); // delta translation
								TArray<TPair<float, const FMetaHumanCharacterIdentity::FState*>> StatesToBlend = {{ 0.1f, State1.Get() }, { 0.9f, State3.Get() }};
								State->BlendPresets(/*InGizmoIndex*/ -1, StatesToBlend, EBlendOptions::Both, /*bInBlendSymmetrically*/ true);
								State->BlendPresets(/*InGizmoIndex*/ -1, StatesToBlend, EBlendOptions::Features, /*bInBlendSymmetrically*/ false);
								State->BlendPresets(/*InGizmoIndex*/ -1, StatesToBlend, EBlendOptions::Proportions, /*bInBlendSymmetrically*/ true);

								// simple smoke test of ResetNeckRegion
								State->ResetNeckRegion();

								// simple smoke test of ResetNeckExclusionMask
								State1->ResetNeckExclusionMask();

								// smoke test the two versions of StateToDna
								TSharedRef<IDNAReader> FaceDNAReader2 = State->StateToDna(FaceDNAAsset);
								TSharedRef<IDNAReader> FaceDNAReader3 = State->StateToDna(FaceDNAReader->Unwrap());

								// smoke test CopyBodyJointsToFace
								UDNAAsset* BodyDNAAsset = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, GetTransientPackage());
								check(BodyDNAAsset);
								TSharedRef<IDNAReader> BodyDNAReader = BodyDNAAsset->GetDnaReaderFromAsset();
								TSharedPtr<IDNAReader> FaceDNAReader4 = MetaHumanCharacterIdentity.CopyBodyJointsToFace(BodyDNAReader->Unwrap(), FaceDNAReader->Unwrap());

								// simple test of Get and SetSettings
								FMetaHumanCharacterIdentity::FSettings Settings = State->GetSettings();
								Settings.SetBodyDeltaInEvaluation(true);
								Settings.SetGlobalVertexDeltaScale(0.9f);
								Settings.SetGlobalHighFrequencyScale(0.8f);
								Settings.SetHighFrequencyIteration(5); // NB no way to get this atm
								State->SetSettings(Settings);
								FMetaHumanCharacterIdentity::FSettings Settings2 = State->GetSettings();
								const float Tolerance = 0.0000001f;
								if (FMath::Abs(Settings.GlobalVertexDeltaScale() - Settings2.GlobalVertexDeltaScale()) > Tolerance ||
									FMath::Abs(Settings.GlobalHighFrequencyScale() - Settings2.GlobalHighFrequencyScale()) > Tolerance ||
									Settings.UseBodyDeltaInEvaluation() != Settings2.UseBodyDeltaInEvaluation())
								{
									UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Get/SetSettings gives unexpected result"));
									bResult = false;
								}

								// test dump data for AR
								FString TestDirectoryPath = FPaths::ProjectSavedDir();
								State->WriteDebugAutoriggingData(TestDirectoryPath);
								// check that we have created the required files:
								// bind_pose.npy, eyeLeft_lod0_mesh.obj, eyeRight_lod0_mesh.obj, teeth_lod0_mesh.obj, head_lod0_mesh.obj, params.npy, targets.json
								TArray<FString> RequiredFiles = {
									"bind_pose.npy",
									"eyeLeft_lod0_mesh.obj",
									"eyeRight_lod0_mesh.obj",
									"teeth_lod0_mesh.obj",
									"head_lod0_mesh.obj",
									"params.npy",
									"targets.json"
								};

								for (int32 Index = 0; Index < RequiredFiles.Num(); ++Index)
								{
									FString FullPath = FPaths::Combine(TestDirectoryPath, RequiredFiles[Index]);
									if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FullPath))
									{
										UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("In WriteDebugAutoriggingData function, expected file %s does not exist"), *FullPath);
										bResult = false;
									}
								}


								// Simple smoke test of UpdateFaceSkinWeightsFromBodyAndVertexNormals
								// create a Body API in order to get the number of vertices per lod
								FMetaHumanCharacterBodyIdentity MetaHumanCharacterBodyIdentity;
								FString BodyPCAModelPath = PluginDir / TEXT("Content/Body/IdentityTemplate");
								FString LegacyBodiesPath = PluginDir / TEXT("Content/Optional/Body/FixedCompatibility");
								bool bInitializedBodyAPI = MetaHumanCharacterBodyIdentity.Init(BodyPCAModelPath, LegacyBodiesPath);
								check(bInitializedBodyAPI);
								TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterBodyIdentity.CreateState();
								TArray<int32> NumVerticesPerLod = BodyState->GetNumVerticesPerLOD();
								FFloatTriplet Triplet1(0, 5, 1.0f);
								FFloatTriplet Triplet2(0, 6, 1.0f);
								FFloatTriplet Triplet3(100, 7, 1.0f);
								TArray<TPair<int32, TArray<FFloatTriplet>>> CombinedBodySkinWeights =
								{
									{ NumVerticesPerLod[0], TArray<FFloatTriplet>{ Triplet1, Triplet2, Triplet3 }},
									{ NumVerticesPerLod[1], TArray<FFloatTriplet>{ Triplet1, Triplet2, Triplet3 }},
									{ NumVerticesPerLod[2], TArray<FFloatTriplet>{ Triplet1, Triplet2, Triplet3 }},
									{ NumVerticesPerLod[3], TArray<FFloatTriplet>{ Triplet1, Triplet2, Triplet3 }}
								};
								TSharedPtr<IDNAReader> FaceDNAReader5 = MetaHumanCharacterIdentity.UpdateFaceSkinWeightsFromBodyAndVertexNormals(CombinedBodySkinWeights, FaceDNAReader->Unwrap(), *State);


								// Simple smoke test of SetBodyVertexNormals
								const FMetaHumanRigEvaluatedState BodyVerticesAndVertexNormals = BodyState->GetVerticesAndVertexNormals();
								State->SetBodyVertexNormals(BodyVerticesAndVertexNormals.VertexNormals, NumVerticesPerLod);

								// Simple smoke test of SetBodyJointsAndBodyFaceVertices (called when updating face from body)
								TArray<FVector3f> BodyFaceVertices;
								State->SetBodyJointsAndBodyFaceVertices(BodyState->CopyBindPose(), BodyVerticesAndVertexNormals.Vertices);
							}
						}
						else
						{
							UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to initialize MetaHumanCharacterIdentity"));
							bResult = false;
						}
					}
					else
					{
						UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to find plugin directory"));
						bResult = false;
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected test: %s"), *InParameters);
		bResult = false;
	}

	return bResult;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTestMetaHumanCharacterBodyIdentityTest, "MetaHumanCreator.CoreTech.MetaHumanCharacterBodyIdentityTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FTestMetaHumanCharacterBodyIdentityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add("Basic test of MetaHumanCharacterBodyIdentity API for full MH");
	OutTestCommands.Add("MetaHumanCharacterBodyIdentity_MH");
}

bool FTestMetaHumanCharacterBodyIdentityTest::RunTest(const FString& InParameters)
{
	bool bResult = true;

	if (InParameters == "MetaHumanCharacterBodyIdentity_MH") // potentially extend this to other supported variants of MH going forward
	{
		FMetaHumanCharacterBodyIdentity MetaHumanCharacterBodyIdentity;
		FString BodyPCAModelPath;
		FString LegacyBodiesPath;
		FString PluginDir = FPaths::ConvertRelativePathToFull(*IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetBaseDir());

		if (!PluginDir.IsEmpty())
		{
			BodyPCAModelPath = PluginDir / TEXT("Content/Body/IdentityTemplate");
			LegacyBodiesPath = PluginDir / TEXT("Content/Optional/Body/FixedCompatibility");

			bool bInitializedAPI = MetaHumanCharacterBodyIdentity.Init(BodyPCAModelPath, LegacyBodiesPath);

			if (bInitializedAPI)
			{ 
				TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> State = MetaHumanCharacterBodyIdentity.CreateState();
				const TArray<FVector3f> Gizmos = State->GetRegionGizmos();
				if (Gizmos.Num() != 21)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of gizmos"));
					bResult = false;
				}

				// simple tests of basic functionality
				const TArray<PhysicsBodyVolume> PhysicsVolumesThigh = State->GetPhysicsBodyVolumes(FName(TEXT("thigh_l")));
				if (PhysicsVolumesThigh.Num() != 1)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of physics volumes for thigh_l"));
					bResult = false;
				}

				const TArray<PhysicsBodyVolume> PhysicsVolumesSpine04 = State->GetPhysicsBodyVolumes(FName(TEXT("spine_04")));
				if (PhysicsVolumesSpine04.Num() != 2)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of physics volumes for spine_04"));
					bResult = false;
				}

				// test getting and setting constraints
				const int32 ExpectedNumConstraints = 29;
				TArray<FMetaHumanCharacterBodyConstraint> Constraints = State->GetBodyConstraints();
				State->EvaluateBodyConstraints(Constraints);

				if (Constraints.Num() != ExpectedNumConstraints || State->GetNumberOfConstraints() != ExpectedNumConstraints)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of body constraints"));
					bResult = false;
				}

				for (int32 Constraint = 0; Constraint < Constraints.Num(); ++Constraint)
				{
					if (Constraints[Constraint].bIsActive)
					{
						UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expect all constraints to be inactive initially"));
						bResult = false;
					}
				}


				const FMetaHumanRigEvaluatedState OrigVerticesAndNormals = State->GetVerticesAndVertexNormals();
				const int32 ExpectedNumVertices = 73823;
				if (OrigVerticesAndNormals.Vertices.Num() != ExpectedNumVertices || OrigVerticesAndNormals.VertexNormals.Num() != ExpectedNumVertices)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of vertices or vertex normals from evaluated state"));
					bResult = false;
				}

				Constraints[1].bIsActive = true;
				Constraints[1].TargetMeasurement = Constraints[1].MaxMeasurement;
				State->EvaluateBodyConstraints(Constraints);

				FMetaHumanRigEvaluatedState UpdatedVerticesAndNormals = State->GetVerticesAndVertexNormals();

				// check that there is a difference between the original and updated vertices and normals
				bool bVerticesSame = CheckValues(OrigVerticesAndNormals.Vertices, UpdatedVerticesAndNormals.Vertices, FString(TEXT("body state vertices")), 0.001f, 0.001f);
				if (bVerticesSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertices to be different and they are the same"));
					bResult = false;
				}				
				bool bVertexNormalsSame = CheckValues(OrigVerticesAndNormals.VertexNormals, UpdatedVerticesAndNormals.VertexNormals, FString(TEXT("body state vertex normals")), 0.001f, 0.001f);
				if (bVertexNormalsSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertex normals to be different and they are the same"));
					bResult = false;
				}

				// remove the constraint and check get the original result
				Constraints[1].bIsActive = false;
				State->EvaluateBodyConstraints(Constraints);
				UpdatedVerticesAndNormals = State->GetVerticesAndVertexNormals();
				bVerticesSame = CheckValues(OrigVerticesAndNormals.Vertices, UpdatedVerticesAndNormals.Vertices, FString(TEXT("body state vertices")), 0.01f, 0.01f);
				if (!bVerticesSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertices to be the same and they are different"));
					bResult = false;
				}
				bVertexNormalsSame = CheckValues(OrigVerticesAndNormals.VertexNormals, UpdatedVerticesAndNormals.VertexNormals, FString(TEXT("body state vertex normals")), 0.01f, 0.01f);
				if (!bVertexNormalsSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertex normals to be the same and they are different"));
					bResult = false;
				}


				// add a constraint and check that the target measurement is similar to the actual measurement
				Constraints[5].bIsActive = true;
				const float TargetMeasurement = Constraints[5].MinMeasurement + (Constraints[5].MaxMeasurement - Constraints[5].MinMeasurement) * 0.3f;
				Constraints[5].TargetMeasurement = TargetMeasurement;
				State->EvaluateBodyConstraints(Constraints);
				const float ActualMeasurement = State->GetMeasurement(5);
				if (FMath::Abs(TargetMeasurement - ActualMeasurement) > 0.3f) // seem to have to use quite a large tolerance here
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected calculated measurement %f to be similar to target measurement %f and it is not"), ActualMeasurement, TargetMeasurement);
					bResult = false;
				}

				const TArray<FVector> ContourVertices = State->GetContourVertices(5);
				if (ContourVertices.IsEmpty())
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected measurement contour vertices to contain values and it does not"));
					bResult = false;
				}

				// test archiving and restoring and check that get the same result
				const FMetaHumanRigEvaluatedState BeforeArchiveVerticesAndNormals = State->GetVerticesAndVertexNormals();
				FSharedBuffer Archive;
				const bool bArchived = State->Serialize(Archive);
				if (!bArchived)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to archive state"));
					bResult = false;
				}
				TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> State2 = MetaHumanCharacterBodyIdentity.CreateState();
				const bool bRestored = State2->Deserialize(Archive);
				if (!bRestored)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to restore state"));
					bResult = false;
				}

				const FMetaHumanRigEvaluatedState AfterArchiveVerticesAndNormals = State2->GetVerticesAndVertexNormals();
				bVerticesSame = CheckValues(BeforeArchiveVerticesAndNormals.Vertices, AfterArchiveVerticesAndNormals.Vertices, FString(TEXT("body state vertices")), 0.01f, 0.01f);
				if (!bVerticesSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertices to be the same and they are different"));
					bResult = false;
				}
				bVertexNormalsSame = CheckValues(BeforeArchiveVerticesAndNormals.VertexNormals, AfterArchiveVerticesAndNormals.VertexNormals, FString(TEXT("body state vertex normals")), 0.01f, 0.01f);
				if (!bVertexNormalsSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertex normals to be the same and they are different"));
					bResult = false;
				}


				// call Reset and check get the same as the original result
				State->Reset();
				const FMetaHumanRigEvaluatedState ResetVerticesAndNormals = State->GetVerticesAndVertexNormals();
				bVerticesSame = CheckValues(OrigVerticesAndNormals.Vertices, ResetVerticesAndNormals.Vertices, FString(TEXT("body state vertices")), 0.01f, 0.01f);
				if (!bVerticesSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertices to be the same and they are different"));
					bResult = false;
				}
				bVertexNormalsSame = CheckValues(OrigVerticesAndNormals.VertexNormals, ResetVerticesAndNormals.VertexNormals, FString(TEXT("body state vertex normals")), 0.01f, 0.01f);
				if (!bVertexNormalsSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertex normals to be the same and they are different"));
					bResult = false;
				}

				const float GlobalDeltaScale = 0.1f;
				State->SetGlobalDeltaScale(GlobalDeltaScale);
				const float ActualGlobalDeltaScale = State->GetGlobalDeltaScale();
				if (FMath::Abs(GlobalDeltaScale - ActualGlobalDeltaScale) > 0.00001f) 
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected global delta scale different from actual global delta scale"));
					bResult = false;
				}

				const TArray<int32> NumVerticesPerLOD = State->GetNumVerticesPerLOD();
				if (NumVerticesPerLOD.Num() != 4 || NumVerticesPerLOD[0] != 54412 || NumVerticesPerLOD[1] != 13575 || NumVerticesPerLOD[2] != 4597 || NumVerticesPerLOD[3] != 1239)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of vertices per LOD result"));
					bResult = false;
				}

				// simple test of getting a vertex just to exercise the function
				const FVector3f Vertex = State->GetVertex(OrigVerticesAndNormals.Vertices, /*InDNAMeshIndex*/ 2, /*InDNAVertexIndex*/ 10);
				const FVector3f ExpectedVertex = { -20.9420f, 13.3903, 1.4788f };
				if (FMath::Abs((Vertex - ExpectedVertex).Length()) > 0.001f)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("GetVertex function returns unexpected value"));
					bResult = false;
				}

				// check number of joints
				const int32 NumJoints = State->GetNumberOfJoints();
				if (NumJoints != 342)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("GetNumberOfJoints function returns unexpected value"));
					bResult = false;
				}

				// copy the bind pose
				const TArray<FMatrix44f> BindPose = State->CopyBindPose();
				if (BindPose.Num() != NumJoints)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("CopyBindPose returned array of unexpected size"));
					bResult = false;
				}

				// test GetNeutralJointTransform
				for (int32 Joint = 0; Joint < NumJoints; ++Joint)
				{
					FVector3f JointTranslation;
					FRotator3f JointRotation;
					State->GetNeutralJointTransform( Joint, JointTranslation, JointRotation);
				}

				// test CopyCombinedModelVertexInfluenceWeights
				TArray<TPair<int32, TArray<FFloatTriplet>>> VertexInfluenceWeights;
				State->CopyCombinedModelVertexInfluenceWeights(VertexInfluenceWeights);
				if (VertexInfluenceWeights.Num() != NumVerticesPerLOD.Num())
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected number of vertex influence weights"));
					bResult = false;
				}

				for (int32 Index = 0; Index < VertexInfluenceWeights.Num(); ++Index)
				{
					for (int32 Weight = 0; Weight < VertexInfluenceWeights[Index].Value.Num(); ++Weight)
					{
						// the key of each pair is the number of vertices for that lod
						if (VertexInfluenceWeights[Index].Value[Weight].Row < 0 || VertexInfluenceWeights[Index].Value[Weight].Row >= VertexInfluenceWeights[Index].Key ||
							VertexInfluenceWeights[Index].Value[Weight].Col < 0 || VertexInfluenceWeights[Index].Value[Weight].Col >= NumJoints)
						{
							UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("VertexInfluenceWeight is out of range"));
							bResult = false;
						}
					}
				}

				// test blending presets
				// blend between two states across all gizmos and check that the output matches what we expect
				TArray<TPair<float, const FMetaHumanCharacterBodyIdentity::FState*>> BlendStates;

				// create two states, each with one constraint applied, and evaluate them
				TArray<FMetaHumanCharacterBodyConstraint> Constraints1 = Constraints;
				TArray<FMetaHumanCharacterBodyConstraint> Constraints2 = Constraints;
				TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> State1 = MetaHumanCharacterBodyIdentity.CreateState();
				State2 = MetaHumanCharacterBodyIdentity.CreateState();

				// ensure all constraints are turned off
				for (int32 Constraint = 0; Constraint < Constraints1.Num(); ++Constraint)
				{
					Constraints1[Constraint].bIsActive = false;
					Constraints2[Constraint].bIsActive = false;
				}
				// then set one constraint for each case
				Constraints1[3].bIsActive = true;
				Constraints1[3].TargetMeasurement = Constraints1[3].MinMeasurement + (Constraints1[3].MaxMeasurement - Constraints1[3].MinMeasurement) * 0.55f;
				Constraints2[1].bIsActive = true;
				Constraints2[1].TargetMeasurement = Constraints2[1].MinMeasurement + (Constraints2[1].MaxMeasurement - Constraints2[1].MinMeasurement) * 0.3f;

				State1->EvaluateBodyConstraints(Constraints1);
				State2->EvaluateBodyConstraints(Constraints2);

				BlendStates.Add(TPair<float, const FMetaHumanCharacterBodyIdentity::FState*>(0.0f, State1.Get()));
				BlendStates.Add(TPair<float, const FMetaHumanCharacterBodyIdentity::FState*>(1.0f, State2.Get()));

				// blend over all gizmos, giving a result which should match State2
				TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> NewState = MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*State);

				NewState->BlendPresets(/*InGizmoIndex*/ INDEX_NONE, BlendStates, EBodyBlendOptions::Both);

				// Compare the results
				FMetaHumanRigEvaluatedState NewStateVerticesAndNormals = NewState->GetVerticesAndVertexNormals();
				FMetaHumanRigEvaluatedState State2VerticesAndNormals = State2->GetVerticesAndVertexNormals();

				bVerticesSame = CheckValues(NewStateVerticesAndNormals.Vertices, State2VerticesAndNormals.Vertices, FString(TEXT("body state vertices")), 0.001f, 0.001f);
				if (!bVerticesSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertices to be the same and they are different"));
					bResult = false;
				}
				bVertexNormalsSame = CheckValues(NewStateVerticesAndNormals.VertexNormals, State2VerticesAndNormals.VertexNormals, FString(TEXT("body state vertex normals")), 0.001f, 0.001f);
				if (!bVertexNormalsSame)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expected vertex normals to be the same and they are different"));
					bResult = false;
				}

				// exercise the other two ways of blending, but don't compare the numerical results
				State->BlendPresets(/*InGizmoIndex*/ INDEX_NONE, BlendStates, EBodyBlendOptions::Skeleton);
				State->BlendPresets(/*InGizmoIndex*/ INDEX_NONE, BlendStates, EBodyBlendOptions::Shape);


				// test get and set the body type
				EMetaHumanBodyType BodyType = State->GetMetaHumanBodyType();

				if (BodyType != EMetaHumanBodyType::BlendableBody)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expecting BlendableBody type"));
					bResult = false;
				}

				// set a legacy body type
				State->SetMetaHumanBodyType(EMetaHumanBodyType::f_tal_nrw);
				BodyType = State->GetMetaHumanBodyType();

				if (BodyType != EMetaHumanBodyType::f_tal_nrw)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expecting f_tal_nrw type"));
					bResult = false;
				}

				// then fit to legacy
				State->SetMetaHumanBodyType(EMetaHumanBodyType::BlendableBody, /*bFitFromLegacy*/ true);
				BodyType = State->GetMetaHumanBodyType();
				if (BodyType != EMetaHumanBodyType::BlendableBody)
				{
					UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Expecting BlendableBody type"));
					bResult = false;
				}

				// test various methods to do with wrangling DNA
				UDNAAsset* BodyDNAAsset = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, GetTransientPackage());
				UDNAAsset* FaceDNAAsset = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Face, GetTransientPackage());


				if (BodyDNAAsset && FaceDNAAsset)
				{
					TSharedRef<IDNAReader> FaceDNAReader = FaceDNAAsset->GetDnaReaderFromAsset();
					TSharedRef<IDNAReader> BodyDNAReader = BodyDNAAsset->GetDnaReaderFromAsset();

					// test GetMeasurementsForFaceAndBody
					TMap<FString, float> Measurements;
					State->GetMeasurementsForFaceAndBody(FaceDNAReader, BodyDNAReader, Measurements);
					const int32 ExpectedNumMeasurements = 26;

					if (Measurements.Num() != ExpectedNumMeasurements)
					{
						UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Number of measurements does not match expected number"));
						bResult = false;
					}
					// NB we are not doing any numerical comparisons here, just exercising the functions
				}

			}
			else
			{
				UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Failed to initialize MetaHumanCharacterBodyIdentity"));
				bResult = false;
			}

		}

	}
	else
	{
		UE_LOG(LogMetaHumanCoreTechLibTest, Error, TEXT("Unexpected test: %s"), *InParameters);
		bResult = false;
	}

	return bResult;
}

#endif

