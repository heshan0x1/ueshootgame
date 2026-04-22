// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Texture.h"
#include "Serialization/Archive.h"

class FArchive;
namespace Nanite
{

class FDisplacementMap
{
private:
	static constexpr size_t MAX_NUM_MIP_LEVELS = 13;

public:
	virtual ~FDisplacementMap() {}

	ETextureSourceFormat	SourceFormat;

	int32		BytesPerPixel;
	int32		SizeX;
	int32		SizeY;
	uint32		NumLevels;

	float		Magnitude;
	float		Center;

	TextureAddress	AddressX;
	TextureAddress	AddressY;

public:

	NANITEUTILITIES_API				FDisplacementMap();
	NANITEUTILITIES_API				FDisplacementMap( struct FImage&& TextureSourceImage, float InMagnitude, float InCenter, TextureAddress InAddressX, TextureAddress InAddressY );
	
	// Bilinear filtered
	NANITEUTILITIES_API float		Sample( FVector2f UV ) const;

	// Bounds over UV-rectangle (approximate, but conservative)
	NANITEUTILITIES_API FVector2f	Sample( FVector2f MinUV, FVector2f MaxUV ) const;

	// Bounds over UV-rectangle (hierarchical traversal, increased refinements lead to tighter bounds, 
	// but significantly more expensive for large numbers of refinements)
	NANITEUTILITIES_API FVector2f	SampleHierarchical( FVector2f MinUV, FVector2f MaxUV, uint32 MaxRefinements ) const;

	// hierarchical sample warping for perfect importance sampling according to probability density represented by image (not required to be normalized).
	// [ Clarberg, et al., "Wavelet importance sampling: efficiently evaluating products of complex functions" ]
	NANITEUTILITIES_API FVector2f   WarpSample(const FVector2f& UV) const;

	/**
	 * Filtered with elliptic weighted averaging
	 * 
	 * \param Axis0 major axis
	 * \param Axis1 minor axis
	 */
	NANITEUTILITIES_API float		SampleEWA( FVector2f UV, FVector2f Axis0, FVector2f Axis1 ) const;

	float		Sample( int32 x, int32 y ) const;
	FVector2f	Sample( int32 x, int32 y, uint32 Level ) const;

	float		Load( int32 x, int32 y ) const;
	FVector2f	Load( int32 x, int32 y, uint32 Level ) const;

	float       LoadFiltered( int32 x, int32 y, uint32 Level ) const;

	NANITEUTILITIES_API virtual void Serialize(FArchive& Ar);

private:

	// per-level EWA
	float       EWA( uint32 Level, FVector2f UV, FVector2f Axis0, FVector2f Axis1 ) const;

	TArray64< uint8 >	SourceData;
	TArray< FVector2f >	MipData[ MAX_NUM_MIP_LEVELS ];
	TArray< float >     MipDataFiltered[ MAX_NUM_MIP_LEVELS ];

	void		Address( int32& x, int32& y ) const;
};

FORCEINLINE float FDisplacementMap::Sample( int32 x, int32 y ) const
{
	Address( x, y );

	float Displacement = Load( x, y );
	Displacement -= Center;
	Displacement *= Magnitude;

	return Displacement;
}

FORCEINLINE FVector2f FDisplacementMap::Sample( int32 x, int32 y, uint32 Level ) const
{
	Address( x, y );

	x >>= Level;
	y >>= Level;

	FVector2f Displacement = Load( x, y, Level );
	Displacement -= FVector2f( Center );
	Displacement *= Magnitude;

	return Displacement;
}

FORCEINLINE float FDisplacementMap::Load( int32 x, int32 y ) const
{
	const uint8* PixelPtr = &SourceData[ int64( x + (int64)y * SizeX ) * BytesPerPixel ];

	if( SourceFormat == TSF_BGRA8 )
	{
		return float( PixelPtr[2] ) / 255.0f;
	}
	else if( SourceFormat == TSF_RGBA16 )
	{
		checkSlow( BytesPerPixel == sizeof(uint16) * 4 );
		return float( *(uint16*)PixelPtr ) / 65535.0f;
	}
	else if( SourceFormat == TSF_RGBA16F || SourceFormat == TSF_R16F )
	{
		FFloat16 HalfValue = *(FFloat16*)PixelPtr;
		return HalfValue;
	}
	else if( SourceFormat == TSF_G8 )
	{
		return float( PixelPtr[0] ) / 255.0f;
	}
	else if( SourceFormat == TSF_G16 )
	{
		return float( *(uint16*)PixelPtr ) / 65535.0f;
	}
	else if( SourceFormat == TSF_RGBA32F || SourceFormat == TSF_R32F )
	{
		return *(float*)PixelPtr;
	}
	else
	{
		checkf( 0, TEXT("Displacement map format not supported") );
		return 0.0f;
	}
}

FORCEINLINE FVector2f FDisplacementMap::Load( int32 x, int32 y, uint32 Level ) const
{
	checkSlow( Level > 0 );

	uint32 MipSizeX = ( ( SizeX - 1 ) >> Level ) + 1;
	uint32 MipSizeY = ( ( SizeY - 1 ) >> Level ) + 1;

	return MipData[ Level - 1 ][ x + y * MipSizeX ];
}


FORCEINLINE float FDisplacementMap::LoadFiltered( int32 x, int32 y, uint32 Level ) const
{
	checkSlow( Level >= 0 );

	if( Level == 0 )
	{
		return Load( x, y );
	}
	
	uint32 MipSizeX = ( ( SizeX - 1 ) >> Level ) + 1;
	uint32 MipSizeY = ( ( SizeY - 1 ) >> Level ) + 1;

	return MipDataFiltered[ Level - 1 ][ x + y * MipSizeX ];
}

FORCEINLINE void FDisplacementMap::Address( int32& x, int32& y ) const
{
	if( AddressX == TA_Clamp )
		x = FMath::Clamp( x, 0, SizeX - 1 );
	else
	{
		x  = x % SizeX;
		x += x < 0 ? SizeX : 0;
	}

	if( AddressY == TA_Clamp )
		y = FMath::Clamp( y, 0, SizeY - 1 );
	else
	{
		y  = y % SizeY;
		y += y < 0 ? SizeY : 0;
	}
}

inline FArchive& operator<<(FArchive& Ar, FDisplacementMap& DisplacementMap)
{
    DisplacementMap.Serialize(Ar);
    return Ar;
}

} // namespace Nanite