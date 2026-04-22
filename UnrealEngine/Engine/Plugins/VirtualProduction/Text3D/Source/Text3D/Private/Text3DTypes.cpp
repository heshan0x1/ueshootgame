// Copyright Epic Games, Inc. All Rights Reserved.

#include "Text3DTypes.h"

#include "Subsystems/Text3DEngineSubsystem.h"

namespace UE::Text3D::Geometry
{
	FCachedFontFaceGlyphHandle::FCachedFontFaceGlyphHandle(FText3DFontFaceCache* InFontFaceCache, uint32 InGlyphIndex, const FGlyphMeshParameters& InParameters)
	{
		if (InFontFaceCache)
		{
			if (FText3DCachedMesh* CachedMesh = InFontFaceCache->FindOrBuildGlyphMesh(InGlyphIndex, InParameters))
			{
				FontFaceHash = GetTypeHash(*InFontFaceCache);
				GlyphIndex = InGlyphIndex;
				GlyphMeshHash = FText3DFontFaceCache::GetGlyphMeshHash(InGlyphIndex, InParameters);
				CachedMesh->RefCount++;
			}
		}
	}

	FCachedFontFaceGlyphHandle::~FCachedFontFaceGlyphHandle()
	{
		Unset();
	}

	FCachedFontFaceGlyphHandle& FCachedFontFaceGlyphHandle::operator=(const FCachedFontFaceGlyphHandle& InOther)
	{
		Unset();
		FontFaceHash = InOther.FontFaceHash;
		GlyphIndex = InOther.GlyphIndex;
		GlyphMeshHash = InOther.GlyphMeshHash;
		if (FText3DCachedMesh* CachedMesh = Resolve())
		{
			CachedMesh->RefCount++;
		}
		return *this;
	}

	void FCachedFontFaceGlyphHandle::Unset()
	{
		if (FText3DCachedMesh* CachedMesh = Resolve())
		{
			CachedMesh->RefCount--;
		}

		FontFaceHash = 0;
		GlyphMeshHash = 0;
		GlyphIndex = 0;
	}

	const FText3DCachedMesh* FCachedFontFaceGlyphHandle::Resolve() const
	{
		if (IsValid())
		{
			if (UText3DEngineSubsystem* Text3DSubsystem = UText3DEngineSubsystem::Get())
			{
				if (const FText3DFontFaceCache* CachedFontFace = Text3DSubsystem->FindCachedFontFace(FontFaceHash))
				{
					if (const FText3DCachedMesh* CachedMesh = CachedFontFace->FindGlyphMesh(GlyphMeshHash))
					{
						return CachedMesh;
					}
				}
			}
		}

		return nullptr;
	}

	FText3DCachedMesh* FCachedFontFaceGlyphHandle::Resolve()
	{
		if (IsValid())
		{
			if (UText3DEngineSubsystem* Text3DSubsystem = UText3DEngineSubsystem::Get())
			{
				if (FText3DFontFaceCache* CachedFontFace = Text3DSubsystem->FindCachedFontFace(FontFaceHash))
				{
					if (FText3DCachedMesh* CachedMesh = CachedFontFace->FindGlyphMesh(GlyphMeshHash))
					{
						return CachedMesh;
					}
				}
			}
		}

		return nullptr;
	}
}
