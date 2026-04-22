// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

class UTexture2D;

/** Slate style set that defines all the styles for the Cinematic Assembly Tools */
class FCineAssemblyToolsStyle : public FSlateStyleSet
{
public:
	static FName StyleName;

	/** Access the singleton instance for this style set */
	static FCineAssemblyToolsStyle& Get();

	/** Returns the name of the thumbnail brush associated with the input schema */
	FName GetThumbnailBrushNameForSchema(const FString& SchemaName) const;

	/** 
	 * Sets the texture resource used by the thumbnail brush associated with the input schema.
	 * If the input schema does not have a thumbnail brush associated with it yet, a new one will be created first.
	 * If the texture resource is null, the brush will fallback to using a predefined icon.
	 */
	void SetThumbnailBrushTextureForSchema(const FString& SchemaName, UTexture2D* BrushTexture);

private:

	FCineAssemblyToolsStyle();
	~FCineAssemblyToolsStyle();
};
