// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IntVectorTypes.h"
#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"

#define UE_API METAHUMANPIPELINECORE_API

class UTextureRenderTarget2D;

namespace UE::MetaHuman
{
struct IFramePathResolver;
}

namespace UE::MetaHuman::Pipeline
{

class FUEImageLoadNode : public FNode
{
public:

	UE_API FUEImageLoadNode(const FString& InName);
	UE_API virtual ~FUEImageLoadNode() override;

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	TUniquePtr<IFramePathResolver> FramePathResolver;
	bool bFailOnMissingFile = false;

	enum ErrorCode
	{
		FailedToLoadFile = 0,
		FailedToFindFile,
		NoFramePathResolver,
		BadFilePath,
	};
};

class FUEImageSaveNode : public FNode
{
public:

	UE_API FUEImageSaveNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString FilePath;
	int32 FrameNumberOffset = 0;

	enum ErrorCode
	{
		FailedToSaveFile = 0,
		FailedToCompressData,
		MissingFrameFormatSpecifier
	};
};

class FUEImageResizeNode : public FNode
{
public:

	UE_API FUEImageResizeNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 MaxSize = 1;
};

class FUEImageCropNode : public FNode
{
public:

	UE_API FUEImageCropNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 X = -1;
	int32 Y = -1;
	int32 Width = -1;
	int32 Height = -1;

	enum ErrorCode
	{
		BadValues = 0
	};
};

class FUEImageRotateNode : public FNode
{
public:

	UE_API FUEImageRotateNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	UE_API void SetAngle(float InAngle);
	UE_API float GetAngle();

	enum ErrorCode
	{
		UnsupportedAngle = 0
	};

private:

	float Angle = 0;
	FCriticalSection AngleMutex;
};

class FUEImageCompositeNode : public FNode
{
public:

	UE_API FUEImageCompositeNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
};

class FUEImageToUEGrayImageNode : public FNode
{
public:

	UE_API FUEImageToUEGrayImageNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
};

class FUEGrayImageToUEImageNode : public FNode
{
public:

	UE_API FUEGrayImageToUEImageNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
};

class FUEImageToHSImageNode : public FNode
{
public:

	UE_API FUEImageToHSImageNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
};

class FBurnContoursNode : public FNode
{
public:

	UE_API FBurnContoursNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 Size = 1;
	int32 LineWidth = 0; // set this to a value > 0 to connect the contour points by lines
};

class FDepthLoadNode : public FNode
{
public:

	UE_API FDepthLoadNode(const FString& InName);
	UE_API virtual ~FDepthLoadNode() override;

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	TUniquePtr<IFramePathResolver> FramePathResolver;
	bool bFailOnMissingFile = false;

	enum ErrorCode
	{
		FailedToLoadFile = 0,
		FailedToFindFile,
		NoFramePathResolver
	};
};

class FDepthSaveNode : public FNode
{
public:

	UE_API FDepthSaveNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString FilePath;
	int32 FrameNumberOffset = 0;
	bool bShouldCompressFiles = true;

	enum ErrorCode
	{
		FailedToSaveFile = 0,
		FailedToCompressData,
		MissingFrameFormatSpecifier
	};
};

class FDepthQuantizeNode : public FNode
{
public:

	UE_API FDepthQuantizeNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	float Factor = 80; // Eightieth of a cm (0.125mm). This equals the oodle compression quantization. 
};

class FDepthResizeNode : public FNode
{
public:

	UE_API FDepthResizeNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 Factor = 1;
};

class FDepthToUEImageNode : public FNode
{
public:

	UE_API FDepthToUEImageNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	float Min = 0.0f;
	float Max = 1.0f;

	enum ErrorCode
	{
		BadRange = 0
	};
};

class FFColorToUEImageNode : public FNode
{
public:
	UE_API FFColorToUEImageNode(const FString& InName);
	
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	
	TArray<FColor> Samples;
	int32 Width = -1;
	int32 Height = -1;

	enum ErrorCode
	{
		NoInputImage = 0
	};
}; 

class FCopyImagesNode : public FNode
{
public:
	UE_API FCopyImagesNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 FrameNumberOffset = 0;

	FString InputFilePath;
	FString OutputDirectoryPath;

	enum ErrorCode
	{
		FailedToFindFile = 0,
		FailedToCopyFile,
		MissingFrameFormatSpecifier
	};
};

}

#undef UE_API
