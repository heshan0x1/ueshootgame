// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BSpline.h"
#include "Misc/ScopeExit.h"

namespace UE
{
namespace Geometry
{
namespace Spline
{

template<typename VALUETYPE> class UE_EXPERIMENTAL(5.7, "New spline APIs are experimental.") TPolyBezierSpline;
	
/* Augments B-Spline interface */
template<typename VALUETYPE>
class TPolyBezierSpline :
	public TBSpline<VALUETYPE, 3>,
	private TSelfRegisteringSpline<TPolyBezierSpline<VALUETYPE>, VALUETYPE>
{
public:
	using Base = TBSpline<VALUETYPE, 3>;
	typedef typename Base::ValueType ValueType;
	using FWindow = typename Base::FWindow;

	// Generate compile-time type ID for PolyBezier
	DECLARE_SPLINE_TYPE_ID(
		TEXT("PolyBezier"),
		*TSplineValueTypeTraits<VALUETYPE>::Name
	);
	
    TPolyBezierSpline() = default;
    virtual ~TPolyBezierSpline() override = default;
	
	TPolyBezierSpline(
		const ValueType& P0, 
		const ValueType& P1, 
		const ValueType& P2, 
		const ValueType& P3,
		EParameterizationPolicy Parameterization = EParameterizationPolicy::Uniform)
	: Base()
	{
		// Add the initial segment
		AddBezierSegmentInternal( 0, P0, P1, P2, P3, Parameterization);
		TPolyBezierSpline<VALUETYPE>::Reparameterize(Parameterization);
	}

    /**
	 * Factory method to create an empty spline with a default segment 
	 */
	static TPolyBezierSpline CreateDefault()
	{
		// Create a minimal valid default segment for serialization
		ValueType P0(0, 0, 0);
		ValueType P1(0.33f, 0, 0);
		ValueType P2(0.66f, 0, 0);
		ValueType P3(1, 0, 0);
		
		return TPolyBezierSpline(P0, P1, P2, P3, EParameterizationPolicy::Uniform);
	}

	// Static factory methods for common shapes    
    // Create a straight line between two points
    static TPolyBezierSpline<ValueType> CreateLine(
        const ValueType& Start, 
        const ValueType& End)
    {
        // Calculate control points for a straight line
        // For a straight line, the control points should be evenly spaced
        ValueType P0 = Start;
        ValueType P3 = End;
        ValueType Direction = (End - Start);
        ValueType P1 = Start + Direction * 0.33f;
        ValueType P2 = Start + Direction * 0.66f;
        
        TPolyBezierSpline<ValueType> Result(P0, P1, P2, P3);
        Result.Reparameterize(EParameterizationPolicy::Uniform);
        return Result;
    }
    
    // Create a circular arc with given center, radius, and angle range
    static TPolyBezierSpline<ValueType> CreateCircleArc(
        const ValueType& Center,
        float Radius,
        float StartAngle,
        float EndAngle,
        int32 NumSegments = 4)
    {
           // Ensure angles are in the right order
           if (EndAngle < StartAngle)
           {
               float Temp = StartAngle;
               StartAngle = EndAngle;
               EndAngle = Temp;
           }
           
           // Ensure reasonable segment count
           NumSegments = FMath::Max(1, NumSegments);
           
           // Calculate angle per segment
           float AngleSpan = EndAngle - StartAngle;
           float AnglePerSegment = AngleSpan / static_cast<float>(NumSegments);
           
           // For large angles per segment, increase segment count for better approximation
           if (AnglePerSegment > UE_PI/4)
           {
               NumSegments = FMath::CeilToInt(AngleSpan / (UE_PI/4));
               AnglePerSegment = AngleSpan / static_cast<float>(NumSegments);
           }
           
           // Pre-compute all points along the arc for exact positioning
           TArray<ValueType> Points;
           Points.SetNum(NumSegments + 1);
           
           for (int32 i = 0; i <= NumSegments; i++)
           {
               float Angle = StartAngle + (static_cast<float>(i) * AnglePerSegment);
               Points[i] = Center + ValueType(
                   Radius * FMath::Cos(Angle),
                   Radius * FMath::Sin(Angle),
                   0);
           }
           
           // Calculate first segment with precise tangents
           ValueType P0 = Points[0];
           ValueType P3 = Points[1];
           
           // Get normalized direction vectors from center to points
           ValueType Dir0 = Math::GetSafeNormal(P0 - Center);
           ValueType Dir3 = Math::GetSafeNormal(P3 - Center);
           
           // Calculate perpendicular vectors (normalized)
           ValueType Perp0 = ValueType(-Dir0.Y, Dir0.X, 0);
           ValueType Perp3 = ValueType(-Dir3.Y, Dir3.X, 0);
           
           // Calculate precise tangent scale
           float TangentScale = Radius * (4.0f / 3.0f) * 
                                    FMath::Tan(AnglePerSegment / 4.0f);
           
           // Create control points with exact perpendicular tangents
           ValueType P1 = P0 + Perp0 * TangentScale;
           ValueType P2 = P3 - Perp3 * TangentScale;
           
           // Create spline with first segment
           TPolyBezierSpline<ValueType> Result(P0, P1, P2, P3);
           
           // Add remaining segments with carefully calculated control points
           for (int32 i = 1; i < NumSegments; i++)
           {
               P0 = Points[i];
               P3 = Points[i+1];
               
               // Calculate exact perpendicular vectors for this segment
               Dir0 = Math::GetSafeNormal(P0 - Center);
               Dir3 = Math::GetSafeNormal(P3 - Center);
               Perp0 = ValueType(-Dir0.Y, Dir0.X, 0);
               Perp3 = ValueType(-Dir3.Y, Dir3.X, 0);
               
               // Create control points with exact perpendicular tangents
               P1 = P0 + Perp0 * TangentScale;
               P2 = P3 - Perp3 * TangentScale;
               
               Result.AppendBezierSegment(P1, P2, P3);
           }
           
           return Result;
    }
    
    // Create a complete circle
    static TPolyBezierSpline<ValueType> CreateCircle(
        const ValueType& Center,
        float Radius,
        int32 NumSegments = 4)
    {
		// Ensure reasonable segment count
		NumSegments = FMath::Max(4, NumSegments);
    
		// Create a full circle arc (0 to 2π)
		TPolyBezierSpline<ValueType> Result = 
			CreateCircleArc(Center, Radius, 0.0f, 2.0f * UE_PI, NumSegments);
    
		// Set as closed loop
		Result.SetClosedLoop(true);
		// Apply consistent parameterization
		Result.Reparameterize(EParameterizationPolicy::Centripetal);
		return Result;
    }
    
    // Create an ellipse with X and Y radii
    static TPolyBezierSpline<ValueType> CreateEllipse(
        const ValueType& Center,
        float RadiusX,
        float RadiusY,
        int32 NumSegments = 4)
    {
        // Ensure at least 4 segments for good quality
	    if (NumSegments < 4) NumSegments = 4;
	    
	    // Calculate angle per segment
	    const float AnglePerSegment = 2.0f * UE_PI / static_cast<float>(NumSegments);
	    
	    // Start with first point at angle 0
	    float CurrentAngle = 0.0f;
	    ValueType P0 = Center + ValueType(RadiusX * FMath::Cos(CurrentAngle), 
	                                      RadiusY * FMath::Sin(CurrentAngle), 0);
	    
	    // Calculate next point
	    CurrentAngle += AnglePerSegment;
	    ValueType P3 = Center + ValueType(RadiusX * FMath::Cos(CurrentAngle), 
	                                      RadiusY * FMath::Sin(CurrentAngle), 0);
	    
	    // Calculate correct tangent scale for this angle
	    const float TangentScale = (4.0f/3.0f) * FMath::Tan(AnglePerSegment / 4.0f);
	    
	    // Calculate tangent vectors (scaled appropriately for ellipse)
	    ValueType Tangent0 = ValueType(-RadiusX * FMath::Sin(0.0f), 
	                                  RadiusY * FMath::Cos(0.0f), 0) * TangentScale;
	    ValueType Tangent3 = ValueType(-RadiusX * FMath::Sin(CurrentAngle), 
	                                  RadiusY * FMath::Cos(CurrentAngle), 0) * TangentScale;
	    
	    ValueType P1 = P0 + Tangent0;
	    ValueType P2 = P3 - Tangent3;
	    
	    // Create the spline with the first segment
	    TPolyBezierSpline<ValueType> Result(P0, P1, P2, P3);
	    
	    // Add remaining segments
	    for (int32 i = 1; i < NumSegments; ++i)
	    {
	        P0 = P3; // Start from previous endpoint
	        Tangent0 = Tangent3; // Reuse previous tangent
	        
	        // Calculate next endpoint
	        CurrentAngle += AnglePerSegment;
	        P3 = Center + ValueType(RadiusX * FMath::Cos(CurrentAngle), 
	                               RadiusY * FMath::Sin(CurrentAngle), 0);
	        
	        // Calculate tangent at next point
	        Tangent3 = ValueType(-RadiusX * FMath::Sin(CurrentAngle), 
	                            RadiusY * FMath::Cos(CurrentAngle), 0) * TangentScale;
	        
	        // Calculate control points
	        P1 = P0 + Tangent0;
	        P2 = P3 - Tangent3;
	        
	        // Add the segment
	        Result.AppendBezierSegment(P1, P2, P3);
	    }
	    
	    // Set as closed loop
	    Result.SetClosedLoop(true);
	            
        // Apply consistent parameterization
        Result.Reparameterize(EParameterizationPolicy::Centripetal);
        
        return Result;
    }

	// ISplineInterface Implementation
	virtual void Clear() override { Base::Clear(); }
	virtual TUniquePtr<ISplineInterface> Clone() const override
    {
        TUniquePtr<TPolyBezierSpline> Clone = MakeUnique<TPolyBezierSpline>();
        
        // Copy base class members
        Clone->Values = this->Values;
        Clone->PairKnots = this->PairKnots;
        Clone->bIsClosedLoop = this->bIsClosedLoop;
        Clone->bClampEnds = this->bClampEnds;
        
        // Copy infinity modes
        Clone->PreInfinityMode = this->PreInfinityMode;
        Clone->PostInfinityMode = this->PostInfinityMode;
        
        return Clone;
    }
	
    float FindNearestOnSegment(const ValueType& Point, int32 SegmentIndex, float& OutSquaredDistance) const
    {
    	if (!IsValidSegmentIndex(SegmentIndex))
    	{
    		OutSquaredDistance = TNumericLimits<float>::Max();
    		return 0.0f;
    	}

    	// Get Bezier control points for this segment
    	TStaticArray<ValueType, 4> Coeffs = {};
    	const int32 BaseIndex = SegmentIndex * 4;
        
    	// Get the control points
    	const ValueType& P0 = Base::GetValue(BaseIndex);
    	const ValueType& P1 = Base::GetValue(BaseIndex + 1);
    	const ValueType& P2 = Base::GetValue(BaseIndex + 2);
    	const ValueType& P3 = Base::GetValue(BaseIndex + 3);

    	// Setup coefficients in standard Bezier polynomial form relative to test point
    	Coeffs[0] = P0 - Point;                    // constant term
    	Coeffs[1] = (P1 - P0) * 3;                 // linear coefficient
    	Coeffs[2] = (P2 - P1*2 + P0) * 3;          // quadratic coefficient  
    	Coeffs[3] = P3 - P2*3 + P1*3 - P0;         // cubic coefficient

    	const float LocalT = Math::FindNearestPoint_Cubic(MakeArrayView(Coeffs), 0.0f, 1.0f, OutSquaredDistance);
    	return MapLocalSegmentParameterToGlobal(SegmentIndex, LocalT);
    }
	
	virtual float FindNearest(const ValueType& Point, float& OutSquaredDistance) const override
	{
    	float BestDistSq = TNumericLimits<float>::Max();
    	float BestParam = 0.0f;

    	const int32 NumSegments = Base::NumKeys() / 4;
    	for (int32 i = 0; i < NumSegments; ++i)
    	{
    		float SegmentDistSq;
    		const float SplineParam = FindNearestOnSegment(Point, i, SegmentDistSq);
    		
    		if (SegmentDistSq < BestDistSq)
    		{
    			BestDistSq = SegmentDistSq;
    			BestParam = SplineParam;
    		}
    	}

    	OutSquaredDistance = BestDistSq;
    	
    	return BestParam;
	}

	// Evaluator methods
	static int32 GetDegree() { return 3; }

    /** 
     * Evaluate nth derivative at parameter
     * @tparam Order - The derivative order (0 = position, 1 = first derivative, etc.)
     * @param Parameter - Parameter in spline space
     * @return nth derivative vector
     */
    template<int32 Order>
    ValueType EvaluateDerivative(float Parameter) const
    {
		FWindow Window = FindWindow(Parameter);

		// If FindWindow fails, it will return an array of nullptr. InterpolateWindow assumes validity of elements.
		if (Window[0] == nullptr) { return ValueType(); }
		
        // Order 0 is just regular evaluation
	    if constexpr (Order == 0)
	    {
	        return InterpolateWindow(Window, Parameter);
	    }
    
    	// Convert global parameter to segment information
    	int32 SegmentIndex;
    	float LocalT;
    	MapGlobalParameterToLocalSegment(Parameter, SegmentIndex, LocalT);
		
	    // For PolyBezier, each segment is normalized to unit parameter space
        constexpr float SegmentScale = 1.0f;
	    
	    // Compute the derivative with proper scaling
	    return Math::TBezierDerivativeCalculator<ValueType, Order>::Compute(
	        Window, LocalT, SegmentScale);
    }

	/**
	 * MANIPULATION METHODS
	 */

	/**
	 * Replaces all control points in the spline with the provided points.
	 * For a cubic Bezier spline, the number of points must be a multiple of 4 (or 4N-1 for open splines).
	 * 
	 * @param Points - Array of new control points
	 * @param ParameterizationPolicy - How to distribute Knots for the new segments
	 * @return True if the points were successfully set
	 */
	bool SetControlPoints(
		const TArray<ValueType>& Points,
		EParameterizationPolicy ParameterizationPolicy)
	{
	    const int32 NumPoints = Points.Num();
    
	    // Need at least 4 points for a valid cubic bezier segment
	    if (NumPoints < 4)
	    {
	        UE_LOG(LogSpline, Warning, TEXT("SetControlPoints requires at least 4 points to add a valid segment. Got %d points."), NumPoints);
	        return false;
	    }
	    
	    // Clear existing points
	    Clear();
	    const bool bIsClosedLoop = Base::IsClosedLoop();
	    
	    // For cubic Bezier curves, points come in groups of 4:
	    // P0 (start), P1 (control), P2 (control), P3 (end)
	    // Each complete segment requires 4 points
	    
	    // Calculate how many complete segments we have
	    int32 NumSegments = (NumPoints) / 4;
	    
	    // Make sure we have at least one complete segment
	    if (NumSegments < 1)
	    {
	        UE_LOG(LogSpline, Warning, TEXT("Not enough points to create a complete segment. Need at least 4 points."));
	        return false;
	    }
	    
	    // Add each segment
	    for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
	    {
	        const int32 BaseIndex = SegmentIndex * 4;
	        
	        // Ensure we have enough points for this segment
	        if (BaseIndex + 3 >= NumPoints)
	        {
	            break;
	        }
	        
	        // Extract the 4 control points for this segment
	        ValueType P0 = Points[BaseIndex];
	        ValueType P1 = Points[BaseIndex + 1];
	        ValueType P2 = Points[BaseIndex + 2];
	        ValueType P3 = Points[BaseIndex + 3];
	        
	        // Add the segment
	        if (SegmentIndex == 0)
	        {
	            // For the first segment, use AddBezierSegmentInternal
	            this->AddBezierSegmentInternal(0, P0, P1, P2, P3, ParameterizationPolicy);
	        }
	        else
	        {
	            // For subsequent segments, check if P0 matches the last point of the previous segment
	            ValueType LastPoint = Base::GetValue(Base::NumKeys() - 1);
	            
	            if (Math::SizeSquared(P0 - LastPoint) < UE_KINDA_SMALL_NUMBER)
	            {
	                // Points are the same, use AppendBezierSegment which only needs P1, P2, P3
	                this->AppendBezierSegment(P1, P2, P3, ParameterizationPolicy);
	            }
	            else
	            {
	            	
	                // Points don't match, use AddBezierSegmentInternal with appropriately computed parameter
	            	this->AddBezierSegmentInternal(NumSegments, P0, P1, P2, P3, ParameterizationPolicy);
	            }
	        }
	    }
	    
	    // Update closed loop state if needed
	    if (bIsClosedLoop)
	    {
	        // If we're supposed to be a closed loop, make sure the last point connects to the first
	        ValueType FirstPoint = Base::GetValue(0);
	        ValueType LastPoint = Base::GetValue(Base::NumKeys() - 1);
	        
	        // If not already a closed loop, add a segment to close it
	        if (Math::SizeSquared(LastPoint - FirstPoint) > UE_KINDA_SMALL_NUMBER && NumSegments > 0)
	        {
	            // Add a segment that connects back to the start
	            // We need to compute appropriate control points
	            int32 LastBaseIndex = (NumSegments - 1) * 4;
	            
	            // We'll try to maintain tangent continuity if possible
	            ValueType OutTangent = (Points[LastBaseIndex + 2] - Points[LastBaseIndex + 1]);
	            ValueType InTangent = (Points[1] - Points[0]);
	            
	            // Scale tangents to match segment length
	            float SegmentLength = static_cast<float>(Math::Distance(FirstPoint, LastPoint));
	            OutTangent = Math::GetSafeNormal(OutTangent) * (SegmentLength * 0.33f);
	            InTangent = Math::GetSafeNormal(InTangent) * (SegmentLength * 0.33f);
	            
	            // Create control points
	            ValueType P1 = LastPoint + OutTangent;
	            ValueType P2 = FirstPoint - InTangent;
	            
	            // Add the closing segment
	            this->AppendBezierSegment(P1, P2, FirstPoint, ParameterizationPolicy);
	        }
	        
	        Base::SetClosedLoop(true);
	    }
	    else
	    {
	        Base::SetClosedLoop(false);
	    }
	    
	    // Reparameterize for consistent parameter distribution
	    Reparameterize(ParameterizationPolicy);
	    
	    return true;
	}

	/**
	 * Adds multiple Bezier segments to the spline.
	 * 
	 * @param Storage - Storage to add the segments to
	 * @param Points - Array of control points defining the new segments
	 * @param bAppend - If true, append to the end of spline; if false, prepend to the beginning
	 * @param ParameterizationPolicy - How to distribute Knots for the new segments
	 * @return True if the segments were successfully added
	 */
	bool AddBezierSegments(
		const TArray<ValueType>& Points,
		bool bAppend,
		EParameterizationPolicy ParameterizationPolicy)
	{
	    const int32 NumPoints = Points.Num();
	    
		// Need at least 2 points to add a valid segment
		if (NumPoints < 4)
		{
			UE_LOG(LogSpline, Warning, TEXT("AddBezierSegments requires at least 4 points to add a valid segment. Got %d points."), NumPoints);
			return false;
		}
	    
	    // If the spline is currently empty (even though it shouldn't be according to design), this is equivalent to SetControlPoints
	    if (this->Base::NumKeys() == 0)
	    {
	        return SetControlPoints(Points, ParameterizationPolicy);
	    }
	    
	    // Check points format based on append/prepend direction
	    if (bAppend)
	    {
	        // For appending, we expect 3N+1 points: P0 (shared with last existing point), then (P1,P2,P3) per segment
	        if (NumPoints % 3 != 1)
	        {
	            UE_LOG(LogSpline, Warning, TEXT("Invalid point count for appending segments. Expected 3N+1, got %d."), NumPoints);
	            return false;
	        }
	    }
	    else
	    {
	        // For prepending, we expect 3N+1 points: (P0,P1,P2) per segment, then P3 (shared with first existing point)
	        if (NumPoints % 3 != 1)
	        {
	            UE_LOG(LogSpline, Warning, TEXT("Invalid point count for prepending segments. Expected 3N+1, got %d."), NumPoints);
	            return false;
	        }
	    }
	    
	    // Check connection point
	    if (bAppend)
	    {
	        // First point of new segments should match the last point of existing spline
	        const ValueType& FirstNewPoint = Points[0];
	        const ValueType& LastExistingPoint = this->Base::GetValue(this->Base::NumKeys() - 1);
	        
	        // Check if the connection point is reasonably close
	        const double DistanceSquared = Math::SizeSquared(LastExistingPoint, FirstNewPoint);
	        if (DistanceSquared > UE_KINDA_SMALL_NUMBER)
	        {
	            UE_LOG(LogSpline, Warning, TEXT("Connection point mismatch when appending segments. Distance: %f"),
	                FMath::Sqrt(static_cast<float>(DistanceSquared)));
	            // Continue anyway but log warning
	        }
	    }
	    else
	    {
	        // Last point of new segments should match the first point of existing spline
	        const ValueType& LastNewPoint = Points.Last();
	        const ValueType& FirstExistingPoint = this->Base::GetValue(0);
	        
	        // Check if the connection point is reasonably close
	        const double DistanceSquared = Math::SizeSquared(LastNewPoint, FirstExistingPoint);
	        if (DistanceSquared > UE_KINDA_SMALL_NUMBER)
	        {
	            UE_LOG(LogSpline, Warning, TEXT("Connection point mismatch when prepending segments. Distance: %f"),
	                FMath::Sqrt(static_cast<float>(DistanceSquared)));
	            // Continue anyway but log warning
	        }
	    }
	    
	    if (bAppend)
	    {
	        // Append segments to the end of the spline
	        const int32 NumSegmentsToAdd = (NumPoints - 1) / 3;
	        
	        // Add each segment
	        for (int32 SegmentIndex = 0; SegmentIndex < NumSegmentsToAdd; ++SegmentIndex)
	        {
	            const int32 BaseIndex = 1 + SegmentIndex * 3; // Skip the first point (P0) for first segment
	            
	            // For each segment, we need P1, P2, P3 (P0 is the last point of existing spline)
	            const ValueType& P1 = Points[BaseIndex];
	            const ValueType& P2 = Points[BaseIndex + 1];
	            const ValueType& P3 = Points[BaseIndex + 2];
	            
	            // Use the existing AppendBezierSegment method
	            this->AppendBezierSegment(P1, P2, P3, ParameterizationPolicy);
	        }
	    }
	    else
	    {
	        // Prepend segments to the beginning of the spline
	        const int32 NumSegmentsToPrepend = (NumPoints - 1) / 3;
	        
	        // Prepend segments in order starting from the first segment
	        // For prepending, we need to work backwards since each PrependBezierSegment
	        // adds to the beginning
	        for (int32 i = NumSegmentsToPrepend - 1; i >= 0; --i)
	        {
	            const int32 BaseIndex = i * 3;
	            
	            // For each segment, we need P0, P1, P2 (P3 will connect to the existing spline)
	            const ValueType& P0 = Points[BaseIndex];
	            const ValueType& P1 = Points[BaseIndex + 1];
	            const ValueType& P2 = Points[BaseIndex + 2];
	            
	            // Use the existing PrependBezierSegment method
	            this->PrependBezierSegment(P0, P1, P2, ParameterizationPolicy);
	        }
	    }
	    
	    // Reparameterize the spline to ensure consistent parameterization
    	Reparameterize(ParameterizationPolicy);
	    
	    return true;
	}
    /**
     * Appends a Bezier segment using only 3 points (P1, P2, P3)
     * The start point (P0) is automatically inferred from the last point of the previous segment.
     * 
     * @param P1 First control point
     * @param P2 Second control point
     * @param P3 End point
     * @param ParameterizationPolicy How to distribute Knots
     * @return Index of the new segment
     */
    int32 AppendBezierSegment(
        const ValueType& P1, 
        const ValueType& P2, 
        const ValueType& P3, 
        EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
    {
		const TOptional<float> LoopKnotDelta = GetLoopKnotDelta();
		SetClosedLoop(false);
		ON_SCOPE_EXIT
		{
			if (LoopKnotDelta.IsSet())
			{
				SetClosedLoop(true);
				SetParameter(GetNumberOfSegments(), LoopKnotDelta.GetValue());
			}
		};
		
		// Get P0 from the last point of the previous segment
		ValueType P0 = Base::GetValue(Base::NumKeys() - 1);
		int32 NumSegments = Base::NumKeys() / 4;
		// Add segment with append behavior

		return AddBezierSegmentInternal(NumSegments, P0, P1, P2, P3, ParameterizationPolicy);
    }

    /**
     * Prepends a Bezier segment using only 3 points (P0, P1, P2)
     * The end point (P3) is automatically inferred from the first point of the existing spline.
     * 
     * @param P0 First control point
     * @param P1 Second control point
     * @param P2 Third control point
     * @param ParameterizationPolicy How to distribute Knots
     * @return Index of the new segment
     */
    int32 PrependBezierSegment(
		const ValueType& P0, 
		const ValueType& P1, 
		const ValueType& P2, 
		EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
	{
		const TOptional<float> LoopKnotDelta = GetLoopKnotDelta();
		SetClosedLoop(false);
		ON_SCOPE_EXIT
		{
			if (LoopKnotDelta.IsSet())
			{
				SetClosedLoop(true);
				SetParameter(GetNumberOfSegments(), LoopKnotDelta.GetValue());
			}
		};
		
		// Get P3 from the first point of the existing spline
		ValueType P3 = Base::GetValue(0);
		// Add segment with prepend behavior
		return AddBezierSegmentInternal(0, P0, P1, P2, P3,
										ParameterizationPolicy,
										false); // Prepend to first segment
	}

	/**
	 * Inserts a point at the closest location on a segment to the given position.
	 * 
	 * @param SegmentIndex Index of the segment to split
	 * @param Position Position for the new point
	 * @param ParameterizationPolicy How to distribute Knots
	 * @return The index of the newly created point
	 */
	int32 InsertPointAtPosition(
		int32 SegmentIndex,
		const ValueType& Position,
		EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
	{
		// Find the closest point on the segment to the given position
		float SquaredDistance;
		float LocalT = FindLocalParameterNearestToPosition(SegmentIndex, Position, SquaredDistance);
            
		// Insert at that local parameter
		return this->InsertPointAtSegmentParam(SegmentIndex, LocalT, Position, ParameterizationPolicy);
	}

	/**
	 * Inserts a point at a specific segment with local parameter.
	 * This splits the segment at the specified location and places the new point
	 * at the provided position, which may change the spline shape.
	 * 
	 * @param SegmentIndex Index of the segment to split
	 * @param LocalT Local parameter [0,1] within the segment
	 * @param Position Position for the new point
	 * @param ParameterizationPolicy How to distribute Knots
	 * @return The index of the newly created point
	 */
	int32 InsertPointAtSegmentParam(
	    int32 SegmentIndex,
	    float LocalT,
	    const ValueType& Position,
	    EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
	{
    	int32 NumSegments = Base::NumKeys() / 4;
	    // Validate inputs
	    if (NumSegments == 0)
	    {
	        // Special case for empty spline
	        int32 NewPointIndex;
	        if (InitializeFirstSegment(0.0f, Position, NewPointIndex))
	            return NewPointIndex;
	    }
	    
	    SegmentIndex = FMath::Clamp(SegmentIndex, 0, NumSegments - 1);
	    
	    // Get the segment's parameter range
	    FInterval1f SegmentRange = GetSegmentParameterRange(SegmentIndex);
	    
	    // Convert local parameter to global parameter
	    float Parameter = SegmentRange.Min + LocalT * (SegmentRange.Max - SegmentRange.Min);
	    
	    return InsertPoint(Parameter, Position, ParameterizationPolicy);
	}
		
	/**
	 * Inserts a point at the specified parameter along the spline.
	 * This splits the segment at the parameter value and places the new point
	 * at the specified position, which may change the spline shape.
	 * 
	 * @param Storage - Storage to add the point to
	 * @param Parameter Parameter value where to split the spline
	 * @param Position Position for the new point (doesn't need to be on the original curve)
	 * @param ParameterizationPolicy How to distribute Knots
	 * @return The index of the newly created point
	 */
    int32 InsertPoint(
        float Parameter, 
        const ValueType& Position,
        EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
    {
		// todo: stop returning bool, instead interpret INDEX_NONE as failure
	    if (int32 NewPointIndex;
	    	InitializeFirstSegment(Parameter, Position, NewPointIndex))
		{
			return NewPointIndex;
		}

		// Make sure we have a valid parameter
		Parameter = Base::GetNearestAvailableKnotValue(Parameter);
		
        FInterval1f ParameterRange = GetParameterSpace();
		int32 SegmentIndex;
		float LocalT;
		MapGlobalParameterToLocalSegment(Parameter, SegmentIndex, LocalT);

		const int32 BaseIndex = SegmentIndex * 4;
		ValueType P0 = Base::GetValue(BaseIndex);
		
		// Handle out-of-range Knots
		if (Parameter <= ParameterRange.Min)
		{
			// Prepend
			return PrependPoint(Position, ParameterizationPolicy);
		}
    
		if (Parameter >= ParameterRange.Max)
		{
			return AppendPoint(Position, ParameterizationPolicy);
		}
 
        ValueType P1 = Base::GetValue(BaseIndex + 1);
        ValueType P2 = Base::GetValue(BaseIndex + 2);
        ValueType P3 = Base::GetValue(BaseIndex + 3);
 
        // Use De Casteljau algorithm to split the curve
        const float t = LocalT;
        const float mt = 1.0f - t;
        
        // Calculate split control points
        ValueType Q0 = P0;
        ValueType Q1 = P0 * mt + P1 * t;
        ValueType Q2 = (P0 * mt + P1 * t) * mt + (P1 * mt + P2 * t) * t;
        ValueType Q3 = Position;  // Use provided position for the split point
        
        ValueType R0 = Position;  // Use provided position for the split point
        ValueType R1 = (P1 * mt + P2 * t) * mt + (P2 * mt + P3 * t) * t;
        ValueType R2 = P2 * mt + P3 * t;
        ValueType R3 = P3;
        
        FInterval1f SegmentRange = GetSegmentParameterRange(SegmentIndex);
		float TargetSplitParam = SegmentRange.Interpolate(LocalT);

		if (TargetSplitParam == SegmentRange.Min)
		{
			TargetSplitParam = Param::NextDistinct(TargetSplitParam);
		}

		if (TargetSplitParam == SegmentRange.Max)
		{
			TargetSplitParam = Param::PrevDistinct(TargetSplitParam);
		}
		
		for (int32 i = 0; i < 4; ++i)
		{
			Base::RemoveValue(BaseIndex);
		}
		
        // Add the two new segments using the policy
        AddBezierSegmentInternal(SegmentIndex, Q0, Q1, Q2, Q3, ParameterizationPolicy);
		Base::InsertKnot( FKnot(TargetSplitParam, this->bClampEnds ? this->GetDegree() : 1));
		int32 IndexToReturn = AddBezierSegmentInternal(SegmentIndex + 1, R0, R1, R2, R3, ParameterizationPolicy);
		
		return IndexToReturn;
    }
	
    /**
     * Inserts a Bezier segment at the specified parameter
     * 
     * @param ValuesChannel - Channel to add the segment to
     * @param Parameter Parameter value where to insert the segment (Spline Space)
     * @param P1 First control point
     * @param P2 Second control point
     * @param P3 End point
     * @param ParameterizationPolicy How to distribute Knots
     * @return Index of the new segment
     */
    int32 InsertBezierSegment(
        float Parameter, 
        const ValueType& P1, 
        const ValueType& P2, 
        const ValueType& P3, 
        EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
    {
		FInterval1f ParameterRange = GetParameterSpace();
        // Handle edge cases
        if (Parameter >= ParameterRange.Max)
        {
            // Add new segment at the end
            return AppendBezierSegment(P1, P2, P3, ParameterizationPolicy);
        }
        
        if (Parameter <= ParameterRange.Min)
        {
			// we treat P1, P2, P3 as the starting three control points, and connect to existing spline at the start
            return PrependBezierSegment(P1, P2, P3, ParameterizationPolicy);
        }
        
        // Normal case - insert at parameter
        const int32 NumSegments = Base::NumKeys() / 4;
        
        // Find which segment contains this parameter
		int32 SegmentIndex;
		float LocalParam;
		MapGlobalParameterToLocalSegment(Parameter, SegmentIndex, LocalParam);
        
        // Evaluate position at the parameter
        ValueType PositionAtParam = Base::Evaluate(Parameter);
        
        // Use the InsertPoint method to split the curve at the parameter
        InsertPointAtSegmentParam(SegmentIndex, LocalParam, PositionAtParam, ParameterizationPolicy);
        
        // Add the new segment from the inserted point
        int32 Result = AddBezierSegmentInternal(SegmentIndex+1, PositionAtParam, P1, P2, P3, ParameterizationPolicy);
    	
        return Result;
    }

	/**
	* Removes a Bezier segment from the spline
	*
	* @param Storage - Storage to remove the segment from
	* @param SegmentIndex - Segment index to remove
	* @return true if successfully removed
	*/
    bool RemoveSegment(const int32 SegmentIndex)
    {
    	const int32 NumSegments = Base::NumKeys() / 4;
	    if (SegmentIndex < 0 || SegmentIndex >= NumSegments)
	    {
	        return false;
	    }

    	// Calculate storage indices
    	const bool bIsLastSegment = (SegmentIndex == NumSegments - 1);
    
    	UE_LOG(LogSpline, Verbose, TEXT("Removing segment at index %d"), SegmentIndex);
    
        if (bIsLastSegment)
	    {
	    	return RemovePoint(GetNumberOfSegments());
	    }
	    else
	    {
	    	// in all other cases, removing the segment would be the same as removing the point at that index
	        return RemovePoint(SegmentIndex);
	    }
    }

	/**
	 * Removes a point from the spline.
	 * @param PointIndex - Index of the point to remove
	 */
	bool RemovePoint(int32 PointIndex)
	{
		const int32 NumSegments = GetNumberOfSegments();
        if (PointIndex < 0 || PointIndex > NumSegments)
        {
            return false;
        }
		bool bSuccess = true;
		const int32 SegmentIndex = FMath::Clamp(PointIndex, 0, NumSegments - 1);
		const int32 BaseIndex = SegmentIndex * 4;
		const int32 Degree = GetDegree();
		if (PointIndex == 0)
		{
			// Removing the first point - remove the first segment
			for (int32 i = 3; i >= 0; --i)
			{
				bSuccess &= Base::RemoveValue(BaseIndex + i);
			}
			// Remove first knot
			Base::RemoveKnot(0);

			if (Base::IsClampedEnds())
			{
				Base::PairKnots[0].Multiplicity = Degree + 1;
			}
		}
		// else if last point
		else if (PointIndex == GetNumberOfSegments())
		{
			// Removing the last point - remove the last segment
			for (int32 i = 3; i >= 0; --i)
			{
				bSuccess &= Base::RemoveValue(BaseIndex + i);
			}
	        
			// Remove end knot
			Base::RemoveKnot(SegmentIndex + 1);
			if (Base::IsClampedEnds())
			{
				Base::PairKnots.Last().Multiplicity = Degree + 1;
			}
		}
		else
		{
			// For an internal point, we need to remove exactly 4 control points:
			// - P2 and P3 from the previous segment (where P3 is the point)
			// - P0 and P1 from the next segment (where P0 is the same point)
        
			// Start index for removal: P2 of previous segment
			int32 StartRemoveIndex = (PointIndex - 1) * 4 + 2;
        
			// Remove 4 consecutive control points
			for (int32 i = 0; i < 4; ++i)
			{
				Base::RemoveValue(StartRemoveIndex);
			}
        
			Base::RemoveKnot(PointIndex);
		}
		Base::Dump();
		return bSuccess;
	}

    /**
	 * Updates a single control point within a Bezier segment
	 * @param Storage - Storage to update the point in
	 * @param SegmentIndex - Which cubic Bezier segment (each has 4 points)
	 * @param PointIndex - Which point in the segment (0=P0, 1=P1, 2=P2, 3=P3)
	 * @param NewValue - New position for the control point
	 * @return true if successfully updated
	 */
    bool UpdateSegmentPoint(
    	const int32 SegmentIndex,
    	const int32 PointIndex,
    	const ValueType& NewValue)
    {
        const int32 NumSegments = Base::NumKeys() / 4;
        if (SegmentIndex < 0 || SegmentIndex >= NumSegments ||
            PointIndex < 0 || PointIndex > 3)
        {
            return false;
        }

        // Convert segment+point index to global index in spline values
        const int32 GlobalIndex = SegmentIndex * 4 + PointIndex;
        return Base::SetValue(GlobalIndex, NewValue);
    }

    /**
     * Updates all control points of a Bezier segment at once
     * @param SegmentIndex - Which cubic Bezier segment to update
     * @param P0 - Start point of segment
     * @param P1 - First control point
     * @param P2 - Second control point
     * @param P3 - End point of segment
     * @return true if successfully updated
     */
    bool UpdateSegment(
    	int32 SegmentIndex,
    	const ValueType& P0,
    	const ValueType& P1, 
        const ValueType& P2,
        const ValueType& P3)
    {
        const int32 NumSegments = Base::NumKeys() / 4;
        if (SegmentIndex < 0 || SegmentIndex >= NumSegments)
        {
            return false;
        }

        const int32 BaseIndex = SegmentIndex * 4;
        bool bSuccess = true;
        bSuccess &= Base::SetValue(BaseIndex, P0);
        bSuccess &= Base::SetValue(BaseIndex + 1, P1);
        bSuccess &= Base::SetValue(BaseIndex + 2, P2);
        bSuccess &= Base::SetValue(BaseIndex + 3, P3);
    
        return bSuccess;
    }
	
	/** Returns the number of Bezier segments in the spline */
	virtual int32 GetNumberOfSegments() const override
	{
		const int32 NumPoints = this->Base::NumKeys();
		return NumPoints/4;
	}

	/**
	 * Maps a segment index to its parameter range
	 * @param SegmentIndex - Index of the segment (0-based)
	 * @return True if the segment index is valid and mapping succeeded
	 */
	virtual FInterval1f GetSegmentParameterRange(int32 SegmentIndex) const override
	{
		FInterval1f SegmentRange;
		if (SegmentIndex >= 0 && SegmentIndex < Base::PairKnots.Num() - 1)
		{
			SegmentRange.Min = Base::PairKnots[SegmentIndex].Value;
			SegmentRange.Max = Base::PairKnots[SegmentIndex + 1].Value;
		}
        
		return SegmentRange;
	}

	// Parameterization methods
	/**
	 * Reparameterizes the spline based on the provided points and mode
	 * @param Mode - New parameterization mode
	 * @param Points - Array of control points to use for reparameterization
	 * @return 
	 */
	virtual void Reparameterize(EParameterizationPolicy Mode = EParameterizationPolicy::Centripetal) override
	{
		const TArray<ValueType>& Points = this->Values;
		const int32 NumValues = Points.Num();

		GenerateDistanceKnotsForBezier(Mode);
    	
		UE_LOG(LogSpline, Verbose, TEXT("Set knot vector - Points: %d, Knots: %d, Mode: %d"),
					   NumValues,  this->PairKnots.Num(), static_cast<int32>(Mode));
		
		Base::PrintKnotVector();
	}
	
	virtual FInterval1f GetParameterSpace() const override
	{
		return Base::PairKnots.Num() > 0
			? FInterval1f(Base::PairKnots[0].Value, Base::PairKnots.Last().Value)
			: FInterval1f::Empty();
	}

	virtual float GetParameter(int32 Index) const override
	{
		if (Base::PairKnots.IsEmpty())
			return 0.f;
		
        int32 NumSegments = Base::PairKnots.Num() - 1;
        int32 SegmentIndex = Index / 4;
        int32 LocalPointIndex = Index % 4;
    
        if (SegmentIndex >= NumSegments)
            return Base::PairKnots.Last().Value;
    
        FInterval1f Range(Base::PairKnots[SegmentIndex].Value, Base::PairKnots[SegmentIndex + 1].Value);
        const float T = static_cast<float>(LocalPointIndex) / 3.0f;
        return Range.Interpolate(T);
	}

	virtual int32 SetParameter(int32 Index, float NewParameter) override
	{
		if (Index < 0 || Index >= Base::NumKeys() || Base::NumKeys() < 4)
		{
			return INDEX_NONE;
		}
		
		constexpr int32 PointsPerSegment = 4;
		const int32 SegmentIndex = Index / PointsPerSegment;
		const int32 LocalPointIndex = Index % PointsPerSegment;
		int32 NumSegments = GetNumberOfSegments();

		// Get the original parameter value
		int32 OldParameterIndex = LocalPointIndex == 0 ? SegmentIndex : SegmentIndex + 1;
		float OldParameter = Base::PairKnots[OldParameterIndex].Value;
    
		// Early exit if parameter isn't changing
		if (OldParameter == NewParameter)
		{
			return Index;
		}
		
		UE_LOG(LogSpline, Verbose, TEXT("\t"));
		UE_LOG(LogSpline, Verbose, TEXT("Before SetParameter(%d, %f):"), Index, NewParameter);
		Base::Dump();
		
		// Prevent reuse of an existing knot.
		const float DesiredNewParameter = NewParameter;
		const float ActualNewParameter = this->GetNearestAvailableKnotValue(DesiredNewParameter);

		const float DesiredToOldDistance = FMath::Abs(DesiredNewParameter - OldParameter);
		const float DesiredToActualNewDistance = FMath::Abs(DesiredNewParameter - ActualNewParameter);

		if (DesiredToActualNewDistance > DesiredToOldDistance)
		{
			// If our nearest valid knot is further away from the desired value than our current value is, we need to just keep our current value
			return Index;
		}
		
		int32 ReturnIndex = INDEX_NONE;
		
		if (LocalPointIndex == 0) // P0
		{
			int32 LeftSegmentIndex = SegmentIndex - 1;
			int32 RightSegmentIndex = SegmentIndex;
			
			Base::SetKnot(OldParameterIndex, NewParameter);

			int32 Segment = SegmentIndex;
			if (NewParameter < OldParameter)
			{
				while (LeftSegmentIndex >= 0 && GetSegmentParameterRange(LeftSegmentIndex).Min > GetSegmentParameterRange(LeftSegmentIndex).Max)
				{
					FlipSegment(LeftSegmentIndex);
					LeftSegmentIndex--;
					RightSegmentIndex--;
					Segment = RightSegmentIndex;
				}
			}
			else if (NewParameter > OldParameter)
			{
				while (RightSegmentIndex <= NumSegments - 1 && GetSegmentParameterRange(RightSegmentIndex).Min > GetSegmentParameterRange(RightSegmentIndex).Max)
				{
					FlipSegment(RightSegmentIndex);
					LeftSegmentIndex++;
					RightSegmentIndex++;
					Segment = RightSegmentIndex;
				}
			}
			
			ReturnIndex = Segment * PointsPerSegment + LocalPointIndex;
		}
		else if (LocalPointIndex == 3) // P3
		{
			int32 LeftSegmentIndex = SegmentIndex;
			int32 RightSegmentIndex = SegmentIndex + 1;
			
			Base::SetKnot(OldParameterIndex, NewParameter);

			int32 Segment = SegmentIndex;
			if (NewParameter < OldParameter)
			{
				while (LeftSegmentIndex >= 0 && GetSegmentParameterRange(LeftSegmentIndex).Min > GetSegmentParameterRange(LeftSegmentIndex).Max)
				{
					FlipSegment(LeftSegmentIndex);
					LeftSegmentIndex--;
					RightSegmentIndex--;
					Segment = LeftSegmentIndex;
				}
			}
			else if (NewParameter > OldParameter)
			{
				while (RightSegmentIndex <= NumSegments - 1 && GetSegmentParameterRange(RightSegmentIndex).Min > GetSegmentParameterRange(RightSegmentIndex).Max)
				{
					FlipSegment(RightSegmentIndex);
					LeftSegmentIndex++;
					RightSegmentIndex++;
					Segment = LeftSegmentIndex;
				}
			}
			
			ReturnIndex = Segment * PointsPerSegment + LocalPointIndex;
		}

		UE_LOG(LogSpline, Verbose, TEXT("\t"));
		UE_LOG(LogSpline, Verbose, TEXT("After SetParameter(%d, %f):"), Index, NewParameter);
		Base::Dump();
		
		return ReturnIndex;
	}
	
	void FlipSegment(int32 Segment)
	{
		static constexpr int32 ControlPointsPerSegment = 4;
		
		const int32 NumSegments = GetNumberOfSegments();
		if (Segment < 0 || Segment >= NumSegments)
		{
			return;
		}

		UE_LOG(LogSpline, Verbose, TEXT("\t"));
		UE_LOG(LogSpline, Verbose, TEXT("Before FlipSegment(%d):"), Segment);
		Base::Dump();
		
		// Flip the segment
		{
			int32 P0 = Segment * ControlPointsPerSegment + 0;
			int32 P1 = Segment * ControlPointsPerSegment + 1;
			int32 P2 = Segment * ControlPointsPerSegment + 2;
			int32 P3 = Segment * ControlPointsPerSegment + 3;

			Base::Values.Swap(P0, P3);
			Base::Values.Swap(P1, P2);
		}

		// Fix up neighbor segment by keeping P3 and P0 coincident and preserving the out-tangent of the end of the neighbor segment
		if (Segment != 0)
		{
			// Set P3 of Segment - 1 to P0 of Segment
			// Need to preserve P2 relative to P3 on Segment - 1
			int32 P2 = (Segment - 1) * ControlPointsPerSegment + 2;
			int32 P3 = (Segment - 1) * ControlPointsPerSegment + 3;
			int32 P0 = Segment * ControlPointsPerSegment + 0;

			ValueType Delta = Base::Values[P3] - Base::Values[P2];
			Base::Values[P3] = Base::Values[P0];
			Base::Values[P2] = Base::Values[P3] - Delta;
		}

		// Fix up neighbor segment by keeping P0 and P3 coincident and preserving the in-tangent of the beginning of the neighbor segment
		if (Segment != NumSegments - 1)
		{
			// Set P0 of Segment + 1 to P3 of Segment
			// Need to preserve P1 relative to P0 on Segment + 1
			int32 P0 = (Segment + 1) * ControlPointsPerSegment + 0;
			int32 P1 = (Segment + 1) * ControlPointsPerSegment + 1;
			int32 P3 = Segment * ControlPointsPerSegment + 3;

			ValueType Delta = Base::Values[P0] - Base::Values[P1];
			Base::Values[P0] = Base::Values[P3];
			Base::Values[P1] = Base::Values[P0] - Delta;
		}
		
		// Fix up the knot vector
		Base::SwapKnots(Segment, Segment + 1);

		UE_LOG(LogSpline, Verbose, TEXT("\t"));
		UE_LOG(LogSpline, Verbose, TEXT("After FlipSegment(%d):"), Segment);
		Base::Dump();
	}
	
	virtual int32 FindIndexForParameter(float Parameter, float& OutLocalParam) const override
	{
		int32 NumSegments = Base::PairKnots.Num() - 1;

		// todo: either delete this function in favor of MapGlobalParameterToLocalSegment or fix the loop to binary search with caching
		
		for (int32 i = 0; i < NumSegments; ++i)
		{
			float Start = Base::PairKnots[i].Value;
			float End = Base::PairKnots[i + 1].Value;
			// Exact match on last segment end point
			if (i == NumSegments - 1 && Parameter == End)
			{
				OutLocalParam = 1.0f;
				return i * 4;
			}

			if (Parameter >= Start && Parameter < End)
			{
				float SpanLength = End - Start;

				OutLocalParam = (SpanLength > FLT_EPSILON)
					? (Parameter - Start) / SpanLength
					: 0.0f;

				return i * 4;
			}
		}
		OutLocalParam = 0.0f;
		return 0; // Fallback to first segment
	}
	
	/**
	 * Sets the closed loop flag without modifying segments
	 * @param bClosed Whether the spline should be a closed loop
	 */
	void SetClosedLoopFlag(bool bClosed)
	{
		Base::bIsClosedLoop = bClosed;
	}
	
	virtual void SetClosedLoop(bool bShouldClose) override
	{
		// Skip if state isn't changing
		if (bShouldClose == this->IsClosedLoop())
			return;
        
		if (bShouldClose)
		{
			// When closing a loop, add a segment to connect the last point back to the first
			const int32 NumSegments = GetNumberOfSegments();
        
			if (NumSegments >= 1) // Need at least one segment to close
			{
				// Get first and last points
				ValueType FirstPoint = this->GetValue(0);
				ValueType LastPoint = this->GetValue((NumSegments - 1) * 4 + 3);
            
				// If the last point isn't already equal to the first point
				if (!Math::Equals(FirstPoint, LastPoint, UE_SMALL_NUMBER))	
				{
					// Calculate control points for a smooth transition
					ValueType Direction = FirstPoint - LastPoint;
					ValueType P1 = LastPoint + Direction * 0.33f;
					ValueType P2 = FirstPoint - Direction * 0.33f;
					
					// Add the closing segment
					AppendBezierSegment(P1, P2, FirstPoint);
				}
			}
		}
		else
		{
			// For an open spline, remove the closing segment
			const int32 NumSegments = GetNumberOfSegments();
			if (NumSegments > 0)
			{
				// Remove the last segment
				this->RemoveSegment(NumSegments - 1);
			}
		}
    
		// Set the flag
		SetClosedLoopFlag(bShouldClose);
	}

	/**
	 * Maps a local segment parameter [0,1] to global parameter space
	 * @param SegmentIndex - Index of the segment
	 * @param LocalParam - Local parameter within segment [0,1]
	 * @return Parameter value in global space, or 0 if segment is invalid
	 */
	float MapLocalSegmentParameterToGlobal(int32 SegmentIndex, float LocalParam) const
	{
		FInterval1f SegmentRange = GetSegmentParameterRange(SegmentIndex);
		return SegmentRange.Interpolate(LocalParam);
	}

	/**
	 * Maps a global parameter to a segment index and local parameter
	 * @param GlobalParam - Parameter in global space
	 * @param OutSegmentIndex - Output segment index
	 * @param OutLocalParam - Output local parameter [0,1]
	 * @return True if mapping succeeded
	 */
	bool MapGlobalParameterToLocalSegment(float GlobalParam, int32& OutSegmentIndex, float& OutLocalParam) const
	{
		// Handle parameter outside range
		if (Base::PairKnots.Num() < 2)
		{
			OutSegmentIndex = 0;
			OutLocalParam = 0.0f;
			return false;
		}
        
		if (GlobalParam <= Base::PairKnots[0].Value)
		{
			OutSegmentIndex = 0;
			OutLocalParam = 0.0f;
			return true;
		}
        
		if (GlobalParam >= Base::PairKnots.Last().Value)
		{
			OutSegmentIndex = Base::PairKnots.Num() - 2;
			OutLocalParam = 1.0f;
			return true;
		}
        
		int32 SegmentIndex = INDEX_NONE;

		// todo: make CVar
		constexpr bool bEnableSegmentCaching = true;
		if (bEnableSegmentCaching && LastEvaluatedSegment != INDEX_NONE && LastEvaluatedSegment < Base::PairKnots.Num() - 1)
		{
			if (GlobalParam < Base::PairKnots[LastEvaluatedSegment + 1].Value && GlobalParam >= Base::PairKnots[LastEvaluatedSegment].Value)
			{
				// cache hit!
				SegmentIndex = LastEvaluatedSegment;
			}
		}
		
		if (SegmentIndex == INDEX_NONE)
		{
			// cache miss!
			int32 SegmentLow = 0;
			int32 SegmentHigh = Base::PairKnots.Num() - 2;
			SegmentIndex = SegmentLow + (SegmentHigh - SegmentLow) / 2;

			while (SegmentLow <= SegmentHigh)
			{
				int32 Mid = SegmentLow + (SegmentHigh - SegmentLow) / 2;
				const float& StartParam = Base::PairKnots[Mid].Value;
				const float& EndParam   = Base::PairKnots[Mid + 1].Value;

				if (GlobalParam >= StartParam && GlobalParam < EndParam)
				{
					SegmentIndex = Mid;
					break;
				}
				else if (GlobalParam < StartParam)
				{
					SegmentHigh = Mid - 1;
				}
				else
				{
					SegmentLow = Mid + 1;
				}
			}
		}
		
		OutSegmentIndex = SegmentIndex;
		LastEvaluatedSegment = SegmentIndex;
        
		// Calculate local parameter
		float SegmentLength = Base::PairKnots[SegmentIndex + 1].Value - Base::PairKnots[SegmentIndex].Value;
		if (SegmentLength > UE_SMALL_NUMBER)
		{
			OutLocalParam = (GlobalParam - Base::PairKnots[SegmentIndex].Value) / SegmentLength;
		}
		else
		{
			OutLocalParam = 0.0f;
		}
        
		return true;
	}

	/**
	 * Gets the number of segments defined by distinct knots
	 */
	int32 GetNumDistinctSegments() const
	{
		return FMath::Max(0, Base::PairKnots.Num() - 1);
	}

	int32 FindSegmentIndex(float Parameter, float &OutLocalParam) const
	{
		int32 ControlPointIdx = FindIndexForParameter(Parameter, OutLocalParam);
		return ControlPointIdx / 4; // Each segment has 4 control points
	}

	virtual int32 GetExpectedNumKnots() const override
	{
		const int32 NumValues = Base::NumKeys();
		// Each segment has 4 control points. For 1 segment, we need 8 knots (4 start, 4 end).
		int32 NumSegments = NumValues / 4;
    
		// Special handling for incomplete first segment
		if (NumSegments == 0)
		{
			return 0;
		}
		return 4 + (NumSegments - 1) * 3 + 4;
	}
	
	void SetKnotVector(const TArray<FKnot>& NewKnots)
	{
		Base::SetCustomKnots(NewKnots);
	}
	
private:
	
	/**
	 * Computes chord or centripetal distances between segment endpoints (P0 to P3).
	 * Uses those distances to build a proportional knot vector with the proper multiplicities for each Bézier segment.
	 * @param Mode - Knot generation mode to use
	 */
	void GenerateDistanceKnotsForBezier(EParameterizationPolicy Mode)
	{
		TArray<ValueType> Points = this->Values;

		// For a Bezier spline, the control points don't all lie on the curve
		// So we need to compute chord lengths between segment endpoints only
		int NumSegments = Points.Num() / 4;
		
		TArray<float> SegmentLengths;
		SegmentLengths.Reserve(NumSegments);
        
		for (int32 i = 0; i < NumSegments; ++i)
		{
			const int32 StartIndex = i * 4;
			const int32 EndIndex = StartIndex + 3;
            
			if (EndIndex < Points.Num())
			{
				SegmentLengths.Add(ComputeSegmentLength(Points[StartIndex], Points[EndIndex], Mode));
			}
		}
        
		// Now create a knot vector that respects:
		// 1. Bezier segment structure (knot multiplicity)
		// 2. Proportional chord/centripetal lengths
        
		// This will only return Val if Offset is exactly 0.f. Otherwise, it is guaranteed to change in the direction of Offset.
		// We sacrifice a bit of accuracy for the assumption that Val will actually change.
		auto SafeAdd = [](float Val, float Offset) -> float
		{
			if (Offset == 0.f) return Val;
				
			const float Result = Val + Offset;
			if (Result != Val) return Result;

			// Guaranteed one-step progress even under FTZ/DAZ:
			return UE::Geometry::Spline::Param::Step(
				Val, Offset > 0.f ? UE::Geometry::Spline::Param::EDir::Right
								  : UE::Geometry::Spline::Param::EDir::Left);
		};
		
		// Reset pair knots
		this->PairKnots.Reset();
        
		typename Base::FValidKnotSearchParams SearchParams;
		SearchParams.bSearchLeft = false;	// we are appending repeatedly, we want insertion order to be predictable (always go after conflicting knots).

		// Add start knot with multiplicity 4
		SearchParams.DesiredParameter = 0.f;
		this->InsertKnot(FKnot(this->GetNearestAvailableKnotValue(SearchParams), 4));
        
		// Calculate accumulated distances for internal knots
		float AccumLength = 0.0f;
		for (int32 i = 0; i < NumSegments - 1; ++i) // -1 because we don't need knots at the very end
		{
			if (i < SegmentLengths.Num())
			{
				AccumLength = SafeAdd(AccumLength, SegmentLengths[i]);
                
				// Use normalized accumulated length as the desired knot value
				SearchParams.DesiredParameter = AccumLength;
				
				// Interior knots at segment boundaries with multiplicity 3
				this->InsertKnot(FKnot(this->GetNearestAvailableKnotValue(SearchParams), 3));
			}
		}

		SearchParams.DesiredParameter = SafeAdd(AccumLength, SegmentLengths.Num() > 0
			? SegmentLengths.Last()
			: 0.0f);

		this->InsertKnot(FKnot(this->GetNearestAvailableKnotValue(SearchParams), 4));

		// Mark flat knots cache as dirty
		this->MarkFlatKnotsCacheDirty();
	}

    virtual FWindow FindWindow(float Parameter) const override
    {
        FWindow Window = {};
        if (Base::NumKeys() < 4)  // Need at least one complete Bezier segment
        {
            return Window;
        }
		const TArray<ValueType>& Entries = this->Values;
        // Find which segment we're in
    	const int32 NumSegments = Base::NumKeys() / 4;

       	int32 SegmentIndex;
    	float LocalT;
    	MapGlobalParameterToLocalSegment(Parameter, SegmentIndex, LocalT);

        if (Base::IsClosedLoop())
        {
            // For closed loop, wrap the segment index
            SegmentIndex = SegmentIndex % NumSegments;
            if (SegmentIndex < 0) SegmentIndex += NumSegments;
        }
        
        // Each Bezier segment has exactly 4 control points
        const int32 BaseIndex = SegmentIndex * 4;

        if (Base::IsClosedLoop())
        {
            // Handle wrapping for closed loop
            const int32 NumPoints = Entries.Num();
            auto WrapIndex = [NumPoints](int32 Index) -> int32
            {
                return ((Index % NumPoints) + NumPoints) % NumPoints;
            };

        	for (int Idx = 0; Idx < Base::WindowSize; ++Idx)
        	{
        		Window[Idx] = &Entries[WrapIndex(BaseIndex + Idx)];
        	}
        }
        else
        {
        	// Return null window if BaseIndex is too large to fully populate the window.
            if (BaseIndex + 3 >= Entries.Num())
            {
                return Window;
            }

        	// Standard case - no wrapping
        	for (int Idx = 0; Idx < Base::WindowSize; ++Idx)
        	{
        		Window[Idx] = &Entries[BaseIndex + Idx];
        	}
        }

        return Window;
    }

    virtual ValueType InterpolateWindow(TArrayView<const ValueType* const> Window, float Parameter) const override 
    {		
        if (Window.Num() < 4)
        {
            return ValueType();
        }
  		int32 NumSegments = GetNumberOfSegments();
    	int32 SegmentIndex;
    	float LocalT;
    	MapGlobalParameterToLocalSegment(Parameter, SegmentIndex, LocalT);

        // Calculate Bernstein basis functions
        const float t = LocalT;
        const float OneMinusT = 1.0f - t;
        const float t2 = t * t;
        const float OneMinusT2 = OneMinusT * OneMinusT;

        // Cubic Bernstein polynomials
        const float Basis[4] =
        {
            OneMinusT * OneMinusT2,     // (1-t)³
            3.0f * t * OneMinusT2,      // 3t(1-t)²
            3.0f * t2 * OneMinusT,      // 3t²(1-t)
            t * t2                      // t³
        };

        return TSplineInterpolationPolicy<ValueType>::InterpolateWithBasis(Window, Basis);
    }

	/**
	 * Finds the local parameter on a segment closest to the given position
	 * 
	 * @param SegmentIndex Index of the segment
	 * @param Position Position to find closest point to
	 * @param OutSquaredDistance Output parameter for squared distance
	 * @return Local parameter [0,1] within the segment
	 */
	float FindLocalParameterNearestToPosition(
		int32 SegmentIndex,
		const ValueType& Position, 
		float& OutSquaredDistance) const
    {
    	int32 NumSegments = Base::NumKeys() / 4;
    	// Validate segment index
    	if (SegmentIndex < 0 || SegmentIndex >= NumSegments)
    	{
    		OutSquaredDistance = TNumericLimits<float>::Max();
    		return 0.0f;
    	}
    
    	// Get global parameter nearest to position
    	float GlobalParam = FindNearestOnSegment(Position, SegmentIndex, OutSquaredDistance);
		
    	// Convert to local parameter
    	FInterval1f SegmentRange = GetSegmentParameterRange(SegmentIndex);
    	float SegmentLength = SegmentRange.Max - SegmentRange.Min;
    
    	if (SegmentLength == 0.f)
    		return 0.0f;
        
    	return (GlobalParam - SegmentRange.Min) / SegmentLength;
    }
	
	/**
	 * Checks if a segment index is valid
	 * @param Index - Index to check
	 * @return true if the index is valid
	 */
	bool IsValidSegmentIndex(int32 Index) const
    {
    	int32 NumSegments = Base::NumKeys() / 4;
    	return Index >= 0 && Index < NumSegments;
    }

	/**
	 * Helper method to add the first segment to the spline
	 * @param Parameter - Parameter value where to insert the point (Spline Space)
	 * @param ControlPoint - Bezier control point to insert
	 * @param PointIndex - Index of the newly created bezier control point
	 * @return true if the points were added, false otherwise
	 */
	bool InitializeFirstSegment(
		float Parameter,
		const ValueType& ControlPoint,
		int32& PointIndex)
    {
    	if (Base::NumKeys() < GetDegree())
    	{
    		// First point - just store it directly
    		PointIndex = Base::AddValue(ControlPoint);
    		return true;
    	}

    	// Adding the fourth control point would create a segment
    	if (Base::NumKeys() == GetDegree())
    	{
    		const ValueType& P0 = Base::GetValue(0);
    		const ValueType& P1 = Base::GetValue(1);
    		const ValueType& P2 = Base::GetValue(2);
    		const ValueType& P3 = ControlPoint;
	        
    		// Add the first segment
    		SetControlPoints({P0, P1, P2, P3}, EParameterizationPolicy::Centripetal);
    		PointIndex = 3;
    		return true;
    	}
    	return false;
    }

	/**
     * Prepends a point to the spline at the beginning.
     * The new point will be connected to the first segment.
     * @param Position The position of the new point
     * @param ParameterizationPolicy How to distribute Knots
     * @return Index of the new segment
     */
    int32 PrependPoint(
		const ValueType& Position,
		EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
	{
		const TOptional<float> LoopKnotDelta = GetLoopKnotDelta();
		SetClosedLoop(false);
		ON_SCOPE_EXIT
		{
			if (LoopKnotDelta.IsSet())
			{
				SetClosedLoop(true);
				SetParameter(GetNumberOfSegments(), LoopKnotDelta.GetValue());
			}
		};
    
		// Create a control point at the beginning
		ValueType P3 = Base::GetValue(0); // First point of the spline
    
		// Calculate reasonable auto-tangents
		ValueType Dir = P3 - Position;
		ValueType P1 = Position + Dir * 0.33f;
		ValueType P2 = Position + Dir * 0.66f;
    
		// Add the segment by reusing existing code
		int32 NewIndex = AddBezierSegmentInternal(0, Position, P1, P2, P3, ParameterizationPolicy, false);
		check (NewIndex == 0);
		return NewIndex;
	}

	/**
	 * Appends a point to the end of the spline.
	 * Creates a new cubic Bezier segment connecting the last point to the new point.
	 * 
	 * @param Position The position of the new point
	 * @param ParameterizationPolicy How to distribute Knots
	 * @return Index of the new segment
	 */
	int32 AppendPoint(
		const ValueType& Position,
		EParameterizationPolicy ParameterizationPolicy)
	{
		const TOptional<float> LoopKnotDelta = GetLoopKnotDelta();
		SetClosedLoop(false);
		ON_SCOPE_EXIT
		{
			if (LoopKnotDelta.IsSet())
			{
				SetClosedLoop(true);
				SetParameter(GetNumberOfSegments(), LoopKnotDelta.GetValue());
			}
		};
    		
		// Append - create a point at the end
		int32 LastIndex = Base::NumKeys() - 1;
		ValueType P0 = Base::GetValue(LastIndex);
        
		// Calculate reasonable auto-tangents
		ValueType Dir = Position - P0;
		ValueType P1 = P0 + Dir * 0.33f;
		ValueType P2 = P0 + Dir * 0.66f;

		int32 NumSegments = Base::NumKeys() / 4;
		// Add the segment
		int32 NewIndex = AddBezierSegmentInternal(NumSegments, P0, P1, P2, Position, ParameterizationPolicy/*, StartParam*/);

		return NewIndex;
	}
	
	/**
	 * Helper to compute segment length based on parameterization policy
	 */
	float ComputeSegmentLength(
		const ValueType& P0,
		const ValueType& P3,
		EParameterizationPolicy ParameterizationPolicy)
    {
    	switch (ParameterizationPolicy)
    	{
    	case EParameterizationPolicy::ChordLength:
    		return static_cast<float>(Math::Distance(P0, P3));
    	case EParameterizationPolicy::Centripetal:
    		return static_cast<float>(Math::CentripetalDistance(P0, P3));
    	case EParameterizationPolicy::Uniform:
    	default:
			return 1.0f;
    	}
    }

	/**
	 * Applies multiplicity to the internal knots based on the spline's degree
	 */
	void ApplyInternalKnotsMultiplicity()
	{
		// fix internal knots to be clamped to Degree
		for (int32 i = 1; i < Base::PairKnots.Num() - 1; ++i)
		{
			Base::PairKnots[i].Multiplicity = GetDegree();
		}
	}

	int32 AddBezierSegmentInternal(
		int32 SegmentIndex,
		const ValueType& P0,
		const ValueType& P1, 
		const ValueType& P2, 
		const ValueType& P3,
		EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal,
		bool bAppendToSegment = true) // true = connect to end, false = connect to start
	{
		const int32 NumSegments = Base::NumKeys() / 4;
		SegmentIndex = FMath::Clamp(SegmentIndex, 0, NumSegments);
    
		// Calculate segment length
		float SegmentLength = ComputeSegmentLength(P0, P3, ParameterizationPolicy);
    
		// Add the control points
		int32 ControlPointIndex = SegmentIndex * 4;
		Base::InsertValue(ControlPointIndex, P0);
		Base::InsertValue(ControlPointIndex + 1, P1);
		Base::InsertValue(ControlPointIndex + 2, P2);
		Base::InsertValue(ControlPointIndex + 3, P3);

		// FInterval1f RefRange = GetSegmentParameterRange(SegmentIndex);
		bool bAppendToEnd = bAppendToSegment && SegmentIndex > (this->GetNumDistinctSegments() - 1);
		bool bPrependToStart = !bAppendToSegment && SegmentIndex == 0;
	
		// Insert knots based on append/prepend to the segment
		if (NumSegments == 0) // First segment
		{
			this->ResetKnotVector();
			this->InsertKnot( FKnot(0.0f, this->bClampEnds ? this->GetDegree() + 1 : 1));
			this->InsertKnot( FKnot(1.f, this->bClampEnds ? this->GetDegree() + 1 : 1));
		}
		else if (bAppendToEnd)
		{
			const float LastParameter = Base::PairKnots.Last().Value;
			const float CandidateByLength = LastParameter + SegmentLength;
			const float CandidateByStep   = UE::Geometry::Spline::Param::NextDistinct(LastParameter);
			const float NextParameter = FMath::Max(CandidateByLength, CandidateByStep);
			this->InsertKnot(FKnot(NextParameter, this->bClampEnds ? this->GetDegree() : 1));
		}
		else if (bPrependToStart)
		{
			const float FirstParameter = Base::PairKnots[0].Value;
			const float CandidateByLength = FirstParameter - SegmentLength;
			const float CandidateByStep   = UE::Geometry::Spline::Param::PrevDistinct(FirstParameter);
			const float NextParameter = FMath::Min(CandidateByLength, CandidateByStep);
			this->InsertKnot( FKnot(NextParameter, this->bClampEnds ? this->GetDegree() : 1));
		}
	
		this->ApplyClampedKnotsMultiplicity();

		// make sure the internal knots have the correct multiplicity as one of the end knots
		// might have become an internal knot
		this->ApplyInternalKnotsMultiplicity();
		Base::Dump();
		return SegmentIndex;
	}

	/** Returns the length of the parameter range of the closing segment. The returned value is unset if the spline is open. */
	TOptional<float> GetLoopKnotDelta() const
	{
		return Base::IsClosedLoop()
			? Base::PairKnots[Base::PairKnots.Num() - 1].Value - Base::PairKnots[Base::PairKnots.Num() - 2].Value
			: TOptional<float>();
	}

private:

	/** This is the last segment fetched by parameter. Used to accelerate predictable sampling patterns. */
	mutable int32 LastEvaluatedSegment = INDEX_NONE;
};


	
// Common type definitions
using FPolyBezierSpline2f = TPolyBezierSpline<FVector2f>;
using FPolyBezierSpline3f = TPolyBezierSpline<FVector3f>;
using FPolyBezierSpline2d = TPolyBezierSpline<FVector2d>;
using FPolyBezierSpline3d = TPolyBezierSpline<FVector3d>;
} // end namespace UE::Geometry::Spline
} // end namespace UE::Geometry
} // end namespace UE