// Copyright Epic Games, Inc. All Rights Reserved.
#include "Cloud/MetaHumanARServiceRequest.h"
#include "Cloud/MetaHumanCloudServicesSettings.h"
#include "Cloud/MetaHumanDdcUtils.h"
#include "Logging/StructuredLog.h"
#include "Containers/SharedString.h"
#include "Misc/EngineVersion.h"
#include "DNAUtils.h"
#include "DNACommon.h"
#include "DNAReader.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

THIRD_PARTY_INCLUDES_START
#pragma push_macro("verify")
#undef verify
#pragma warning(disable:4068)
#pragma pvs(push)
#pragma pvs(disable: 590)
#pragma pvs(disable: 568)
#include "Proto/metahuman_service_api.pb.cc.inc"
#pragma pvs(pop)
#pragma pop_macro("verify")
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogAutorigServiceRequest, Log, All)

static TAutoConsoleVariable<bool> CVarSaveAutorigProtobuffers(
	TEXT("MetaHuman.Cloud.Autorig.SaveProtobuffers"),
	false,
	TEXT("Enable or disable saving protobuffer contents in project directory"),
	ECVF_Default
);

static TAutoConsoleVariable<bool> CVarAutorigUseDdc(
	TEXT("MetaHuman.Cloud.Autorig.UseDdc"),
	false,
	TEXT("Enable or disable using DDC for autorig requests"),
	ECVF_Default
);

namespace UE::MetaHuman
{
	/** Number of vertices of the face mesh on the LOD0. */
	static const int32 FaceMeshVertexCount = 24049;
	/** Number of vertices of the eye mesh on the LOD0. */
	static const int32 EyeMeshVertexCount = 770;

	struct FAutorigRequestContext : FRequestContextBase
	{
		FAutorigRequestContext(TSharedRef<FMetaHumanServiceRequestBase> Owner)
			: FRequestContextBase(Owner)
		{
		}
		TArray<uint8> ProtobufPayload;
		FString CacheKey;
	};

	void FAutoRigServiceRequest::RequestSolveAsync()
	{
		TSharedPtr<FAutorigRequestContext> RequestContext = MakeShared<FAutorigRequestContext>(AsShared());
		
		const auto FillProtoMesh = [](metahuman_service_api::Mesh* Mesh, const TArray<FVector>& SourceArray) -> bool
			{
				for (const FVector& Vector : SourceArray)
				{
					if (FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z))
					{
						metahuman_service_api::Vertex* vertex = Mesh->add_vertices();
						vertex->set_x(Vector.X);
						vertex->set_y(Vector.Y);
						vertex->set_z(Vector.Z);
					}
					else
					{
						UE_LOGFMT(LogAutorigServiceRequest, Warning, "Invalid FVector input to autorigger");
						return false;
					}
				}
				return true;
			};

	// build Protobuf message
		{
#define MH_ARS_ALLOCATE_MESSAGE(Type, Name)\
metahuman_service_api::Type * Name = new metahuman_service_api::Type

			MH_ARS_ALLOCATE_MESSAGE(Head, Head);
			MH_ARS_ALLOCATE_MESSAGE(Mesh, Face);
			FillProtoMesh(Face, SolveParameters.ConformedFaceVertices);
			Head->set_allocated_face(Face);

			MH_ARS_ALLOCATE_MESSAGE(Eyes, Eyes);
			MH_ARS_ALLOCATE_MESSAGE(Mesh, LeftEye);
			FillProtoMesh(LeftEye, SolveParameters.ConformedLeftEyeVertices);
			MH_ARS_ALLOCATE_MESSAGE(Mesh, RightEye);
			FillProtoMesh(RightEye, SolveParameters.ConformedRightEyeVertices);
			MH_ARS_ALLOCATE_MESSAGE(Mesh, Shell);
			FillProtoMesh(Shell, SolveParameters.ConformedEyeShellVertices);
			MH_ARS_ALLOCATE_MESSAGE(Mesh, Lashes);
			FillProtoMesh(Lashes, SolveParameters.ConformedEyeLashesVertices);
			MH_ARS_ALLOCATE_MESSAGE(Mesh, Edge);
			FillProtoMesh(Edge, SolveParameters.ConformedEyeEdgeVertices);
			Eyes->set_allocated_edge(Edge);
			Eyes->set_allocated_left(LeftEye);
			Eyes->set_allocated_right(RightEye);
			Eyes->set_allocated_lashes(Lashes);
			Eyes->set_allocated_shell(Shell);
			Head->set_allocated_eyes(Eyes);

			MH_ARS_ALLOCATE_MESSAGE(Mesh, Teeth);
			FillProtoMesh(Teeth, SolveParameters.ConformedTeethVertices);
			Head->set_allocated_teeth(Teeth);

			MH_ARS_ALLOCATE_MESSAGE(Mesh, Cartilage);
			FillProtoMesh(Cartilage, SolveParameters.ConformedCartilageVertices);
			Head->set_allocated_cartilage(Cartilage);

			MH_ARS_ALLOCATE_MESSAGE(Mesh, Saliva);
			FillProtoMesh(Saliva, SolveParameters.ConformedSalivaVertices);
			Head->set_allocated_saliva(Saliva);

			metahuman_service_api::AutorigRequest ProtoRequest;
			ProtoRequest.set_allocated_head(Head);
			ProtoRequest.set_high_frequency_index(SolveParameters.HighFrequency);
			ProtoRequest.set_to_target_scale(SolveParameters.Scale);

			if (SolveParameters.BindPose.Num() && SolveParameters.Coefficients.Num())
			{
				MH_ARS_ALLOCATE_MESSAGE(Parameters, Parameters);
				*Parameters->mutable_bind_pose() = { SolveParameters.BindPose.GetData(), SolveParameters.BindPose.GetData() + SolveParameters.BindPose.Num() };
				*Parameters->mutable_solver_coefficients() = { SolveParameters.Coefficients.GetData(), SolveParameters.Coefficients.GetData() + SolveParameters.Coefficients.Num() };
				Parameters->set_model_id(TCHAR_TO_UTF8(*SolveParameters.ModelIdentifier));
				ProtoRequest.set_allocated_parameters(Parameters);
			}

			MH_ARS_ALLOCATE_MESSAGE(Quality, Quality);
			switch (SolveParameters.RigType)
			{
			case ERigType::JointsAndBlendshapes:
				Quality->set_rig_type(metahuman_service_api::RIG_TYPE_JOINTS_AND_BLENDSHAPES);
				break;
			case ERigType::JointsOnly:
			default:
				Quality->set_rig_type(metahuman_service_api::RIG_TYPE_JOINTS_ONLY);
				break;
			}
			switch (SolveParameters.RigRefinementLevel)
			{
			case ERigRefinementLevel::Medium:
				Quality->set_refinement_level(metahuman_service_api::REFINEMENT_LEVEL_MEDIUM);
				break;
			case ERigRefinementLevel::None:
			default:
				Quality->set_refinement_level(metahuman_service_api::REFINEMENT_LEVEL_NONE);
				break;
			}
			switch (SolveParameters.ExportLayers)
			{
			case EExportLayers::Rbf:
				Quality->set_export_layers(metahuman_service_api::ExportLayers::EXPORT_LAYERS_RBF);
				break;
			case EExportLayers::None:
				Quality->set_export_layers(metahuman_service_api::ExportLayers::EXPORT_LAYERS_NONE);
				break;
			case EExportLayers::Default:
			default:
				Quality->set_export_layers(metahuman_service_api::ExportLayers::EXPORT_LAYERS_UNKNOWN);
				break;
			}

			ProtoRequest.set_allocated_quality(Quality);

			MH_ARS_ALLOCATE_MESSAGE(UEVersion, UEVersion);
			UEVersion->set_minor(FEngineVersion::Current().GetMinor());
			UEVersion->set_major(FEngineVersion::Current().GetMajor());
			ProtoRequest.set_allocated_ue_version(UEVersion);

			const size_t SizeOfProtoRequest = ProtoRequest.ByteSizeLong();
			RequestContext->ProtobufPayload.SetNumUninitialized(SizeOfProtoRequest);
			ProtoRequest.SerializeToArray(RequestContext->ProtobufPayload.GetData(), SizeOfProtoRequest);
		}

		// check the DDC first, in case the user has just requested a new autorig when nothig has really changed
		FSHA1 Sha1;
		Sha1.Update(RequestContext->ProtobufPayload.GetData(), RequestContext->ProtobufPayload.NumBytes());
		const FSHAHash HashedPayload = Sha1.Finalize();
		const FString HashedPayloadStr = HashedPayload.ToString();

		if( CVarSaveAutorigProtobuffers.GetValueOnAnyThread() )
		{
			// save out as <HASH>.pb.bin
			const FString PayloadSaveName = FPaths::ProjectDir() / (HashedPayloadStr + TEXT(".pb.bin"));
			FFileHelper::SaveArrayToFile(RequestContext->ProtobufPayload, *PayloadSaveName);
		}
#define AUTORIGSERVICE_DERIVEDDATA_VER TEXT("UEMHCAR_41a2767b4ae049ecb5a4bea25afdb736_")
		FString CacheKey = AUTORIGSERVICE_DERIVEDDATA_VER + HashedPayloadStr;
		bool bFetchFromService = true;
		if (CVarAutorigUseDdc.GetValueOnAnyThread())
		{
			FSharedBuffer DNABuffer = TryCacheFetch(CacheKey);
			if (!DNABuffer.IsNull())
			{
				TArray<uint8> TempBuffer;
				TempBuffer.SetNumUninitialized(DNABuffer.GetSize());
				FMemory::Memcpy(TempBuffer.GetData(), DNABuffer.GetData(), DNABuffer.GetSize());
				MetaHumanServiceRequestProgressDelegate.ExecuteIfBound(1.0f);
				OnRequestCompleted(TempBuffer, RequestContext);
				bFetchFromService = false;
			}
		}
		
		if(bFetchFromService)
		{
			// set the key so that the cache can be updated with the response
			RequestContext->CacheKey = MoveTemp(CacheKey);
			ExecuteRequestAsync(RequestContext);
		}
	}

	TSharedRef<FAutoRigServiceRequest> FAutoRigServiceRequest::CreateRequest(const FTargetSolveParameters& InSolveParams)
	{
		TSharedRef<FAutoRigServiceRequest> Request = MakeShared<FAutoRigServiceRequest>();
		Request->SolveParameters = InSolveParams;
		return Request;
	}

	bool FAutoRigServiceRequest::DoBuildRequest(TSharedRef<IHttpRequest> HttpRequest, FRequestContextBasePtr Context)
	{
		FAutorigRequestContext* RequestContext = reinterpret_cast<FAutorigRequestContext*>(Context.Get());
		const UMetaHumanCloudServicesSettings* Settings = GetDefault<UMetaHumanCloudServicesSettings>();
		HttpRequest->SetVerb("POST");
		HttpRequest->SetURL(Settings->AutorigServiceUrl);
		HttpRequest->SetHeader("Content-Type", TEXT("application/octet-stream"));
		HttpRequest->SetContent(MoveTemp(RequestContext->ProtobufPayload));
		
		return true;
	}

	void FAutoRigServiceRequest::OnRequestCompleted(const TArray<uint8>& Content, FRequestContextBasePtr Context)
	{
		FAutorigRequestContext* RequestContext = reinterpret_cast<FAutorigRequestContext*>(Context.Get());
		FAutorigResponse Response(Content);
		if (Response.IsValid())
		{
			if (CVarAutorigUseDdc.GetValueOnAnyThread())
			{
				if (!RequestContext->CacheKey.IsEmpty())
				{
					const FSharedBuffer SharedBuffer = FSharedBuffer::Clone(Content.GetData(), Content.Num());
					UpdateCacheAsync(RequestContext->CacheKey, FSharedString(TEXT("MetaHumanAutorigService")), SharedBuffer);
				}
			}
			AutorigRequestCompleteDelegate.ExecuteIfBound(Response);
		}
		else
		{
			UE_LOGFMT(LogAutorigServiceRequest, Error, "Service returned invalid DNA");
			OnRequestFailed(EMetaHumanServiceRequestResult::ServerError, Context);
		}
	}

	// ========================================================================

	bool FAutorigResponse::ReadDna()
	{
		TArray<uint8> PayloadCopy;
		PayloadCopy.SetNumUninitialized(Payload.Num());
		FMemory::Memcpy(PayloadCopy.GetData(), Payload.GetData(), Payload.Num());
		Dna = ReadDNAFromBuffer(&PayloadCopy, EDNADataLayer::All);
		return Dna != nullptr;
	}
}