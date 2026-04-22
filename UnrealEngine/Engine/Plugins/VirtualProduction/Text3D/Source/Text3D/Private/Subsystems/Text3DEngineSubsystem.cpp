// Copyright Epic Games, Inc. All Rights Reserved.

#include "Text3DEngineSubsystem.h"

#include "Containers/Ticker.h"
#include "UDynamicMesh.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GeometryBuilders/Text3DGlyphContourNode.h"
#include "GeometryBuilders/Text3DGlyphLoader.h"
#include "GeometryBuilders/Text3DGlyphMeshBuilder.h"
#include "Logs/Text3DLogs.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Settings/Text3DProjectSettings.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_FREETYPE
#include "Fonts/FontCacheFreeType.h"
#endif

UText3DEngineSubsystem* UText3DEngineSubsystem::Get()
{
	if (GEngine)
	{
		return GEngine->GetEngineSubsystem<UText3DEngineSubsystem>();
	}

	return nullptr;
}

void UText3DEngineSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	ScheduleCacheCleanup();
}

void UText3DEngineSubsystem::Deinitialize()
{
	Super::Deinitialize();
	ClearCache();
}

bool UText3DEngineSubsystem::Exec_Dev(UWorld* InWorld, const TCHAR* InCmd, FOutputDevice& InAr)
{
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	if (FParse::Command(&InCmd, TEXT("text3d")))
	{
		if (FParse::Command(&InCmd, TEXT("cache")))
		{
			if (FParse::Command(&InCmd, TEXT("show")))
			{
				PrintCache();
				return true;
			}

			if (FParse::Command(&InCmd, TEXT("cleanup")))
			{
				CleanupCache(0);
				return true;
			}
		}
	}

	return false;
}

void UText3DEngineSubsystem::ScheduleCacheCleanup()
{
	CancelCacheCleanup();

	if (const UText3DProjectSettings* TextSettings = UText3DProjectSettings::Get())
	{
		if (TextSettings->GetScheduleFontFaceGlyphCleanup())
		{
			CacheCleanupDelegate = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateUObject(this, &UText3DEngineSubsystem::CleanupCache),
				FMath::Max(0, TextSettings->GetFontFaceGlyphCleanupPeriod())
			);
		}
	}
}

void UText3DEngineSubsystem::CancelCacheCleanup()
{
	if (CacheCleanupDelegate.IsValid())
	{
		FTSTicker::RemoveTicker(CacheCleanupDelegate);
		CacheCleanupDelegate.Reset();
	}
}

bool UText3DEngineSubsystem::CleanupCache(float)
{
	uint32 GlyphMeshCount = 0;
	uint32 GlyphMeshRemoved = 0;
	uint32 FontFaceRemoved = 0;

	for (TMap<uint32, FText3DFontFaceCache>::TIterator It(CachedFontFaces); It; ++It)
	{
		FText3DFontFaceCache& FontFace = It.Value();
		GlyphMeshRemoved += FontFace.CleanupCache();
		GlyphMeshCount += FontFace.GetCacheGlyphCount();
		if (!FontFace.IsValid() || FontFace.GetCacheGlyphCount() == 0)
		{
			It.RemoveCurrent();
			FontFaceRemoved++;
		}
	}

	if (FontFaceRemoved > 0 || GlyphMeshRemoved > 0)
	{
		UE_LOG(LogText3D, Log, TEXT("Text3D Engine Subsystem cache cleanup completed : %i unused glyph(s) removed, %i remaining glyph(s), %i unused font face(s) removed, %i remaining font face(s)"), GlyphMeshRemoved, GlyphMeshCount, FontFaceRemoved, CachedFontFaces.Num());
	}

	return true;
}

void UText3DEngineSubsystem::PrintCache() const
{
	UE_LOG(LogText3D, Log, TEXT("Text3D Engine Subsystem is currently caching %i fonts: "), CachedFontFaces.Num());
	for (const TPair<uint32, FText3DFontFaceCache>& CachedFont : CachedFontFaces)
	{
		CachedFont.Value.PrintCache();
	}
}

void UText3DEngineSubsystem::ClearCache()
{
	CachedFontFaces.Empty();
}

FText3DFontFaceCache* UText3DEngineSubsystem::FindCachedFontFace(uint32 InFontFaceHash)
{
	return CachedFontFaces.Find(InFontFaceHash);
}

#if WITH_FREETYPE
FText3DFontFaceCache* UText3DEngineSubsystem::FindOrAddCachedFontFace(const TSharedPtr<FFreeTypeFace>& InFontFace)
{
	if (!InFontFace.IsValid() || !InFontFace->IsFaceValid())
	{
		return nullptr;
	}

	const uint32 FontFaceHash = FText3DFontFaceCache::GetFontFaceHash(InFontFace);

	if (FontFaceHash == 0)
	{
		return nullptr;
	}

	FText3DFontFaceCache& FontFaceCache = CachedFontFaces.FindOrAdd(FontFaceHash, FText3DFontFaceCache(InFontFace));

	/** Refresh font face with latest valid version */
	FontFaceCache.UpdateFontFace(InFontFace);

	return &FontFaceCache;
}

uint32 FText3DFontFaceCache::GetFontFaceHash(const TSharedPtr<FFreeTypeFace>& InFontFace)
{
	if (const FT_Face Face = InFontFace->GetFace())
	{
		if (Face->family_name != nullptr && Face->style_name != nullptr)
		{
			const FString FaceFamily(Face->family_name);
			const FString FaceStyle(Face->style_name);
			const int32 FaceIndex = Face->face_index;
			return HashCombine(HashCombine(GetTypeHash(FaceFamily), GetTypeHash(FaceStyle)), FaceIndex);
		}
	}

	return 0;
}

FText3DFontFaceCache::FText3DFontFaceCache(const TSharedPtr<FFreeTypeFace>& InFontFace)
	: FontFace(InFontFace)
	, FontFaceHash(GetFontFaceHash(InFontFace))
{
}

void FText3DFontFaceCache::UpdateFontFace(const TSharedPtr<FFreeTypeFace>& InFontFace)
{
	FontFace = InFontFace;
}
#endif

uint32 FText3DFontFaceCache::GetGlyphMeshHash(uint32 InGlyphIndex, const FGlyphMeshParameters& InParameters)
{
	return HashCombine(GetTypeHash(InParameters), GetTypeHash(InGlyphIndex));
}

FText3DCachedMesh* FText3DFontFaceCache::FindOrBuildGlyphMesh(uint32 InGlyphIndex, const FGlyphMeshParameters& InParameters)
{
	const uint32 HashParameters = GetGlyphMeshHash(InGlyphIndex, InParameters);
	if (FText3DCachedMesh* CachedMesh = GlyphMeshes.Find(HashParameters))
	{
		return CachedMesh;
	}

	const TText3DGlyphContourNodeShared Root = LoadGlyphContours(InGlyphIndex);
	if (!Root || Root->Children.IsEmpty())
	{
		return nullptr;
	}

	UText3DEngineSubsystem* TextSubsystem = UText3DEngineSubsystem::Get();
	if (!TextSubsystem)
	{
		return nullptr;
	}

	FText3DCachedMesh& CachedMesh = GlyphMeshes.FindOrAdd(HashParameters);

#if WITH_FREETYPE
	if (const FT_Face Face = FontFace->GetFace())
	{
		CachedMesh.FontFaceGlyphSize = FVector2D(Face->size->metrics.x_ppem, Face->size->metrics.y_ppem);
	}
#endif

	const FString MeshName = FString::Printf(TEXT("Text3D_Char_%u"), HashParameters);
	const FName StaticMeshName = MakeUniqueObjectName(TextSubsystem, UStaticMesh::StaticClass(), FName(*MeshName));
	CachedMesh.StaticMesh = NewObject<UStaticMesh>(TextSubsystem, StaticMeshName, RF_Transient | RF_Public);

	const FName DynamicMeshName = MakeUniqueObjectName(TextSubsystem, UDynamicMesh::StaticClass(), FName(*MeshName));
	CachedMesh.DynamicMesh = NewObject<UDynamicMesh>(TextSubsystem, DynamicMeshName, RF_Transient | RF_Public);

	FText3DGlyphMeshBuilder MeshCreator;
	MeshCreator.CreateMeshes(
		Root,
		InParameters.Extrude,
		InParameters.Bevel,
		InParameters.BevelType,
		InParameters.BevelSegments,
		InParameters.bOutline,
		InParameters.OutlineExpand,
		InParameters.OutlineType
	);
	MeshCreator.SetFrontAndBevelTextureCoordinates(InParameters.Bevel);
	MeshCreator.MirrorGroups(InParameters.Extrude);
	MeshCreator.MovePivot(InParameters.PivotOffset);
	MeshCreator.BuildMesh(CachedMesh, GetDefault<UText3DProjectSettings>()->GetDefaultMaterial());

	return &CachedMesh;
}

const FText3DCachedMesh* FText3DFontFaceCache::FindGlyphMesh(uint32 InGlyphHash) const
{
	if (const FText3DCachedMesh* CachedMesh = GlyphMeshes.Find(InGlyphHash))
	{
		return CachedMesh;
	}

	return nullptr;
}

FText3DCachedMesh* FText3DFontFaceCache::FindGlyphMesh(uint32 InGlyphHash)
{
	if (FText3DCachedMesh* CachedMesh = GlyphMeshes.Find(InGlyphHash))
	{
		return CachedMesh;
	}

	return nullptr;
}

uint32 FText3DFontFaceCache::CleanupCache()
{
	uint32 GlyphMeshRemoved = 0;
	for (TMap<uint32, FText3DCachedMesh>::TIterator It(GlyphMeshes); It; ++It)
	{
		if (It->Value.RefCount == 0)
		{
			It.RemoveCurrent();
			GlyphMeshRemoved++;
		}
	}
	return GlyphMeshRemoved;
}

TText3DGlyphContourNodeShared FText3DFontFaceCache::LoadGlyphContours(uint32 InGlyphIndex) const
{
#if WITH_FREETYPE
	if (!FontFace)
	{
		return nullptr;
	}

	const FT_Face Face = FontFace->GetFace();
	if (!Face)
	{
		UE_LOG(LogText3D, Error, TEXT("Failed to load font face glyph contours '%u %i' due to invalid face"), FontFaceHash, InGlyphIndex);
		return nullptr;
	}

	const FT_Error Result = FT_Load_Glyph(Face, InGlyphIndex, FT_LOAD_DEFAULT);
	if (Result != 0)
	{
		UE_LOG(LogText3D, Error, TEXT("Failed to load font face glyph contours '%u %i' with error code : %i"), FontFaceHash, InGlyphIndex, Result);
		return nullptr;
	}

	const FText3DGlyphLoader GlyphLoader(Face->glyph);
	TText3DGlyphContourNodeShared Root = GlyphLoader.GetContourList();

	return Root;
#else
	UE_LOG(LogText3D, Warning, TEXT("FreeType is not available, cannot proceed without it"))
	return nullptr;
#endif
}

void FText3DFontFaceCache::PrintCache() const
{
#if WITH_FREETYPE
	if (IsValid())
	{
		const FT_Face Face = FontFace->GetFace();
		UE_LOG(LogText3D, Log, TEXT("Cached Font Face Family=%s Style=%s Index=%i Hash=%u Meshes=%i Usage=%i"), *FString(Face->family_name), *FString(Face->style_name), Face->face_index, FontFaceHash, GlyphMeshes.Num(), GetCacheRefCount());
	}
#endif
}

bool FText3DFontFaceCache::IsValid() const
{
#if WITH_FREETYPE
	return FontFace.IsValid() && FontFace->GetFace() != nullptr;
#else
	return false;
#endif
}

uint32 FText3DFontFaceCache::GetCacheRefCount() const
{
	uint32 MeshCount = 0;
	for (const TPair<uint32, FText3DCachedMesh>& GlyphMeshPair : GlyphMeshes)
	{
		MeshCount += GlyphMeshPair.Value.RefCount;
	}
	return MeshCount;
}

uint32 FText3DFontFaceCache::GetCacheGlyphCount() const
{
	return GlyphMeshes.Num();
}
