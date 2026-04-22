// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"
#include "AssetRegistry/AssetData.h"

#include "MetaHumanCharacterViewport.generated.h"

//struct FAssetData;

UENUM()
enum class EMetaHumanCharacterEnvironment : uint8
{
	Studio,
	Split,
	Fireside,
	Moonlight,
	Tungsten,
	Portrait,
	RedLantern,
	TextureBooth,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterEnvironment, EMetaHumanCharacterEnvironment::Count);

UENUM()
enum class EMetaHumanCharacterLOD : uint8
{
	LOD0,
	LOD1,
	LOD2,
	LOD3,
	LOD4,
	LOD5,
	LOD6,
	LOD7,
	Auto,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterLOD, EMetaHumanCharacterLOD::Count);

UENUM()
enum class EMetaHumanCharacterCameraFrame : uint8
{
	Auto,
	Face,
	Body,
	Far,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterCameraFrame, EMetaHumanCharacterCameraFrame::Count);

UENUM()
enum class EMetaHumanCharacterRenderingQuality : uint8
{
	Medium,
	High,
	Epic,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterRenderingQuality, EMetaHumanCharacterRenderingQuality::Count);


USTRUCT(BlueprintType)
struct FMetaHumanCharacterViewportSettings
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterEnvironment CharacterEnvironment = EMetaHumanCharacterEnvironment::Studio;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	FLinearColor BackgroundColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport", meta = (UIMin = "-270", UIMax = "270", ClampMin = "-270", ClampMax = "270"))
	float LightRotation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bTonemapperEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bUseCustomEnvironment = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Viewport", meta=(AllowedClasses="/Script/Engine.Level"))
    FSoftObjectPath CustomEnvironment;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterLOD LevelOfDetail = EMetaHumanCharacterLOD::LOD0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterCameraFrame CameraFrame = EMetaHumanCharacterCameraFrame::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterRenderingQuality RenderingQuality = EMetaHumanCharacterRenderingQuality::Epic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bAlwaysUseHairCards = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowViewportOverlays = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceBones = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyBones = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceNormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyNormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceTangents = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyTangents = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceBinormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyBinormals = false;
};