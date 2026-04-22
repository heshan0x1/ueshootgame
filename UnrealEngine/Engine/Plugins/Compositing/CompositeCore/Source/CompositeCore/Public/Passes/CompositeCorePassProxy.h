// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/Array.h"
#include "Containers/ContainersFwd.h"
#include "Misc/Optional.h"
#include "Misc/TVariant.h"
#include "SceneViewExtension.h"
#include "SceneTexturesConfig.h"
#include "ScreenPass.h"

#define UE_API COMPOSITECORE_API

class FRDGBuilder;
struct FRDGTextureDesc;
class FSceneView;
struct FScreenPassTexture;
struct FScreenPassRenderTarget;
struct FPostProcessMaterialInputs;
class FCompositeCorePassProxy;
struct IPooledRenderTarget;
class UTexture;
class USceneCaptureComponent2D;

enum class EPostProcessMaterialInput : uint32;

namespace UE
{
	namespace CompositeCore
	{
		/** Texture encoding type, used for scene color. (HDR is not currently supported.) */
		enum class EEncoding : uint32
		{
			Linear = 0,
			Gamma = 1,
			sRGB = 2,
		};

		/** Type used to identify passes, textures or built-in renderer sources/targets. */
		using ResourceId = uint32;
		/** Built-in custom render pass identifier. */
		static constexpr ResourceId BUILT_IN_CRP_ID = 1;
		/** Built-in empty/black identifier. */
		static constexpr ResourceId BUILT_IN_EMPTY_ID = 2;
		/** First identifier of the external texture inputs, see ExternalInputs on FRenderWork. */
		static constexpr ResourceId EXTERNAL_RANGE_START_ID = 100;

		/** Texture resource metadata. */
		struct FResourceMetadata
		{
			/** Is the alpha inverted (like scene color)? */
			bool bInvertedAlpha = false;

			/** Is the texture content distorted? */
			bool bDistorted = false;

			/** Is the texture's exposure already adjusted? */
			bool bPreExposed = false;
			
			/** Source color encoding. */
			EEncoding Encoding = EEncoding::Linear;
			
			/** Equality operator */
			bool operator==(const FResourceMetadata& Other) const = default;
			
			/** Inequality operator */
			bool operator!=(const FResourceMetadata& Other) const = default;
		};

		/** Pass texture description for internal resources (default scene textures). */
		struct FPassInternalResourceDesc
		{
			/** Index, which maps to the default 0-4 post-processing inputs or beyond. */
			int32 Index = 0;
			
			/** Flag to bypass the previous pass textures & access the original scene textures. */
			bool bOriginalCopyBeforePasses = false;
		};

		/** Pass texture description for external render targets. */
		struct FPassExternalResourceDesc
		{
			/** External resource identifier. */
			ResourceId Id = 0;
		};

		/** Pass input declaration, referring to internal textures, external textures or the output of another pass proxy. */
		using FPassInputDecl = TVariant<FPassInternalResourceDesc, FPassExternalResourceDesc, const FCompositeCorePassProxy*>;
		
		/** Array of pass input declarations. */
		using FPassInputDeclArray = TArray<FPassInputDecl>;

		/** Returns a default input array declaring one internal texture resource. */
		UE_API FPassInputDeclArray GetDefaultInputDeclArray();

		/** External texture resource and its accompanying metadata. */
		struct FExternalTexture
		{
			/** Texture weak object pointer, for use on the game thread. */
			TWeakObjectPtr<UTexture> Texture = {};
			
			/** Texture metadata. */
			FResourceMetadata Metadata = {};
		};

		/** Resolved texture resource with an active (screen) texture and its accompanying metadata. */
		struct FPassTexture
		{
			/** Pass screen texture. */
			FScreenPassTexture Texture = {};
			
			/** Texture metadata. */
			FResourceMetadata Metadata = {};
		};

		/** Pass texture input definition. */
		using FPassInput = FPassTexture;

		/** Resolved pass inputs. */
		struct FPassInputArray
		{
			/** Default constructor */
			FPassInputArray() = default;
			FPassInputArray(FPassInputArray&&) = default;
			FPassInputArray(const FPassInputArray&) = default;
			FPassInputArray& operator=(FPassInputArray&&) = default;
			FPassInputArray& operator=(const FPassInputArray&) = default;

			/** Constructor */
			UE_API FPassInputArray(
				FRDGBuilder& GraphBuilder,
				const FSceneView& InView,
				const FPostProcessMaterialInputs& InPostInputs,
				const ISceneViewExtension::EPostProcessingPass& InLocation
			);

			/** Array access operator overload */
			const FPassInput& operator[] (int32 Index) const
			{
				return Inputs[Index];
			}

			/** Array access operator overload */
			FPassInput& operator[] (int32 Index)
			{
				return Inputs[Index];
			}
			
			/** Tests if index is valid, i.e. greater than or equal to zero, and less than the number of elements in the array. */
			bool IsValidIndex(int32 Index) const
			{
				return Inputs.IsValidIndex(Index);
			}

			/** Number of pass inputs */
			int32 Num() const { return Inputs.Num(); }

			/** Input array getter. */
			TArray<UE::CompositeCore::FPassInput>& GetArray() { return Inputs; }

			/** Input array const getter. */
			const TArray<UE::CompositeCore::FPassInput>& GetArray() const { return Inputs; }

			/** Conversion function to engine post-process (material) inputs. */
			UE_API FPostProcessMaterialInputs ToPostProcessInputs(FRDGBuilder& GraphBuilder, FSceneTextureShaderParameters SceneTextures) const;

			/** Pass override output. */
			FScreenPassRenderTarget OverrideOutput;

		private:
			/** Pass texture input array. */
			TArray<UE::CompositeCore::FPassInput> Inputs;
		};

		/** Pass parameter information. */
		struct FPassContext
		{
			/** The uniform buffer containing all scene textures. */
			FSceneTextureShaderParameters SceneTextures;

			/** Active post-processing output view rectangle. */
			FIntRect OutputViewRect;
			
			/** Post-processiong location. */
			ISceneViewExtension::EPostProcessingPass Location;
			
			/** Is the current pass expected to output scene color? */
			bool bOutputSceneColor = false;
		};

		/** Options to control the built-in custom render pass. */
		struct FBuiltInRenderPassOptions final
		{
			/** Constructor. */
			UE_API FBuiltInRenderPassOptions();

			/** Custom user flags value used to alter materials in the composite render pass. */
			TOptional<int32> ViewUserFlagsOverride;

			/** Enables the development shader debug feature that routes the Base Color output to Emissive for the separate render. Non-shipping PC build only. */
			bool bEnableUnlitViewmode;

			/** Enables the dilation pass with size > 0 (currently only 1 is supported). */
			int32 DilationSize;

			/** Opacify to extract the solid colors behind translucent alpha holdout masks. */
			bool bOpacifyOutput;
		};

		/** Render-thread struct for scene view extension render work per frame. */
		struct FRenderWork final
		{
			/** Default frame work. */
			static UE_API const FRenderWork& GetDefault();

			/** Constructor. */
			UE_API FRenderWork();

			/** Returns true if there are no active work passes. */
			bool IsEmpty() const;

			/** Array of user-defined external input texture overrides, where indices map to ResourceId. */
			TArray<FExternalTexture, SceneRenderingAllocator> ExternalInputs;

			/** Array of pre-processing passes applied on specific texture resources at the start of rendering. */
			TSortedMap<ResourceId, TArray<const FCompositeCorePassProxy*>> PreprocessingPasses;

			/** Post-processing passes at the specified locations. */
			TSortedMap<ISceneViewExtension::EPostProcessingPass, TArray<const FCompositeCorePassProxy*>> FramePasses;

			/** Proxy allocator. */
			TUniquePtr<FSceneRenderingBulkObjectAllocator> FrameAllocator;

			/** Optional main render mode override */
			TOptional<ESceneCaptureSource> MainRenderMode;

			/** View modes for which the compositing is allowed. */
			TSet<EViewModeIndex> AllowedViewModes;

			/** List of scene captures to render-update this frame. */
			TArray<TWeakObjectPtr<USceneCaptureComponent2D>, SceneRenderingAllocator> SceneCapturesUpdateQueue;
		};
	}
}

/** Render-thread pass proxy. */
class FCompositeCorePassProxy
{
public:
	/** Default constructor. */
	FCompositeCorePassProxy() = default;

	/** Constructor. */
	UE_API FCompositeCorePassProxy(UE::CompositeCore::FPassInputDeclArray InPassDeclaredInputs);

	/** Virtual destructor */
	virtual ~FCompositeCorePassProxy() = default;

	/** Bare-bones RTTI method to allow users to differentiate / downcast the composite core pass they're interested in. */
	virtual const FName& GetTypeName() const PURE_VIRTUAL(FCompositeCorePassProxy::GetTypeName, static FName Name; return Name;);

	/** Render-thread add pass method to override. */
	virtual UE::CompositeCore::FPassTexture Add(
		FRDGBuilder& GraphBuilder,
		const FSceneView& InView,
		const UE::CompositeCore::FPassInputArray& Inputs,
		const UE::CompositeCore::FPassContext& PassContext
	) const = 0;

	/** Number of inputs used by the pass. */
	int32 GetNumDeclaredInputs() const
	{
		return PassDeclaredInputs.Num();
	}

	/** Get pass input at specified index. */
	const UE::CompositeCore::FPassInputDecl& GetDeclaredInput(int32 InputIndex) const
	{
		return PassDeclaredInputs[InputIndex];
	}

	/** Get pass input at specified index. */
	UE::CompositeCore::FPassInputDecl& GetDeclaredInput(int32 InputIndex)
	{
		return PassDeclaredInputs[InputIndex];
	}

	/** Get declared output pass override. */
	const TOptional<UE::CompositeCore::ResourceId>& GetDeclaredOutputOverride() const
	{
		return PassDeclaredOutputOverride;
	}

	/** Declare output pass override. */
	void DeclareOutputOverride(UE::CompositeCore::ResourceId InExternalId)
	{
		PassDeclaredOutputOverride = InExternalId;
	}

	/** Reset output pass override. */
	void ResetOutputOverride()
	{
		PassDeclaredOutputOverride.Reset();
	}

	/** Convenience function to create an output render target with the specified resolution. */
	static UE_API FScreenPassRenderTarget CreateOutputRenderTarget(FRDGBuilder& GraphBuilder,
		const FSceneView& InView, const FIntRect& OutputViewRect, FRDGTextureDesc OutputDesc, const TCHAR* InName);

protected:

	/** Ensure that the received inputs match the declared inputs. */
	UE_API bool ValidateInputs(const UE::CompositeCore::FPassInputArray& Inputs) const;

	/** List of pass input types. */
	UE::CompositeCore::FPassInputDeclArray PassDeclaredInputs;

	/** Optional declared output pass override. */
	TOptional<UE::CompositeCore::ResourceId> PassDeclaredOutputOverride;
};

/** Use this macro to implement the RTTI method for composite render passes. Should only be used on final classes (e.g. if C derives from B which derives from A, we won't be able
  *  to query a C* for whether it's a "B", in other words, A and B should be abstract and C, final) */
#define IMPLEMENT_COMPOSITE_PASS(TypeName) const FName& GetTypeName() const override { return GetTypeNameStatic(); } \
	static const FName& GetTypeNameStatic() { static FName Name(TEXT(#TypeName)); return Name; }

#undef UE_API
