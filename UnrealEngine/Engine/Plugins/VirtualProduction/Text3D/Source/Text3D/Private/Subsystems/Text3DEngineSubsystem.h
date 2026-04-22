// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "GeometryBuilders/Text3DGlyphContourNode.h"
#include "Subsystems/EngineSubsystem.h"
#include "Text3DTypes.h"

#include "Text3DEngineSubsystem.generated.h"

struct FSharedStruct;
class FFreeTypeFace;

USTRUCT()
struct FText3DFontFaceCache
{
	GENERATED_BODY()

#if WITH_FREETYPE
	static uint32 GetFontFaceHash(const TSharedPtr<FFreeTypeFace>& InFontFace);
#endif
	static uint32 GetGlyphMeshHash(uint32 InGlyphIndex, const FGlyphMeshParameters& InParameters);

	FText3DFontFaceCache() = default;

#if WITH_FREETYPE
	explicit FText3DFontFaceCache(const TSharedPtr<FFreeTypeFace>& InFontFace);
	void UpdateFontFace(const TSharedPtr<FFreeTypeFace>& InFontFace);
#endif

	FText3DCachedMesh* FindOrBuildGlyphMesh(uint32 InGlyphIndex, const FGlyphMeshParameters& InParameters);
	const FText3DCachedMesh* FindGlyphMesh(uint32 InGlyphHash) const;
	FText3DCachedMesh* FindGlyphMesh(uint32 InGlyphHash);
	uint32 CleanupCache();
	
	void PrintCache() const;
	bool IsValid() const;
	uint32 GetCacheRefCount() const;
	uint32 GetCacheGlyphCount() const;

	bool operator==(const FText3DFontFaceCache& InOther) const
	{
		return FontFaceHash == InOther.FontFaceHash;
	}

	bool operator!=(const FText3DFontFaceCache& InOther) const
	{
		return !(*this == InOther);
	}

	friend uint32 GetTypeHash(const FText3DFontFaceCache& InValue)
	{
		return InValue.FontFaceHash;
	}

private:
	TText3DGlyphContourNodeShared LoadGlyphContours(uint32 InGlyphIndex) const;

#if WITH_FREETYPE
	/** Font face for a Font+Typeface */
	TSharedPtr<FFreeTypeFace> FontFace;
#endif

	/** Glyph indexes to cached meshes */
	UPROPERTY()
	TMap<uint32, FText3DCachedMesh> GlyphMeshes;

	/** Hash to uniquely identify this struct */
	UPROPERTY()
	uint32 FontFaceHash = 0;
};

UCLASS()
class UText3DEngineSubsystem : public UEngineSubsystem, public FSelfRegisteringExec
{
	GENERATED_BODY()

public:
	static UText3DEngineSubsystem* Get();

	void ScheduleCacheCleanup();
	void CancelCacheCleanup();

#if WITH_FREETYPE
	FText3DFontFaceCache* FindOrAddCachedFontFace(const TSharedPtr<FFreeTypeFace>& InFontFace);
#endif

	FText3DFontFaceCache* FindCachedFontFace(uint32 InFontFaceHash);

private:
	// ~Begin UEngineSubsystem
	virtual void Initialize(FSubsystemCollectionBase& InCollection) override;
	virtual void Deinitialize() override;
	// ~End UEngineSubsystem

	// ~Begin FSelfRegisteringExec
	virtual bool Exec_Dev(UWorld* InWorld, const TCHAR* InCmd, FOutputDevice& InAr) override;
	// ~End FSelfRegisteringExec

	bool CleanupCache(float);
	void PrintCache() const;
	void ClearCache();

	UPROPERTY()
	TMap<uint32, FText3DFontFaceCache> CachedFontFaces;

	FTSTicker::FDelegateHandle CacheCleanupDelegate;
};
