// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/InterpCurve.h"
#include "Splines/SplineInterfaces.h"

#include "Spline.generated.h"

struct FSplinePoint;
struct FSplineCurves;
class FLegacySpline;
class FNewSpline;

struct UE_EXPERIMENTAL(5.7, "New spline APIs are experimental.") FSpline;

/**
 * A general purpose, reflected spline.
 * The implementation can be configured at runtime.
 */
USTRUCT()
struct FSpline
{
	GENERATED_BODY()

	ENGINE_API FSpline();
	ENGINE_API FSpline(const FSpline& Other);
	ENGINE_API FSpline& operator=(const FSpline& Other);
	ENGINE_API FSpline& operator=(const FSplineCurves& Other);
	ENGINE_API ~FSpline();
	
	/* Control Point Index Interface */
	
	/** Adds point by parameter. The new point is inserted after all points with parameter values less than OR EQUAL to the new point's parameter.
	 * Nick would like to change this behavior in the future. See UE-250236. */
	ENGINE_API void AddPoint(const FSplinePoint& InPoint);
	/** Adds point by index. Provided parameter is ignored. The new point is inserted before all points with parameter values greater than the new point's parameter. */
	ENGINE_API void InsertPoint(const FSplinePoint& InPoint, int32 Index);
	ENGINE_API FSplinePoint GetPoint(const int32 Index) const;
	ENGINE_API void RemovePoint(const int32 Index);
	
	ENGINE_API void SetLocation(int32 Index, const FVector& InLocation);
	ENGINE_API FVector GetLocation(const int32 Index) const;
	
	ENGINE_API void SetInTangent(const int32 Index, const FVector& InTangent);
	ENGINE_API FVector GetInTangent(const int32 Index) const;
	
	ENGINE_API void SetOutTangent(const int32 Index, const FVector& OutTangent);
	ENGINE_API FVector GetOutTangent(const int32 Index) const;

	ENGINE_API void SetRotation(int32 Index, const FQuat& InRotation);
	ENGINE_API FQuat GetRotation(const int32 Index) const;
	
	ENGINE_API void SetScale(int32 Index, const FVector& InScale);
	ENGINE_API FVector GetScale(const int32 Index) const;
	
	ENGINE_API void SetSplinePointType(int32 Index, EInterpCurveMode Type);
	ENGINE_API EInterpCurveMode GetSplinePointType(int32 Index) const;

	ENGINE_API float GetParameterAtIndex(int32 Index) const;
	ENGINE_API float GetParameterAtDistance(float Distance) const;
	ENGINE_API float GetDistanceAtParameter(float Parameter) const;

	ENGINE_API FQuat GetOrientation(int32 Index) const;
	ENGINE_API void SetOrientation(int32 Index, const FQuat& InOrientation);
	
	/* Parameter Interface */

	ENGINE_API FVector Evaluate(float Param) const;
	ENGINE_API FVector EvaluateDerivative(float Param) const;
	ENGINE_API FQuat EvaluateRotation(float Param) const;
	ENGINE_API FVector EvaluateScale(float Param) const;

	ENGINE_API FQuat GetOrientation(float Param) const;
	
	/* Attribute Interface */

	// Non-Templated functions
	bool SupportsAttributes() const { return IsNew(); }
	ENGINE_API bool HasAttributeChannel(FName AttributeName) const;
	ENGINE_API bool RemoveAttributeChannel(FName AttributeName) const;
	ENGINE_API TArray<FName> GetFloatPropertyChannels() const;
	ENGINE_API TArray<FName> GetVectorPropertyChannels() const;

	template <typename AttrType> ENGINE_API float GetAttributeParameter(int32 Index, const FName& Name) const;
	template <typename AttrType> ENGINE_API int32 SetAttributeParameter(int32 Index, float Parameter, const FName& Name);
	template <typename AttrType> ENGINE_API int32 NumAttributeValues(FName AttributeName) const;
	template <typename AttrType> ENGINE_API AttrType GetAttributeValue(int32 Index, const FName& Name) const;
	template <typename AttrType> ENGINE_API void SetAttributeValue(int32 Index, const AttrType& Value, const FName& Name);
	template <typename AttrType> ENGINE_API bool CreateAttributeChannel(FName AttributeName) const;
	template <typename AttrType> ENGINE_API int32 AddAttributeValue(float Param, const AttrType& Value, FName AttributeName) const;
	template <typename AttrType> ENGINE_API void RemoveAttributeValue(int32 Index, FName AttributeName);
	template <typename AttrType> ENGINE_API AttrType EvaluateAttribute(float Param, FName AttributeName) const;

	ENGINE_API float FindNearest(const FVector& InLocation, float& OutSquaredDist) const;
	ENGINE_API float FindNearestOnSegment(const FVector& InLocation, int32 SegmentIndex, float& OutSquaredDist) const;
	
	/* Misc Interface */
	
	ENGINE_API bool operator==(const FSpline& Other) const;
	bool operator!=(const FSpline& Other) const
	{
		return !(*this == Other);
	}
	
	friend FArchive& operator<<(FArchive& Ar, FSpline& Spline)
	{
		Spline.Serialize(Ar);
		return Ar;
	}
	ENGINE_API bool Serialize(FArchive& Ar);
	ENGINE_API void SerializeLoad(FArchive& Ar);
	ENGINE_API void SerializeSave(FArchive& Ar) const;
	ENGINE_API bool ExportTextItem(FString& ValueStr, FSpline const& DefaultValue, class UObject* Parent, int32 PortFlags, class UObject* ExportRootScope) const;
	ENGINE_API bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText);

	uint32 GetVersion() const { return Version; }
	
	ENGINE_API const FInterpCurveVector& GetSplinePointsPosition() const;
	ENGINE_API const FInterpCurveQuat& GetSplinePointsRotation() const;
	ENGINE_API const FInterpCurveVector& GetSplinePointsScale() const;

	/** Returns the length of the specified spline segment up to the parametric value given */
	ENGINE_API float GetSegmentLength(const int32 Index, const float Param, const FVector& Scale3D = FVector(1.0f)) const;

	/** Returns total length along this spline */
	ENGINE_API float GetSplineLength() const;

	/** Returns the total number of segments on this spline. */
	ENGINE_API int32 GetNumSegments() const;
	
	/** Returns the total number of control points on this spline. */
	ENGINE_API int32 GetNumControlPoints() const;
	
	/** Reset the spline to an empty spline. */
	ENGINE_API void Reset();
	
	/** Reset the rotation attribute channel to default values. */
	ENGINE_API void ResetRotation();
	
	/** Reset the scale attribute channel to default values. */
	ENGINE_API void ResetScale();

	ENGINE_API void SetClosedLoop(bool bClosed);
	ENGINE_API bool IsClosedLoop() const;
	
	struct FUpdateSplineParams
	{
		bool bClosedLoop = false;
		bool bStationaryEndpoints = false;
		int32 ReparamStepsPerSegment = 10;
		bool bLoopPositionOverride = false;
		float LoopPosition = 0.0f;
		FVector Scale3D = FVector(1.0f);
	};
	
	/** Update the spline's internal data according to the passed-in params. */
	ENGINE_API void UpdateSpline(const FUpdateSplineParams& InParams);

	/** Update the spline's internal data according to the most recently used update params (or default params if never updated). */
	ENGINE_API void UpdateSpline();

	ENGINE_API TSharedPtr<UE::Geometry::Spline::TSplineInterface<FVector>> GetSplineInterface() const;

private:
	
	static inline const FInterpCurveVector PositionCurve;
	static inline const FInterpCurveQuat RotationCurve;
	static inline const FInterpCurveVector ScaleCurve;
	
	// Used for upgrade logic in spline component.
	// Not ideal, but allows us to automatically populate the proxy
	// at serialize time when we might otherwise not be able to.
	friend class USplineComponent;
	friend struct FPCGSplineStruct;
	
#if WITH_EDITOR
	uint8 PreviousImplementation;
#endif
	uint8 CurrentImplementation;
	
	uint32 Version;

	FUpdateSplineParams CachedUpdateSplineParams;
	
	// probably better implemented as a TSharedPtr<ISplineInterface> or something
	struct {													// Invalid when CurrentImplementation is 0 or 3.
		TSharedPtr<FLegacySpline> LegacyData;					// Valid when CurrentImplementation is 1.
		TSharedPtr<FNewSpline> NewData;							// Valid when CurrentImplementation is 2.
	};
	
	bool IsEnabled() const { return CurrentImplementation != 0; }
	bool IsLegacy() const { return CurrentImplementation == 1; }
	bool IsNew() const { return CurrentImplementation == 2; }

#if WITH_EDITOR
	bool WasEnabled() const { return PreviousImplementation != 0; }
	bool WasLegacy() const { return PreviousImplementation == 1; }
	bool WasNew() const { return PreviousImplementation == 2; }

	/** Called when the implementation is changed at editor time due to a console command. */
	void OnSplineImplementationChanged();
	FDelegateHandle OnSplineImplementationChangedHandle;
#endif

public:

	// True if a given FSpline will actually be implemented when instantiated.
	static bool IsEnabledStatic();
};

template<>
struct TStructOpsTypeTraits<FSpline> : public TStructOpsTypeTraitsBase2<FSpline>
{
	enum
	{
		WithSerializer				= true, // Enables the use of a custom Serialize method.
		WithIdenticalViaEquality	= true, // Enables the use of a custom equality operator.
		WithExportTextItem			= true, // Enables the use of a custom ExportTextItem method.
		WithImportTextItem			= true, // Enables the use of a custom ImportTextItem method.
	};
};
