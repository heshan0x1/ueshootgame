// Copyright Epic Games, Inc. All Rights Reserved.
#include "JsonDomBuilder.h"
#include "Misc/ScopeLock.h"
#include "Cloud/MetaHumanTextureSynthesisServiceRequest.h"
#include "Cloud/MetaHumanCloudServicesSettings.h"
#include "MetaHumanDdcUtils.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "ImageUtils.h"
#include "Logging/StructuredLog.h"
#include "Async/Async.h"
#include "Misc/EngineVersion.h"

#include "DerivedDataCacheInterface.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheModule.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataValueId.h"
#include "Settings/EditorProjectSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanTextureSynthesisRequest, Log, All)

namespace UE::MetaHuman
{
#define MH_CLOUD_TEXTURE_SERVICE_API_VERSION "v1"

	namespace detail
	{
		class FTextureRequestContextBase : public FRequestContextBase
		{
		public:
			int32 Index = 0; // Index is not always used to build a request. For example, request using EBodyTextureType::Underwear_Basecolor
			int32 RequestedResolution = 1024;
			int32 TotalTextureCount = 0;

			FTextureRequestContextBase() = default;
			FTextureRequestContextBase(const FTextureRequestContextBase& Rhs)
				: Index(Rhs.Index),
				RequestedResolution(Rhs.RequestedResolution),
				TotalTextureCount(Rhs.TotalTextureCount)
			{
			}
		};

		template<typename EnumType>
		class TRequestContext : public FTextureRequestContextBase
		{
		public:
			TSharedPtr<THighFrequencyData<EnumType>> HighFrequencyData;
			EnumType Type;
			
			TRequestContext() = default;
			TRequestContext(const TRequestContext& Rhs)
				: FTextureRequestContextBase(Rhs),
				HighFrequencyData(Rhs.HighFrequencyData),
				Type(Rhs.Type)
			{
			}
		};

		using FFaceRequestContext = TRequestContext<EFaceTextureType>;
		using FBodyRequestContext = TRequestContext<EBodyTextureType>;
		
		enum ERequestCategory
		{
			Face,
			Body
		};
		
		// If any of the texture syntheis data we cache (DDC) changes in such a way as to invalidate old cache data this needs to be updated
#define TEXTURE_SYNTHESIS_DERIVEDDATA_VER TEXT("d4dab88926c44e6396a538e2f80e2735")
		FString GetCacheKey(ERequestCategory RequestCategory, int32 HighFrequency, int32 TypeIndex, int32 Resolution)
		{
			// if the data format provided by the TS service changes this version 
			// must also change in order to invalidate older DDC TS content
			return FString::Format(TEXT("UEMHCTS_{0}_{1}{2}{3}{4}"),
				{
					TEXTURE_SYNTHESIS_DERIVEDDATA_VER,
					static_cast<int32>(RequestCategory), HighFrequency, TypeIndex, Resolution >> 10u
				});
		}

		bool TryGetCachedData(TArray<uint8>& OutData, ERequestCategory RequestCategory, int32 Index, int32 TypeIndex, int32 Resolution)
		{
			const FString CacheKeyString = GetCacheKey(RequestCategory, Index, TypeIndex, Resolution);
			FSharedBuffer HighFrequencyDataBuffer = TryCacheFetch(CacheKeyString);
			if (!HighFrequencyDataBuffer.IsNull())
			{
				// in the cache, use it as is
				OutData.SetNumUninitialized(HighFrequencyDataBuffer.GetSize());
				FMemory::Memcpy(OutData.GetData(), HighFrequencyDataBuffer.GetData(), HighFrequencyDataBuffer.GetSize());
			}
			return !HighFrequencyDataBuffer.IsNull();
		}

		struct FAsyncTextureRequestorBase
		{
			DECLARE_DELEGATE_OneParam(FExecuteRequestDelegate, FRequestContextBasePtr);
			DECLARE_DELEGATE(FOnUnauthorizedDelegate);

			FExecuteRequestDelegate ExecuteRequestDelegate;
			FOnUnauthorizedDelegate OnUnauthorizedDelegate;
			FMetaHumanServiceRequestProgressDelegate MetaHumanServiceRequestProgressDelegate;
		};

		/**
		* Template for async requests of textures provided for Face and Body implementations
		*/
		template<uint8 CategoryId,
			typename TTextureType,
			typename THighFrequencyData,
			typename TRequestParams>
		struct TAsyncTextureRequestor : public FAsyncTextureRequestorBase, 
			public TSharedFromThis<TAsyncTextureRequestor<CategoryId, TTextureType, THighFrequencyData, TRequestParams>>
		{
			DECLARE_DELEGATE_OneParam(FCachedRequestCompleteDelegate, TSharedRef<THighFrequencyData>);	
			FCachedRequestCompleteDelegate CachedRequestCompleteDelegate;
			
			TAsyncTextureRequestor() = default;

			void RequestAsync(TSharedPtr<int32> RequestCounter,
				TConstArrayView<int> TextureRequestIndices,
				TConstArrayView<TRequestParams> TextureTypesToRequest) const
			{
				check(TextureTypesToRequest.Num());
				check(TextureTypesToRequest.Num() == TextureRequestIndices.Num());

				TSharedPtr<THighFrequencyData> HighFrequencyData = MakeShared<THighFrequencyData>();
				
				int32 RequestIndex = 0;
				for (const TRequestParams& RequestParams : TextureTypesToRequest)
				{
					// look up the texture in the DDC, or request one from the service
					// the code below aims to execute as much as possible off the game thread but some calls have to run on it, hence the frequent AsyncTask jumping

					const FString CacheKey = GetCacheKey(static_cast<ERequestCategory>(CategoryId), TextureRequestIndices[RequestIndex], static_cast<int32>(RequestParams.Type), RequestParams.Resolution);
					TryCacheFetchAsync(CacheKey, FOnFetchedCacheDataDelegate::CreateLambda(
						[
							HighFrequencyData,
							RequestParams,
							RequestCounter,
							TextureRequestIndex = TextureRequestIndices[RequestIndex],
							TextureCount = TextureTypesToRequest.Num(),
							SharedFromThis = this->AsShared()
						]
					(FSharedBuffer Data)
					{
						if (Data)
						{
							// in cache, use data directly 
							HighFrequencyData->GetMutable(RequestParams.Type).SetNumUninitialized(Data.GetSize());
							FMemory::Memcpy(HighFrequencyData->GetMutable(RequestParams.Type).GetData(), Data.GetData(), Data.GetSize());
							
							// Callback to be executed on the game thread. TaskGraphMainTick guarantees that the function
							// will be run at a safe place, i.e. outside GC, PostLoads and Flushing. The delegates will spawn
							// slow talk dialogs which are also going to flush the render thread. Not running on the main tick
							// may cause recursive flush calls and a potential crash.
							Async(EAsyncExecution::TaskGraphMainTick,
								  [
									  SharedFromThis,
									  RequestCounter,
									  TextureCount,
									  HighFrequencyDataSharedRef = HighFrequencyData.ToSharedRef()
								  ]
								  {
									  const bool bInvokeDelegate = ++(*RequestCounter) == TextureCount;
									  SharedFromThis->MetaHumanServiceRequestProgressDelegate.ExecuteIfBound(*RequestCounter / static_cast<float>(TextureCount));
									  if (bInvokeDelegate)
									  {
										  TSharedRef<THighFrequencyData> Response(HighFrequencyDataSharedRef);
										  SharedFromThis->CachedRequestCompleteDelegate.Execute(Response);
									  }
								  });
						}
						else
						{
							// not in cache, trigger a request to the service

							using TRequestContext = TRequestContext<TTextureType>;
							TSharedPtr<TRequestContext> Request = MakeShared<TRequestContext>();

							Request->HighFrequencyData = HighFrequencyData;
							Request->Type = RequestParams.Type;
							Request->Index = TextureRequestIndex;
							Request->RequestedResolution = RequestParams.Resolution;
							Request->TotalTextureCount = TextureCount;

							AsyncTask(ENamedThreads::Type::GameThread,
								[
									SharedFromThis,
									Request = MoveTemp(Request)
								]
								() mutable
								{
									ServiceAuthentication::CheckHasLoggedInUserAsync(ServiceAuthentication::FOnCheckHasLoggedInUserCompleteDelegate::CreateLambda(
										[
											SharedFromThis,
											Request = MoveTemp(Request)
										]
									(bool bIsLoggedIn, FString, FString)
										{
											if (bIsLoggedIn)
											{
												SharedFromThis->ExecuteRequestDelegate.Execute(Request);
											}
											else
											{
												AsyncTask(ENamedThreads::Type::GameThread, [SharedFromThis]()
													{
														SharedFromThis->OnUnauthorizedDelegate.ExecuteIfBound();
													});
											}
										}));
								});
						}
					}));

					RequestIndex++;
				}
			}
		};
		using FFaceAsyncTextureRequestor = TAsyncTextureRequestor<ERequestCategory::Face, EFaceTextureType, FFaceHighFrequencyData, FFaceTextureRequestParams>;
		using FBodyAsyncTextureRequestor = TAsyncTextureRequestor<ERequestCategory::Body, EBodyTextureType, FBodyHighFrequencyData, FBodyTextureRequestParams>;
	}
	using namespace detail;

	struct FTextureSynthesisServiceRequestBase::FImpl
	{
		union
		{
			FFaceTextureRequestCreateParams Face;
			FBodyTextureRequestCreateParams Body;
		}
		CreateParams;

		FImpl()
		{
			CompletedRequestCount = MakeShared<int32>(0);
		}

		ERequestCategory RequestCategory;
		TSharedPtr<int32> CompletedRequestCount;
		std::atomic<bool> bHasFailure = false;
	};

	TSharedRef<FFaceTextureSynthesisServiceRequest> FTextureSynthesisServiceRequestBase::CreateRequest(const FFaceTextureRequestCreateParams& Params)
	{
		TSharedRef<FFaceTextureSynthesisServiceRequest> Client = MakeShared<FFaceTextureSynthesisServiceRequest>();
		Client->Impl = MakePimpl<FImpl>();
		Client->Impl->RequestCategory = ERequestCategory::Face;
		Client->Impl->CreateParams.Face = Params;
		return Client;
	}

	TSharedRef<FBodyTextureSynthesisServiceRequest> FTextureSynthesisServiceRequestBase::CreateRequest(const FBodyTextureRequestCreateParams& Params)
	{
		TSharedRef<FBodyTextureSynthesisServiceRequest> Client = MakeShared<FBodyTextureSynthesisServiceRequest>();
		Client->Impl = MakePimpl<FImpl>();
		Client->Impl->RequestCategory = ERequestCategory::Body;
		Client->Impl->CreateParams.Body = Params;
		return Client;
	}

	bool FTextureSynthesisServiceRequestBase::DoBuildRequest(TSharedRef<IHttpRequest> HttpRequest, FRequestContextBasePtr Context)
	{
		check(Context.IsValid());
		const UMetaHumanCloudServicesSettings* Settings = GetDefault<UMetaHumanCloudServicesSettings>();		
		FString RequestUrl = Settings->TextureSynthesisServiceUrl + "/" + MH_CLOUD_TEXTURE_SERVICE_API_VERSION + "/versions/" + FString::FromInt(FEngineVersion::Current().GetMajor()) + "." + FString::FromInt(FEngineVersion::Current().GetMinor()) + "/areas";
		if (!DoBuildRequestImpl(RequestUrl, HttpRequest, Context))
		{
			return false;
		}

		HttpRequest->SetURL(RequestUrl);
		HttpRequest->SetVerb("GET");
		HttpRequest->SetHeader("Content-Type", TEXT("application/json"));
		HttpRequest->SetHeader("Accept-Encoding", TEXT("gzip"));

		return true;
	}

	void FTextureSynthesisServiceRequestBase::OnRequestFailed(EMetaHumanServiceRequestResult Result, FRequestContextBasePtr Context)
	{
		FTextureRequestContextBase* RequestDetails = reinterpret_cast<FTextureRequestContextBase*>(Context.Get());
		++(*Impl->CompletedRequestCount);
		bool bHasFailure = false;
		if (Impl->bHasFailure.compare_exchange_weak(bHasFailure, true))
		{
			// only invoke this once - subsequent failures (and successes) are quieted
			FMetaHumanServiceRequestBase::OnRequestFailed(Result, Context);
		}
	}

	// ========================================================================================================= Face

	void FFaceTextureSynthesisServiceRequest::UpdateHighFrequencyFaceTextureCacheAsync(FRequestContextBasePtr Context)
	{
		FFaceRequestContext* RequestDetails = reinterpret_cast<FFaceRequestContext*>(Context.Get());
		const FString CacheKeyString = GetCacheKey(ERequestCategory::Face, Impl->CreateParams.Face.HighFrequency, static_cast<int32>(RequestDetails->Type), RequestDetails->RequestedResolution);
		TConstArrayView<uint8> Data = (*RequestDetails->HighFrequencyData)[RequestDetails->Type];
		check(Data.Num());
		const FSharedBuffer SharedBuffer = FSharedBuffer::Clone(Data.GetData(), Data.Num());
		UpdateCacheAsync(CacheKeyString, FSharedString(TEXT("MetaHumanTextureSynthesis")), SharedBuffer);
	}

	void FFaceTextureSynthesisServiceRequest::RequestTexturesAsync(TConstArrayView<FFaceTextureRequestParams> TexturesToRequestParams)
	{
		check(IsInGameThread());
		TArray<int32> HighFrequencyIndices;
		HighFrequencyIndices.Init(Impl->CreateParams.Face.HighFrequency, TexturesToRequestParams.Num());
		
		TSharedPtr<FFaceAsyncTextureRequestor> FaceAsyncTextureRequestor = MakeShared<FFaceAsyncTextureRequestor>();
		FaceAsyncTextureRequestor->MetaHumanServiceRequestProgressDelegate = MetaHumanServiceRequestProgressDelegate;
		FaceAsyncTextureRequestor->OnUnauthorizedDelegate = FFaceAsyncTextureRequestor::FOnUnauthorizedDelegate::CreateSPLambda(this, [this]()
			{
				FTextureSynthesisServiceRequestBase::OnRequestFailed(EMetaHumanServiceRequestResult::Unauthorized, nullptr);
			});
		FaceAsyncTextureRequestor->CachedRequestCompleteDelegate = FFaceAsyncTextureRequestor::FCachedRequestCompleteDelegate::CreateSPLambda(this, [this](TSharedRef<FFaceHighFrequencyData> HighFrequencyData)
			{
				FaceTextureSynthesisRequestCompleteDelegate.ExecuteIfBound(HighFrequencyData);
			});
		FaceAsyncTextureRequestor->ExecuteRequestDelegate = FFaceAsyncTextureRequestor::FExecuteRequestDelegate::CreateSPLambda(this, [this](FRequestContextBasePtr Context)
			{
				FFaceTextureSynthesisServiceRequest::ExecuteRequestAsync(Context);
			});

		FaceAsyncTextureRequestor->RequestAsync(Impl->CompletedRequestCount, HighFrequencyIndices, TexturesToRequestParams);
	}
	
	bool FFaceTextureSynthesisServiceRequest::DoBuildRequestImpl(FString& InOutRequestUrl, TSharedRef<IHttpRequest> HttpRequest, FRequestContextBasePtr Context)
	{
		check(Context.IsValid());
		FFaceRequestContext* RequestDetails = reinterpret_cast<FFaceRequestContext*>(Context.Get());
		
		InOutRequestUrl += "/face/textureTypes";
		int32 AnimatedMapIndex = 0;
		bool bSupportedTextureType = true;
		switch (RequestDetails->Type)
		{
		case EFaceTextureType::Cavity:
			InOutRequestUrl += "/cavity";
			break;
		case EFaceTextureType::Normal:
		case EFaceTextureType::Normal_Animated_WM1:
		case EFaceTextureType::Normal_Animated_WM2:
		case EFaceTextureType::Normal_Animated_WM3:
			InOutRequestUrl += "/normal";
			AnimatedMapIndex = static_cast<int32>(RequestDetails->Type) - static_cast<int32>(EFaceTextureType::Normal);
			break;
		case EFaceTextureType::Basecolor:
		case EFaceTextureType::Basecolor_Animated_CM1:
		case EFaceTextureType::Basecolor_Animated_CM2:
		case EFaceTextureType::Basecolor_Animated_CM3:
			InOutRequestUrl += "/albedo";
			AnimatedMapIndex = static_cast<int32>(RequestDetails->Type) - static_cast<int32>(EFaceTextureType::Basecolor);
			break;
		default:
			bSupportedTextureType = false;
			break;
		}
		check(bSupportedTextureType);

		int32 HighFrequencyIndex = RequestDetails->Index;
		InOutRequestUrl += "/highFrequencyIds/" + FString::FromInt(HighFrequencyIndex);
		InOutRequestUrl += "/animatedMaps/" + FString::FromInt(AnimatedMapIndex);
		InOutRequestUrl += "/resolutions/" + FString::FromInt(RequestDetails->RequestedResolution >> 10u) + "k";

		return true;
	}

	void FFaceTextureSynthesisServiceRequest::OnRequestCompleted(const TArray<uint8>& Content, FRequestContextBasePtr Context)
	{
		FFaceRequestContext* RequestDetails = reinterpret_cast<FFaceRequestContext*>(Context.Get());
		{
			TArray<uint8>& Data = RequestDetails->HighFrequencyData->GetMutable(RequestDetails->Type);
			Data = Content;
			if ( Data.Num() )
			{
				if (!Impl->bHasFailure)
				{
					const bool bInvokeDelegate = ++(*Impl->CompletedRequestCount) == RequestDetails->TotalTextureCount;
					MetaHumanServiceRequestProgressDelegate.ExecuteIfBound(*Impl->CompletedRequestCount / static_cast<float>(RequestDetails->TotalTextureCount));
					if (bInvokeDelegate)
					{
						TSharedRef<FFaceHighFrequencyData> Response(RequestDetails->HighFrequencyData.ToSharedRef());
						FaceTextureSynthesisRequestCompleteDelegate.ExecuteIfBound(Response);
					}
				}
				// cache anything that succeeded (even if other textures have failed)
				UpdateHighFrequencyFaceTextureCacheAsync(Context);
			}
			else
			{
				// we don't really have much context in this case, but something invalid has come back from the server
				OnRequestFailed(EMetaHumanServiceRequestResult::ServerError, Context);
			}
		}
	}

	// ========================================================================================================= Body

	void FBodyTextureSynthesisServiceRequest::RequestTexturesAsync(TConstArrayView<FBodyTextureRequestParams> TexturesToRequestParams)
	{
		check(IsInGameThread());

		TArray<int> TextureRequestIndices;
		for (const FBodyTextureRequestParams& RequestParams : TexturesToRequestParams)
		{
			switch (RequestParams.Type)
			{
			case EBodyTextureType::Body_Basecolor:
			case EBodyTextureType::Chest_Basecolor:
				TextureRequestIndices.Add(Impl->CreateParams.Body.Tone);
				break;
			case EBodyTextureType::Body_Normal:
			case EBodyTextureType::Body_Cavity:
			case EBodyTextureType::Chest_Normal:
			case EBodyTextureType::Chest_Cavity:
				TextureRequestIndices.Add(Impl->CreateParams.Body.SurfaceMap);
				break;
			default:
				TextureRequestIndices.Add(0);
				break;
			}
		}

		TSharedPtr<FBodyAsyncTextureRequestor> BodyAsyncTextureRequestor = MakeShared<FBodyAsyncTextureRequestor>();
		BodyAsyncTextureRequestor->MetaHumanServiceRequestProgressDelegate = MetaHumanServiceRequestProgressDelegate;
		BodyAsyncTextureRequestor->OnUnauthorizedDelegate = FBodyAsyncTextureRequestor::FOnUnauthorizedDelegate::CreateSPLambda(this, [this]()
			{
				FTextureSynthesisServiceRequestBase::OnRequestFailed(EMetaHumanServiceRequestResult::Unauthorized, nullptr);
			});
		BodyAsyncTextureRequestor->CachedRequestCompleteDelegate = FBodyAsyncTextureRequestor::FCachedRequestCompleteDelegate::CreateSPLambda(this, [this](TSharedRef<FBodyHighFrequencyData> HighFrequencyData)
			{
				BodyTextureSynthesisRequestCompleteDelegate.ExecuteIfBound(HighFrequencyData);
			});
		BodyAsyncTextureRequestor->ExecuteRequestDelegate = FBodyAsyncTextureRequestor::FExecuteRequestDelegate::CreateSPLambda(this, [this](FRequestContextBasePtr Context)
			{
				FBodyTextureSynthesisServiceRequest::ExecuteRequestAsync(Context);
			});

		BodyAsyncTextureRequestor->RequestAsync(Impl->CompletedRequestCount, TextureRequestIndices, TexturesToRequestParams);
	}

	bool FBodyTextureSynthesisServiceRequest::DoBuildRequestImpl(FString& InOutRequestUrl, TSharedRef<IHttpRequest> HttpRequest, FRequestContextBasePtr Context)
	{
		check(Context.IsValid());
		FBodyRequestContext* RequestDetails = reinterpret_cast<FBodyRequestContext*>(Context.Get());
		
		bool bSupportedTextureType = true;
		switch (RequestDetails->Type)
		{
		case EBodyTextureType::Body_Basecolor:
		case EBodyTextureType::Body_Normal:
		case EBodyTextureType::Body_Cavity:
		case EBodyTextureType::Body_Underwear_Basecolor:
		case EBodyTextureType::Body_Underwear_Normal:
		case EBodyTextureType::Body_Underwear_Mask:
			InOutRequestUrl += "/body/textureTypes";
			break;
		case EBodyTextureType::Chest_Basecolor:
		case EBodyTextureType::Chest_Normal:
		case EBodyTextureType::Chest_Cavity:
		case EBodyTextureType::Chest_Underwear_Basecolor:
		case EBodyTextureType::Chest_Underwear_Normal:
			InOutRequestUrl += "/chest/textureTypes";
			break;
		default:
			bSupportedTextureType = false;
			break;
		}

		switch (RequestDetails->Type)
		{
		case EBodyTextureType::Body_Basecolor:
		case EBodyTextureType::Chest_Basecolor:
			InOutRequestUrl += "/albedo/tones" ;
			break;
		case EBodyTextureType::Body_Normal:
		case EBodyTextureType::Chest_Normal:
			InOutRequestUrl += "/normal/surfaceMaps";
			break;
		case EBodyTextureType::Body_Cavity:
		case EBodyTextureType::Chest_Cavity:
			InOutRequestUrl += "/cavity/surfaceMaps";
			break;
		case EBodyTextureType::Body_Underwear_Basecolor:
		case EBodyTextureType::Body_Underwear_Normal:
		case EBodyTextureType::Body_Underwear_Mask:
		case EBodyTextureType::Chest_Underwear_Basecolor:
		case EBodyTextureType::Chest_Underwear_Normal:
			InOutRequestUrl += "/underwear/subTypes";
			break;
		default:
			bSupportedTextureType = false;
			break;
		}

		check(bSupportedTextureType);

		switch (RequestDetails->Type)
		{
		case EBodyTextureType::Body_Underwear_Basecolor:
		case EBodyTextureType::Chest_Underwear_Basecolor:
			InOutRequestUrl += "/albedo";
			break;
		case EBodyTextureType::Body_Underwear_Normal:
		case EBodyTextureType::Chest_Underwear_Normal:
			InOutRequestUrl += "/normal";
			break;
		case EBodyTextureType::Body_Underwear_Mask:
			InOutRequestUrl += "/mask";
			break;
		default:
			break;
		}

		switch (RequestDetails->Type)
		{
		case EBodyTextureType::Body_Basecolor:
		case EBodyTextureType::Body_Normal:
		case EBodyTextureType::Body_Cavity:
		case EBodyTextureType::Chest_Basecolor:
		case EBodyTextureType::Chest_Normal:
		case EBodyTextureType::Chest_Cavity:
			InOutRequestUrl += "/" + FString::FromInt(RequestDetails->Index);
			break;
		default:
			break;
		}
		
		InOutRequestUrl += "/resolutions/" + FString::FromInt(RequestDetails->RequestedResolution >> 10u) + "k";
		
		return true;
	}

	void FBodyTextureSynthesisServiceRequest::UpdateHighFrequencyBodyTextureCacheAsync(FRequestContextBasePtr Context)
	{
		FBodyRequestContext* RequestDetails = reinterpret_cast<FBodyRequestContext*>(Context.Get());
		const FString CacheKeyString = GetCacheKey(ERequestCategory::Body, RequestDetails->Index, static_cast<int32>(RequestDetails->Type), RequestDetails->RequestedResolution);
		TConstArrayView<uint8> Data = (*RequestDetails->HighFrequencyData)[RequestDetails->Type];
		check(Data.Num());
		const FSharedBuffer SharedBuffer = FSharedBuffer::Clone(Data.GetData(), Data.Num());
		UpdateCacheAsync(CacheKeyString, FSharedString(TEXT("MetaHumanTextureSynthesis")), SharedBuffer);
	}

	void FBodyTextureSynthesisServiceRequest::OnRequestCompleted(const TArray<uint8>& Content, FRequestContextBasePtr Context)
	{
		FBodyRequestContext* RequestDetails = reinterpret_cast<FBodyRequestContext*>(Context.Get());
		{
			TArray<uint8>& Data = RequestDetails->HighFrequencyData->GetMutable(RequestDetails->Type);
			Data = Content;
			if (Data.Num())
			{
				if (!Impl->bHasFailure)
				{
					const bool bInvokeDelegate = ++(*Impl->CompletedRequestCount) == RequestDetails->TotalTextureCount;
					MetaHumanServiceRequestProgressDelegate.ExecuteIfBound(*Impl->CompletedRequestCount / static_cast<float>(RequestDetails->TotalTextureCount));
					if (bInvokeDelegate)
					{
						TSharedRef<FBodyHighFrequencyData> Response(RequestDetails->HighFrequencyData.ToSharedRef());
						BodyTextureSynthesisRequestCompleteDelegate.ExecuteIfBound(Response);
					}
				}
				// cache anything that succeeded (even if other textures have failed)
				UpdateHighFrequencyBodyTextureCacheAsync(Context);
			}
			else
			{
				// we don't really have much context in this case, but something invalid has come back from the server
				OnRequestFailed(EMetaHumanServiceRequestResult::ServerError, Context);
			}
		}
	}
}
