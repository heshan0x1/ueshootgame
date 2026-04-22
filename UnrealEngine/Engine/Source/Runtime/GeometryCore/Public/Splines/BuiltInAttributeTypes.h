// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "ParameterizedTypes.h"
#include "SplineTypeId.h"

namespace UE
{
namespace Geometry
{
namespace Spline
{


// Specialize TSplineValueTypeTraits for all built-in types
//-------------------------------------------------------

// Basic types
template<> struct TSplineValueTypeTraits<float> 
{ 
	static inline const FString Name = TEXT("Float");
};

template<> struct TSplineValueTypeTraits<double> 
{ 
	static inline const FString Name = TEXT("Double");
};

template<> struct TSplineValueTypeTraits<int32> 
{ 
	static inline const FString Name = TEXT("Int32");
};

template<> struct TSplineValueTypeTraits<bool> 
{ 
	static inline const FString Name = TEXT("Bool");
};

// Math types
template<> struct TSplineValueTypeTraits<FVector3d> 
{ 
	static inline const FString Name = TEXT("Vector");
};

template<> struct TSplineValueTypeTraits<FVector2D> 
{ 
	static inline const FString Name = TEXT("Vector2D");
};

template<> struct TSplineValueTypeTraits<FVector3f> 
{ 
	static inline const FString Name = TEXT("Vector3f");
};
	
template<> struct TSplineValueTypeTraits<FVector4> 
{ 
	static inline const FString Name = TEXT("Vector4");
};

template<> struct TSplineValueTypeTraits<FQuat> 
{ 
	static inline const FString Name = TEXT("Quat");
};

template<> struct TSplineValueTypeTraits<FRotator> 
{ 
	static inline const FString Name = TEXT("Rotator");
};

template<> struct TSplineValueTypeTraits<FTransform> 
{ 
	static inline const FString Name = TEXT("Transform");
};

// Color types
template<> struct TSplineValueTypeTraits<FLinearColor> 
{ 
	static inline const FString Name = TEXT("LinearColor");
};

template<> struct TSplineValueTypeTraits<FColor> 
{ 
	static inline const FString Name = TEXT("Color");
};

// String types
template<> struct TSplineValueTypeTraits<FName> 
{ 
	static inline const FString Name = TEXT("Name");
};

template<> struct TSplineValueTypeTraits<FString> 
{ 
	static inline const FString Name = TEXT("String");
};

template<> struct TSplineValueTypeTraits<FText> 
{ 
	static inline const FString Name = TEXT("Text");
};

} // namespace Spline
} // namespace Geometry
} // namesp