// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Splines/PolyBezierSpline.h"
namespace UE
{
namespace Geometry
{
namespace Spline
{
	
template<typename VALUETYPE> struct UE_EXPERIMENTAL(5.7, "New spline APIs are experimental.") TTangentBezierControlPoint;
template<typename VALUETYPE> class UE_EXPERIMENTAL(5.7, "New spline APIs are experimental.") TTangentBezierSpline;
	
/**
 * Enum defining how tangents are computed for a spline control point.
 */
enum class UE_EXPERIMENTAL(5.7, "New spline APIs are experimental.") ETangentMode : uint8
{
	/** Automatically compute unclamped tangents based on surrounding points */
	Auto,
    
	/** Automatically compute clamped tangents based on surrounding points */
	AutoClamped,
    
	/** User-specified tangents */
	User,
    
	/** Broken tangents - in and out can be different */
	Broken,
    
	/** Linear tangents aligned with adjacent points */
	Linear,
    
	/** Constant - no interpolation between points */
	Constant,
    
	/** Unknown or invalid tangent mode */
	Unknown
};

/**
 * Structure representing a control point on a tangent-based spline curve
 */
template<typename ValueType>
struct TTangentBezierControlPoint 
{
	/** Position of the control point */
	ValueType Position;

	/** Incoming tangent vector */
	ValueType TangentIn;

	/** Outgoing tangent vector */
	ValueType TangentOut;

	/** Tangent computation mode */
	ETangentMode TangentMode;

	/** Default constructor creates a point at origin with zero tangents */
	TTangentBezierControlPoint ()
		: Position(ValueType())
		, TangentIn(ValueType())
		, TangentOut(ValueType())
		, TangentMode(ETangentMode::Auto)
	{
	}

	/** Constructor with position only - defaults to auto tangents */
	explicit TTangentBezierControlPoint (const ValueType& InPosition)
		: Position(InPosition)
		, TangentIn(ValueType())
		, TangentOut(ValueType())
		, TangentMode(ETangentMode::Auto)
	{
	}

	/** Full constructor */
	TTangentBezierControlPoint (
		const ValueType& InPosition,
		const ValueType& InTangentIn,
		const ValueType& InTangentOut,
		ETangentMode InTangentMode)
		: Position(InPosition)
		, TangentIn(InTangentIn)
		, TangentOut(InTangentOut)
		, TangentMode(InTangentMode)
	{
	}
	
	void Serialize(FArchive& Ar)
	{
		Ar << Position;
		Ar << TangentIn;
		Ar << TangentOut;
		// Serialize as uint8 for compatibility
		uint8 TangentModeValue = static_cast<uint8>(TangentMode);
		Ar << TangentModeValue;
        
		if (Ar.IsLoading())
		{
			TangentMode = static_cast<ETangentMode>(TangentModeValue);
		}
		
	}
    
	friend FArchive& operator<<(FArchive& Ar, TTangentBezierControlPoint& Point)
	{
		Point.Serialize(Ar);
		return Ar;
	}

	bool operator==(const TTangentBezierControlPoint& Other) const
	{
		return Position == Other.Position
			&& TangentIn == Other.TangentIn
			&& TangentOut == Other.TangentOut
			&& TangentMode == Other.TangentMode;
	}
};
	
/**
 * A spline that provides tangent-based control over curve shape while using 
 * piecewise Bezier curves internally for evaluation. Supports both manual
 * tangent control and automatic tangent computation.
 */
template<typename VALUETYPE>
class TTangentBezierSpline :
	public TSplineWrapper<TPolyBezierSpline<VALUETYPE>>,
	private TSelfRegisteringSpline<TTangentBezierSpline<VALUETYPE>, VALUETYPE>
{
public:
	typedef typename TSplineWrapper<TPolyBezierSpline<VALUETYPE>>::ValueType ValueType;
	using TSplineWrapper<TPolyBezierSpline<VALUETYPE>>::InternalSpline;
	using FTangentBezierControlPoint = TTangentBezierControlPoint<ValueType>;

	// Generate compile-time type ID for TangentBezier
	DECLARE_SPLINE_TYPE_ID(
		TEXT("TangentBezier"),
		*TSplineValueTypeTraits<VALUETYPE>::Name
	);
	
	TTangentBezierSpline() = default;
	virtual ~TTangentBezierSpline() override = default;
	
	/** Default constructor with at least one segment */
	TTangentBezierSpline(const ValueType& StartPoint, const ValueType& EndPoint)
		: Tension(0.0f)
		, TangentModes({ETangentMode::Auto, ETangentMode::Auto})
	{
		InternalSpline = FPolyBezierSpline3d::CreateLine(StartPoint, EndPoint);
	}

	/**
	 * Constructor with full control over tangents
	 */
	TTangentBezierSpline(
		const ValueType& StartPoint, 
		const ValueType& EndPoint,
		const ValueType& StartTangent,
		const ValueType& EndTangent,
		bool bAutoTangents = false)
		: Tension(0.0f)
		, TangentModes({bAutoTangents ? ETangentMode::Auto : ETangentMode::User,
							bAutoTangents ? ETangentMode::Auto : ETangentMode::User})
	{
		InternalSpline = FPolyBezierSpline3d(StartPoint,
											StartPoint + StartTangent / 3.0f,
											EndPoint - (EndTangent / 3.0f),
											EndPoint);
	}
   
    /**  Copy constructor */
    TTangentBezierSpline(const TTangentBezierSpline& Other)
	    : Tension(Other.Tension)
	    , TangentModes(Other.TangentModes)
    {
		InternalSpline = Other.InternalSpline;
    }

    /**  Copy assignment */
    TTangentBezierSpline& operator=(const TTangentBezierSpline& Other)
    {
        if (this != &Other)
        {
            Tension = Other.Tension;
            TangentModes = Other.TangentModes;
			bStationaryEndpoint = Other.bStationaryEndpoint;
            InternalSpline = Other.InternalSpline;
        }
        return *this;
    }

	virtual bool IsEqual(const ISplineInterface* OtherSpline) const override
	{
		if (OtherSpline->GetTypeId() == GetTypeId())
		{
			const TTangentBezierSpline* Other = static_cast<const TTangentBezierSpline*>(OtherSpline);
			return operator==(*Other);
		}
		
		return false;
	}

    virtual bool Serialize(FArchive& Ar) override
    {
		// Call immediate parent's Serialize (TSplineWrapper)
		if (!TSplineWrapper<TPolyBezierSpline<ValueType>>::Serialize(Ar))
		{
			return false;
		}
        
        Ar << Tension;
		// Serialize as uint8 for compatibility
		int32 NumTangentModes = TangentModes.Num();
		Ar << NumTangentModes;
        
		if (Ar.IsLoading())
		{
			TangentModes.SetNum(NumTangentModes);
			
			// It was once valid for a spline to have a single bezier point, but we now expect 3 for this case.
			// It is never valid to have 1 or 2 points.
			switch (InternalSpline.NumKeys())
			{
			case 1: InternalSpline.AddValue(ValueType());	// fallthrough
			case 2: InternalSpline.AddValue(ValueType());	// fallthrough
			default: break;
			}
		}
        
		for (int32 i = 0; i < NumTangentModes; ++i)
		{
			uint8 TangentModeValue = static_cast<uint8>(TangentModes[i]);
			Ar << TangentModeValue;
            
			if (Ar.IsLoading())
			{
				TangentModes[i] = static_cast<ETangentMode>(TangentModeValue);
			}
		}
		Ar << bStationaryEndpoint;
		
        return true;
    }

    friend FArchive& operator<<(FArchive& Ar, TTangentBezierSpline& Spline)
    {
        Spline.Serialize(Ar);
        return Ar;
    }

	bool operator==(const TTangentBezierSpline<ValueType>& Other) const
	{
		return	InternalSpline == Other.InternalSpline &&
				Tension == Other.Tension &&
				TangentModes == Other.TangentModes &&
				bStationaryEndpoint == Other.bStationaryEndpoint;
	}

	// Static shape generators

    /**
     * Creates a straight line between two points
     * @param StartPoint - Start point of the line
     * @param EndPoint - End point of the line
     * @return New TangentBezierSpline initialized as a line
     */
    static TTangentBezierSpline CreateLine(
        const ValueType& StartPoint, 
        const ValueType& EndPoint)
    {
		// Create empty spline - we'll manually build it
		TTangentBezierSpline Result;
        
		// Clear the default initialization
		Result.InternalSpline = FPolyBezierSpline3d::CreateLine(StartPoint, EndPoint);
		
        Result.Reparameterize(EParameterizationPolicy::Uniform);
        return Result;
    }

    /**
     * Creates a circular arc with specified parameters
     * @param Center - Center point of the circle
     * @param Radius - Radius of the circle
     * @param StartAngle - Start angle in radians
     * @param EndAngle - End angle in radians
     * @param NumSegments - Number of segments to use (more segments = smoother curve)
     * @return New TangentBezierSpline initialized as an arc
     */
    static TTangentBezierSpline CreateCircleArc(
        const ValueType& Center,
        float Radius,
        float StartAngle,
        float EndAngle,
        int32 NumSegments = 4)
    {
        // Create empty spline - we'll manually build it
        TTangentBezierSpline Result;
        
        // Clear the default initialization
        Result.InternalSpline = FPolyBezierSpline3d::CreateCircleArc(
            Center, Radius, StartAngle, EndAngle, NumSegments);
        
		// Set up tangent status for all points
		Result.TangentModes.Init(ETangentMode::Auto, Result.GetNumPoints());
        
        // Apply consistent parameterization - even though the internal PolyBezier already calls
        // Reparameterize, we need to do it again here to ensure consistency
        Result.Reparameterize(EParameterizationPolicy::Centripetal);
        
        return Result;
    }

    /**
     * Creates a complete circle
     * @param Center - Center point of the circle
     * @param Radius - Radius of the circle
     * @param NumSegments - Number of segments to use (more segments = smoother curve)
     * @return New TangentBezierSpline initialized as a circle
     */
    static TTangentBezierSpline CreateCircle(
        const ValueType& Center,
        float Radius,
        int32 NumSegments = 4)
    {
        // Create the circle
        TTangentBezierSpline Result;
        Result.InternalSpline = FPolyBezierSpline3d::CreateCircle(
            ValueType(Center), Radius, NumSegments);
        
        // Set up tangent status for all points
		Result.TangentModes.Init(ETangentMode::Auto, Result.GetNumPoints());
		
		// Apply consistent parameterization
		Result.Reparameterize(EParameterizationPolicy::Centripetal);
        
        return Result;
    }

    /**
     * Creates an ellipse with specified parameters
     * @param Center - Center point of the ellipse
     * @param RadiusX - Radius along X axis
     * @param RadiusY - Radius along Y axis
     * @param NumSegments - Number of segments to use (more segments = smoother curve)
     * @return New TangentBezierSpline initialized as an ellipse
     */
    static TTangentBezierSpline CreateEllipse(
        const ValueType& Center,
        float RadiusX,
        float RadiusY,
        int32 NumSegments = 4)
    {
        // Create the ellipse
        TTangentBezierSpline Result;
        Result.InternalSpline = FPolyBezierSpline3d::CreateEllipse(
            Center, RadiusX, RadiusY, NumSegments);
        
		// Set up tangent status for all points
		Result.TangentModes.Init(ETangentMode::Auto, Result.GetNumPoints());
        
        // Apply consistent parameterization
        Result.Reparameterize(EParameterizationPolicy::Centripetal);
        
        return Result;
    }
	virtual TUniquePtr<ISplineInterface> Clone() const override
	{
		TUniquePtr<TTangentBezierSpline<VALUETYPE>> Clone = MakeUnique<TTangentBezierSpline<VALUETYPE>>();
    
		// Copy internal spline
		Clone->InternalSpline = this->InternalSpline;
    
		// Copy tangent settings
		Clone->Tension = this->Tension;
		Clone->TangentModes = this->TangentModes;
		Clone->bStationaryEndpoint = this->bStationaryEndpoint;
    
		// Copy infinity modes
		Clone->PreInfinityMode = this->PreInfinityMode;
		Clone->PostInfinityMode = this->PostInfinityMode;
    
		return Clone;
	}

	virtual bool IsClosedLoop() const override
	{
		return InternalSpline.IsClosedLoop();
	}

	virtual void SetClosedLoop(bool bInClosedLoop) override
	{
        // Skip if state isn't changing
	    if (bInClosedLoop == InternalSpline.IsClosedLoop())
	        return;
	        
	    if (bInClosedLoop)
	    {
	        const int32 NumPoints = GetNumPoints();
	        if (NumPoints >= 2)
	        {
	            // Get first and last points
	            const ValueType FirstPos = GetValue(0);
	            const ValueType LastPos = GetValue(NumPoints - 1);
	            
	            // Compute tangents for the closure segment
	            ValueType LastOutTangent, FirstInTangent;
	            
	            if (IsAutoTangent(NumPoints - 1) && IsAutoTangent(0))
	            {
	                // Auto tangents for closure
	                ValueType ClosingTangent = (FirstPos - LastPos) * (1.0f - Tension);
	                LastOutTangent = ClosingTangent;
	                FirstInTangent = -ClosingTangent;
	            }
	            else
	            {
	                // Use existing tangents
	                LastOutTangent = GetTangentOut(NumPoints - 1);
	                FirstInTangent = GetTangentIn(0);
	            }
	            
	            // Calculate control points for the closing segment
	            ValueType P0 = LastPos;
	            ValueType P3 = FirstPos;
	            ValueType P1 = P0 + (LastOutTangent / 3.0f);
	            ValueType P2 = P3 - (FirstInTangent / 3.0f);
	            
	            // Add the closing segment to the internal spline
	            InternalSpline.AppendBezierSegment(P1, P2, P3);
	        	
	        	InternalSpline.SetClosedLoopFlag(bInClosedLoop);
	        }
	    }
	    else
	    {
	    	// For an open spline, remove the closing segment first
	    	// then update the internal flag
	    	const int32 NumSegments = GetNumberOfSegments();
	    	if (NumSegments > 0)
	    	{
	    		// Remove the last segment
	    		InternalSpline.RemoveSegment(NumSegments - 1);
	    	}
        
	    }

		// update flag on the internal spline
		InternalSpline.SetClosedLoopFlag(bInClosedLoop);
	    	
	    
	    // Ensure auto tangents are updated after changing loop state
	    UpdateTangents();
    }
    
	/**
	 * Replaces all points in the spline with the provided points.
	 * 
	 * @param Points - Array of control points to set
	 * @return True if the points were successfully set
	 */
	bool SetControlPoints(const TArray<FTangentBezierControlPoint>& Points)
	{
	    const int32 NumPoints = Points.Num();
		
		Clear();
		
	    // Special case for less than 2 points
	    if (NumPoints < 2)
	    {
	    	if (NumPoints == 1)
	    	{
	    		// Initialize the spline with a single point
	    		int32 OutNewIndex;
	    		return InitializeFirstSegment(Points[0], OutNewIndex);
	    	}
	    	return true;
	    }
	    
	    // Convert to cubic Bezier control points for the internal spline
	    TArray<ValueType> BezierPoints;
	    
	    // For each segment (between consecutive points), we need 4 control points
	    // Reserve space based on number of segments
	    const int32 NumSegments = NumPoints - 1;
	    BezierPoints.Reserve(NumSegments * 4);
	    
	    for (int32 i = 0; i < NumSegments; ++i)
	    {
	        const FTangentBezierControlPoint& StartPoint = Points[i];
	        const FTangentBezierControlPoint& EndPoint = Points[i + 1];
	        
	        ValueType P0 = StartPoint.Position;
	        ValueType P3 = EndPoint.Position;
	        
	        // Calculate tangents based on the points' tangent modes
	        ValueType OutTangent, InTangent;
	        
	        // Process start point's outgoing tangent
	        if (StartPoint.TangentMode == ETangentMode::Auto || 
	            StartPoint.TangentMode == ETangentMode::AutoClamped)
	        {
	            // Compute auto tangent
	            if (i == 0 && NumPoints > 2)
	            {
	                // First point - use forward difference
	                OutTangent = (1.0f - Tension) * (Points[i + 1].Position - Points[i].Position);
	            }
	            else if (i == NumPoints - 2 && NumPoints > 2)
	            {
	                // Point before last - use central difference
	                OutTangent = (1.0f - Tension) * (Points[i + 1].Position - Points[i - 1].Position);
	            }
	            else if (NumPoints > 2)
	            {
	                // Normal case - use central difference
	                OutTangent = (1.0f - Tension) * (Points[i + 1].Position - Points[i - 1].Position);
	            }
	            else
	            {
	                // Only two points - use forward difference
	                OutTangent = (1.0f - Tension) * (Points[i + 1].Position - Points[i].Position);
	            }
	            
	            // Apply clamping for AutoClamped
	            if (StartPoint.TangentMode == ETangentMode::AutoClamped)
	            {
	                const float SegmentLength = Math::Distance(P3, P0);
	                const float MaxTangentSize = SegmentLength * 0.33f;
	                
	                if (Math::Size(OutTangent) > MaxTangentSize && !FMath::IsNearlyZero(Math::Size(OutTangent)))
	                {
	                    OutTangent = Math::GetSafeNormal(OutTangent) * MaxTangentSize;
	                }
	            }
	        }
	        else if (StartPoint.TangentMode == ETangentMode::Linear)
	        {
	            // Linear tangent
	            OutTangent = (P3 - P0) * 0.33f;
	        }
	        else if (StartPoint.TangentMode == ETangentMode::Constant)
	        {
	            // Zero tangent for constant interpolation
	            OutTangent = ValueType();
	        }
	        else
	        {
	            // User or Broken mode - use provided tangent
	            OutTangent = StartPoint.TangentOut;
	        }
	        
	        // Process end point's incoming tangent
	        if (EndPoint.TangentMode == ETangentMode::Auto || 
	            EndPoint.TangentMode == ETangentMode::AutoClamped)
	        {
	            // Compute auto tangent
	            if (i + 1 == 0 && NumPoints > 2)
	            {
	                // First point - use central difference
	                InTangent = (1.0f - Tension) * (Points[i + 2].Position - Points[i].Position);
	            }
	            else if (i + 1 == NumPoints - 1 && NumPoints > 2)
	            {
	                // Last point - use backward difference
	                InTangent = (1.0f - Tension) * (Points[i + 1].Position - Points[i].Position);
	            }
	            else if (NumPoints > 2)
	            {
	                // Normal case - use central difference
	                InTangent = (1.0f - Tension) * (Points[i + 2].Position - Points[i].Position);
	            }
	            else
	            {
	                // Only two points - use backward difference
	                InTangent = (1.0f - Tension) * (Points[i + 1].Position - Points[i].Position);
	            }
	            
	            // Apply clamping for AutoClamped
	            if (EndPoint.TangentMode == ETangentMode::AutoClamped)
	            {
	                const float SegmentLength = Math::Distance(P3, P0);
	                const float MaxTangentSize = SegmentLength * 0.33f;
	                
	                if (Math::Size(InTangent) > MaxTangentSize && !FMath::IsNearlyZero(Math::Size(InTangent)))
	                {
	                    InTangent = Math::GetSafeNormal(InTangent) * MaxTangentSize;
	                }
	            }
	        }
	        else if (EndPoint.TangentMode == ETangentMode::Linear)
	        {
	            // Linear tangent
	            InTangent = (P3 - P0) * 0.33f;
	        }
	        else if (EndPoint.TangentMode == ETangentMode::Constant)
	        {
	            // Zero tangent for constant interpolation
	            InTangent = ValueType();
	        }
	        else
	        {
	            // User or Broken mode - use provided tangent
	            InTangent = EndPoint.TangentIn;
	        }
	        
	        // Calculate bezier control points
	        ValueType P1 = P0 + OutTangent / 3.0f;
	        ValueType P2 = P3 - InTangent / 3.0f;
	        
	        // Add all 4 control points for this segment
	        BezierPoints.Add(P0);
	        BezierPoints.Add(P1);
	        BezierPoints.Add(P2);
	        BezierPoints.Add(P3);
	    }
	    
	    // Set the control points in the internal spline
	    InternalSpline.SetControlPoints(BezierPoints, EParameterizationPolicy::Centripetal);
	    
	    // Store tangent modes for later use
	    TangentModes.Reset(NumPoints);
	    for (const FTangentBezierControlPoint& Point : Points)
	    {
	        TangentModes.Add(Point.TangentMode);
	    }
	    
	    // Update tangents to ensure proper curve continuity
	    UpdateTangents();
	    	    
	    return true;
	}

	/**
	 * Adds multiple points to the spline.
	 * 
	 * @param Points - Array of control points to add
	 * @param bAppend - If true, append to the end of spline; if false, prepend to the beginning
	 * @return True if the points were successfully added
	 */
	 bool AddControlPoints(
        const TArray<FTangentBezierControlPoint>& Points,
        bool bAppend = true)
    {
        const int32 NumPoints = Points.Num();
        
        // Need at least 2 points to add a valid segment
        if (NumPoints < 2)
        {
            UE_LOG(LogSpline, Warning, TEXT("AddControlPoints requires at least 2 points to add a valid segment. Got %d points."), NumPoints);
            return false;
        }
        
        // If the spline is currently empty (though it shouldn't be based on our design),
        // this is equivalent to SetControlPoints
        if (GetNumPoints() == 0)
        {
            return SetControlPoints(Points);
        }
        
        if (bAppend)
        {
            // For each point, use AddPoint 
            for (int32 i = 0; i < NumPoints; ++i)
            {
                // Skip the first point if it's the same as our last existing point 
                // (to avoid duplicates at connection point)
                if (i == 0)
                {
                    const int32 ExistingNumPoints = GetNumPoints();
                    const ValueType LastExistingPoint = GetValue(ExistingNumPoints - 1);
                    
                    // Check if connection points are reasonably close
                    constexpr double ConnectionTolerance = 1e-4;
                    const double DistanceSquared = Math::SizeSquared(LastExistingPoint - Points[0].Position);
                    
                    if (DistanceSquared <= ConnectionTolerance)
                    {
                        // Points are close - the first existing point is the same as our first new point
                        // So update the last point's tangent mode if needed
                        if (Points[0].TangentMode != ETangentMode::User && Points[0].TangentMode != ETangentMode::Broken)
                        {
                            SetPointTangentMode(ExistingNumPoints - 1, Points[0].TangentMode);
                        }
                        continue; // Skip adding this point
                    }
                }
                
                // Add each point normally
                AppendPoint(Points[i]);
            }
        }
        else
        {
            // For prepending, we need to add points in reverse order
            for (int32 i = NumPoints - 1; i >= 0; --i)
            {
                // Skip the last point if it's the same as our first existing point
                if (i == NumPoints - 1)
                {
                    const ValueType FirstExistingPoint = GetValue(0);
                    
                    // Check if connection points are reasonably close
                    constexpr double ConnectionTolerance = 1e-4;
                    const double DistanceSquared = Math::SizeSquared(FirstExistingPoint - Points[i].Position);
                    
                    if (DistanceSquared <= ConnectionTolerance)
                    {
                        // Points are close - update first point's tangent mode if needed
                        if (Points[i].TangentMode != ETangentMode::User && Points[i].TangentMode != ETangentMode::Broken)
                        {
                            SetPointTangentMode(0, Points[i].TangentMode);
                        }
                        continue; // Skip adding this point
                    }
                }
                
                // Prepend each point
                PrependPoint(Points[i]);
            }
        }
        
        // Reparameterize for consistent parameter distribution
        Reparameterize();
        
        return true;
    }

	

    /**
     * Inserts a new point at the specified parameter along the spline.
     * 
     * @param Parameter - Parameter value where to insert the point (Spline Space)
	 * @param ControlPoint - Control point to insert
     * @return The index of the newly created point
     */
    int32 InsertPointAtGlobalParam(float Parameter, 
                     const FTangentBezierControlPoint& ControlPoint)
    {
		int32 SegmentIndex;
		float LocalT;
		bool bValidSegment = InternalSpline.MapGlobalParameterToLocalSegment(Parameter, SegmentIndex, LocalT);

		return InsertPointAtSegmentParam(SegmentIndex, LocalT, ControlPoint);
    }

	/**
	 * Inserts a point at a specific segment with local parameter.
	 * This splits the segment at the specified location and places the new point
	 * at the provided position, which may change the spline shape.
	 * 
	 * @param SegmentIndex Index of the segment to split
	 * @param LocalT Local parameter [0,1] within the segment
	 * @param Position Position for the new point
	 * @param ParameterizationPolicy How to distribute parameters
	 * @return The index of the newly created point
	 */
	int32 InsertPointAtSegmentParam(
		int32 SegmentIndex,
		float LocalT,
		const FTangentBezierControlPoint& ControlPoint,
		EParameterizationPolicy ParameterizationPolicy = EParameterizationPolicy::Centripetal)
	{
		int32 OutNewIndex;
		// Special case: handle first two points
		if (InitializeFirstSegment(ControlPoint, OutNewIndex))
		{
			return OutNewIndex;
		}
		
		// Insert at that parameter with the exact position
		// Have the internal spline handle the insertion
		int32 InsertedPointIndex = InternalSpline.InsertPointAtSegmentParam(SegmentIndex, LocalT, ControlPoint.Position);
		
		// If insertion was successful, update tangents if not auto-computed
		if (InsertedPointIndex >= 0 &&
			(ControlPoint.TangentMode == ETangentMode::User ||
			 ControlPoint.TangentMode == ETangentMode::Broken))
		{
			SetTangentIn(InsertedPointIndex, ControlPoint.TangentIn);
			SetTangentOut(InsertedPointIndex, ControlPoint.TangentOut);
		}

		TangentModes.InsertDefaulted(InsertedPointIndex);
		SetPointTangentMode(InsertedPointIndex, ControlPoint.TangentMode);
				
		// Update adjacent points if they have automatic tangents
		if (InsertedPointIndex > 0 && 
			(TangentModes[InsertedPointIndex-1] == ETangentMode::Auto || 
			 TangentModes[InsertedPointIndex-1] == ETangentMode::AutoClamped))
		{
			UpdatePointTangents(InsertedPointIndex-1, TangentModes[InsertedPointIndex-1]);
		}
        
		if (InsertedPointIndex+1 < GetNumPoints() && 
			(TangentModes[InsertedPointIndex+1] == ETangentMode::Auto || 
			 TangentModes[InsertedPointIndex+1] == ETangentMode::AutoClamped))
		{
			UpdatePointTangents(InsertedPointIndex+1, TangentModes[InsertedPointIndex+1]);
		}
		return InsertedPointIndex;
	}

    /**
     * Inserts a new point at the specified parameter along the spline.
     * 
     * @param PointIndex - Index of the new Point
	 * @param ControlPoint - Control point to insert
     * @return The index of the newly created point
     */
    int32 InsertPointAtPosition(int32 PointIndex, 
                     const FTangentBezierControlPoint& ControlPoint)
    {
	    int32 NewPointIndex;
	    if (InitializeFirstSegment(ControlPoint, NewPointIndex)) return NewPointIndex;

	    const int32 NumPoints = GetNumPoints();
		// need to convert point index to segment index while also clamping,
		// also keeping in mind that we could append or prepend a segment
		const int32 SegmentIndex = FMath::Clamp(PointIndex - 1, 0, NumPoints - 1);
		// Insert at that parameter with the exact position
        // Have the internal spline handle the insertion
        int32 InsertedPointIndex = InternalSpline.InsertPointAtPosition(SegmentIndex, ControlPoint.Position);
		
		// If insertion was successful, update tangents if not auto-computed
		if (InsertedPointIndex >= 0 &&
			(ControlPoint.TangentMode == ETangentMode::User ||
			 ControlPoint.TangentMode == ETangentMode::Broken))
		{
			SetTangentIn(InsertedPointIndex, ControlPoint.TangentIn);
			SetTangentOut(InsertedPointIndex, ControlPoint.TangentOut);
		}
		
		TangentModes.InsertDefaulted(InsertedPointIndex);
		SetPointTangentMode(InsertedPointIndex, ControlPoint.TangentMode);
		
		// Update adjacent points if they have automatic tangents
		if (InsertedPointIndex > 0 && 
			(TangentModes[InsertedPointIndex-1] == ETangentMode::Auto || 
			 TangentModes[InsertedPointIndex-1] == ETangentMode::AutoClamped))
		{
			UpdatePointTangents(InsertedPointIndex-1, TangentModes[InsertedPointIndex-1]);
		}
        
		if (InsertedPointIndex + 1 < GetNumPoints() && 
			(TangentModes[InsertedPointIndex+1] == ETangentMode::Auto || 
			 TangentModes[InsertedPointIndex+1] == ETangentMode::AutoClamped))
		{
			UpdatePointTangents(InsertedPointIndex+1, TangentModes[InsertedPointIndex+1]);
		}
        return InsertedPointIndex;
    }
    
    /**
	 * Helper method to prepend a point to the spline.
	 * 
	 * @param ControlPoint Control point structure to prepend
	 * @return Index of the new point
	 */
	void PrependPoint(const FTangentBezierControlPoint& ControlPoint)
	{
		bool bWasClosed = IsClosedLoop();
		SetClosedLoop(false);
		ON_SCOPE_EXIT
		{
			if (bWasClosed)
			{
				SetClosedLoop(true);
			}
		};
		
    	int32 OutNewIndex;

    	// Special case: handle first two points
    	if (InitializeFirstSegment(ControlPoint, OutNewIndex))
    	{
    		return;
    	}
		        
		// Calculate control points for the cubic Bezier segment
		ValueType P0 = ControlPoint.Position;
        
		// Handle tangent computation based on mode
		ValueType OutTangent;
        
		if (ControlPoint.TangentMode == ETangentMode::User || 
			ControlPoint.TangentMode == ETangentMode::Broken)
		{
			// Use the provided tangent for user-specified mode
			OutTangent = ControlPoint.TangentOut;
		}
		else
		{
			// For auto modes, use a reasonable default that will be updated later
			OutTangent = GetValue(0) - ControlPoint.Position;
		}
        
		ValueType P1 = P0 + (OutTangent / 3.0f);
		ValueType P2 = GetValue(0) - (GetTangentIn(0) / 3.0f);
        
		// Let InternalSpline handle the prepending efficiently
		InternalSpline.PrependBezierSegment(P0, P1, P2);
        
		TangentModes.InsertDefaulted(0);
		SetPointTangentMode(0, ControlPoint.TangentMode);
		
		// Update tangents for the next point if it's auto
		if (GetNumPoints() > 0 && 
			(TangentModes[1] == ETangentMode::Auto || TangentModes[1] == ETangentMode::AutoClamped))
		{
			UpdatePointTangents(1, TangentModes[1]);
		}
	}
    /** 
	 * Appends a control point to the end of the spline.
	 * @param ControlPoint Structure containing position and tangent information
     */
    void AppendPoint(const FTangentBezierControlPoint& ControlPoint)
    {
		bool bWasClosed = IsClosedLoop();
		SetClosedLoop(false);
		ON_SCOPE_EXIT
		{
			if (bWasClosed)
			{
				SetClosedLoop(true);
			}
		};
		
    	int32 NewIndex = GetNumPoints();

    	// Special case: handle first two points
    	if (InitializeFirstSegment(ControlPoint, NewIndex))
    	{
    		return;
    	}
		
    	
	    // Normal case - add segment to existing spline
        ValueType P0 = GetValue(NewIndex - 1);
	    
		        
		ValueType P3 = ControlPoint.Position;
    
		// Update the previous point's tangents if needed
		if (TangentModes[NewIndex - 1] == ETangentMode::Auto || 
			TangentModes[NewIndex - 1] == ETangentMode::AutoClamped)
		{
			UpdatePointTangents(NewIndex - 1, TangentModes[NewIndex - 1]);
		}
    
		// Get its outgoing tangent (may have been updated above)
		ValueType OutTangentPrev = GetTangentOut(NewIndex - 1);
    
		// Calculate control points for the new segment
		ValueType P1 = P0 + (OutTangentPrev / 3.0f);
    
		// For the incoming tangent of the new point
		ValueType P2;
    
		if (ControlPoint.TangentMode == ETangentMode::User || 
			ControlPoint.TangentMode == ETangentMode::Broken)
		{
			// For user-defined tangents, use the provided tangent
			P2 = P3 - (ControlPoint.TangentIn / 3.0f);
		}
		else
		{
			// For other modes, we'll set up a reasonable default
			// The UpdatePointTangents call below will update it properly
			ValueType DefaultTangent = (P3 - P0) * (1.0f - Tension);
			P2 = P3 - (DefaultTangent / 3.0f);
		}
    
		// Add the segment
		InternalSpline.AppendBezierSegment(P1, P2, P3);
		
		TangentModes.InsertDefaulted(NewIndex);
		SetPointTangentMode(NewIndex, ControlPoint.TangentMode);

        // If this is auto mode, update tangents for adjacent points
        if (ControlPoint.TangentMode == ETangentMode::Auto || 
            ControlPoint.TangentMode == ETangentMode::AutoClamped)
        {
            // Update previous point's tangents again to ensure continuity
            if (TangentModes[NewIndex - 1] == ETangentMode::Auto || 
                TangentModes[NewIndex - 1] == ETangentMode::AutoClamped)
            {
                UpdatePointTangents(NewIndex - 1, TangentModes[NewIndex - 1]);
            }
        }
    }
	
	/**
	 * Gets the control point at the specified index
	 * @param Index Index of the point to retrieve
	 * @return Control point structure with position and tangent information
	 */
	FTangentBezierControlPoint GetControlPoint(int32 Index) const
	{
		const int32 NumPoints = GetNumPoints();
		if (Index < 0 || Index >= NumPoints)
		{
			return FTangentBezierControlPoint();
		}
    
		FTangentBezierControlPoint Result;
		Result.Position = GetValue(Index);
		Result.TangentIn = GetTangentIn(Index);
		Result.TangentOut = GetTangentOut(Index);
    
		// Get tangent mode
		if (TangentModes.IsValidIndex(Index))
		{
			Result.TangentMode = TangentModes[Index];
		}
		else
		{
			Result.TangentMode = ETangentMode::Auto;
		}
    
		return Result;
	}
	
    /**
     * Removes a point from the spline
     * @param Index - Index of the point to remove
     */
    void RemovePoint(int32 Index)
	{
		const int32 NumPoints = GetNumPoints();
		if (Index < 0 || Index >= NumPoints)
		{
			return;
		}
		// special case for handling the last point and second last point
		if (NumPoints == 1)
		{
			// Removing the only point
			InternalSpline.RemoveValue(0);
			TangentModes.Reset();
			return;
		}
		else if (NumPoints == 2 && !IsClosedLoop())
		{
			ValueType PadValue = Index == 0 ? InternalSpline.GetValue(2) : InternalSpline.GetValue(1);
		
			// Removing the last point and tangents
			// For CP = 0, we remove P0, P1, P2
			// For CP = 1, we remove P1, P2, P3
			InternalSpline.RemoveValue(Index+2);
			InternalSpline.RemoveValue(Index+1);
			InternalSpline.RemoveValue(Index);
			TangentModes.RemoveAt(Index);
			
			// If we remove point 0:
			// Values = < p3, p2, p2 >
			
			// Ff we remove point 1:
			// Values = < p0, p1, p1 >

			InternalSpline.AddValue(PadValue);
			InternalSpline.AddValue(PadValue);
			
			return;
		}
    
		// Remove the corresponding tangent mode
		if (TangentModes.IsValidIndex(Index))
		{
			TangentModes.RemoveAt(Index);
		}

		InternalSpline.RemovePoint(Index);
    
		// Ensure tangent modes array size matches number of points after removal
		if (TangentModes.Num() > GetNumPoints())
		{
			TangentModes.SetNum(GetNumPoints());
		}
    
		// Update tangents for adjacent points
		UpdateTangents();
		
    }

	/** 
	 * Modifies an existing point
	 * @param Index - Index of the point to modify
	 * @param ControlPoint - New control point data
	 */
	void ModifyPoint(int32 Index, const FTangentBezierControlPoint& ControlPoint)
	{
       const int32 NumPoints = GetNumPoints();
        if (Index < 0 || Index >= NumPoints)
        {
            return;
        }

        const ValueType OldPosition = GetValue(Index);
        bool bPositionChanged = (OldPosition != ControlPoint.Position);
        
        SetPointTangentMode(Index, ControlPoint.TangentMode);

        // Update position
        if (Index == 0)
        {
            // First point
            UpdateBezierControlPoint(Index * 4, ControlPoint.Position);

        	if (IsClosedLoop())
        	{
        		UpdateBezierControlPoint((NumPoints - 1) * 4 + 3, ControlPoint.Position);
        	}
        }
        else if (Index == NumPoints - 1)
        {
            // Last point
            UpdateBezierControlPoint(Index * 4 - 1, ControlPoint.Position);
        	if (IsClosedLoop())
        	{
        		UpdateBezierControlPoint(Index * 4, ControlPoint.Position);
        	}
        }
        else
        {
            // Middle point - affects two segments
            UpdateBezierControlPoint((Index - 1) * 4 + 3, ControlPoint.Position);
            UpdateBezierControlPoint(Index * 4, ControlPoint.Position);
        }
        
        // If not auto tangents, apply the specified tangents
        if (ControlPoint.TangentMode != ETangentMode::Auto && 
            ControlPoint.TangentMode != ETangentMode::AutoClamped && 
            ControlPoint.TangentMode != ETangentMode::Linear && 
            ControlPoint.TangentMode != ETangentMode::Constant)
        {
            SetTangentIn(Index, ControlPoint.TangentIn);
            SetTangentOut(Index, ControlPoint.TangentOut);
        }
        
        // Update tangents based on tangent mode
        UpdatePointTangents(Index, ControlPoint.TangentMode);

		// Update surrounding points if position changed and they're auto tangents
        if (bPositionChanged)
        {
            if (Index > 0 && (TangentModes[Index-1] == ETangentMode::Auto || 
                              TangentModes[Index-1] == ETangentMode::AutoClamped))
            {
                UpdatePointTangents(Index-1, TangentModes[Index-1]);
            }
            
            if (Index < NumPoints-1 && (TangentModes[Index+1] == ETangentMode::Auto ||
                                       TangentModes[Index+1] == ETangentMode::AutoClamped))
            {
                UpdatePointTangents(Index+1, TangentModes[Index+1]);
            }
        }
    }

	void SetValue(int32 Index, const ValueType& NewValue)
	{
		FTangentBezierControlPoint NewPoint = GetControlPoint(Index);
		NewPoint.Position = NewValue;
		ModifyPoint(Index, NewPoint);
	}
	
    /** Gets position at specified parent space parameter */
    ValueType GetValue(float Parameter) const
    {
        return InternalSpline.Evaluate(Parameter);
    }
    
    /** Gets the value of the specified point */
    ValueType GetValue(int32 Index) const
    {
    	const int32 NumKeys = InternalSpline.NumKeys();

    	if (NumKeys == 0 || Index < 0 || Index >= GetNumPoints())
    		return ValueType();
    
    	if (NumKeys == 1)
    	{
    		// Special case when we only have one point
    		if (Index == 0)
    			return InternalSpline.GetValue(0);
    		
    		return ValueType();
    	}

    	// Regular case - convert from point index to bezier control point index
    	if (Index == 0)
    	{
    		// First point is P0 of first segment
    		return InternalSpline.GetValue(0);
    	}
    	else
    	{
    		// Other points are P3 of previous segments
    		const int32 ControlPointIndex = (Index - 1) * 4 + 3;
    		if (ControlPointIndex < NumKeys)
    			return InternalSpline.GetValue(ControlPointIndex);
    	}

    	return ValueType();
    }

    /** Gets tangent at parameter in spline space */
    ValueType GetTangent(float Parameter) const
    {
        return InternalSpline.template EvaluateDerivative<1>(Parameter);
    }
    
    /** Gets incoming tangent for the specified point */
    ValueType GetTangentIn(int32 Index) const
    {
		const int32 NumPoints = GetNumPoints();

		if (NumPoints <= 1)
		{
			return ValueType();
		}
		
        if (Index == 0)
        {
            if (IsClosedLoop())
            {
            	// The point at NumPoints when the spline is closed is the point that is coincident with point 0.
                return GetTangentIn(NumPoints);
            }
            else
            {
            	// If spline is unclosed, in tangent == out tangent at first point.
				return GetTangentOut(Index);
            }
        }
        
        // Normal case - get from previous segment's control points
        const int32 BezierIndex = (Index - 1) * 4;
        if (BezierIndex + 3 < InternalSpline.NumKeys())
        {
            ValueType P3 = InternalSpline.GetValue(BezierIndex + 3);
            ValueType P2 = InternalSpline.GetValue(BezierIndex + 2);
            return (P3 - P2) * 3.0f;
        }
        return ValueType();
    }
    
	/** Gets outgoing tangent for the specified point */
	ValueType GetTangentOut(int32 Index) const
	{
		const int32 NumPoints = GetNumPoints();

		if (NumPoints <= 1)
		{
			return ValueType();
		}
		
		if (Index == NumPoints - 1 && !IsClosedLoop())
		{
			// If spline is unclosed, out tangent == in tangent at final point.
			return GetTangentIn(Index);
		}
		
		// Normal case - get from next segment's control points
		const int32 BezierIndex = Index * 4;
		if (BezierIndex + 1 < InternalSpline.NumKeys())
		{
			ValueType P0 = InternalSpline.GetValue(BezierIndex);
			ValueType P1 = InternalSpline.GetValue(BezierIndex + 1);
			return (P1 - P0) * 3.0f;
		}
		return ValueType();
	}

    /** Sets tangent in for the specified point */
    void SetTangentIn(int32 Index, const ValueType& NewTangent)
    {
		if (Index < 0 || Index >= GetNumPoints())
		{
			return;
		}
		
        if (Index == 0)
        {
            // Special case for first point in closed loop
            if (InternalSpline.IsClosedLoop() && GetNumberOfSegments() > 0)
            {
                const int32 LastSegment = GetNumberOfSegments() - 1;
                ValueType P3 = InternalSpline.GetValue(LastSegment * 4 + 3);
                ValueType P2 = P3 - (NewTangent / 3.0f);
                UpdateBezierControlPoint(LastSegment * 4 + 2, P2);
            }
            return;
        }
        
        // Normal case - update previous segment's P2 control point
        const int32 BezierIndex = (Index - 1) * 4 + 2;
        if (BezierIndex < InternalSpline.NumKeys())
        {
            ValueType P3 = InternalSpline.GetValue(BezierIndex + 1);
            ValueType P2 = P3 - (NewTangent / 3.0f);
            UpdateBezierControlPoint(BezierIndex, P2);
        }
    }
    
    /** Sets tangent out for the specified point */
    void SetTangentOut(int32 Index, const ValueType& NewTangent)
    {
        const int32 NumPoints = GetNumPoints();
        if (Index == NumPoints - 1 && !IsClosedLoop())
        {
            return;
        }
        
        // Normal case - update next segment's P1 control point
        const int32 BezierIndex = Index * 4 + 1;
        if (BezierIndex < InternalSpline.NumKeys())
        {
            ValueType P0 = InternalSpline.GetValue(BezierIndex - 1);
            ValueType P1 = P0 + (NewTangent / 3.0f);
            UpdateBezierControlPoint(BezierIndex, P1);
        }
    }

    /** Gets the number of points in the spline */
    int32 GetNumPoints() const
    {
    	const int32 NumKeys = InternalSpline.NumKeys();
    
    	switch (NumKeys)
    	{
    	case 0:
    		return 0;
    	case 1:
    		// intentional fallthrough!
    	case 2:
    		ensureAlwaysMsgf(false, TEXT("Invalid number of bezier points!"));
    	case 3:
			return 1;
    	default:
			return IsClosedLoop() ? GetNumberOfSegments() : GetNumberOfSegments() + 1;
    	}
    }

	virtual int32 GetNumberOfSegments() const override
	{
		return InternalSpline.GetNumberOfSegments();
	}

	virtual FInterval1f GetSegmentParameterRange(int32 SegmentIndex) const override
	{
		return InternalSpline.GetSegmentParameterRange(SegmentIndex);
	}

    /** Checks if a point's tangents are automatically computed */
    bool IsAutoTangent(int32 Index) const
    {
		return TangentModes.IsValidIndex(Index) && (TangentModes[Index] == ETangentMode::Auto || TangentModes[Index] == ETangentMode::AutoClamped);
    }

	/**
	  * Sets the tangent mode for a specific point
	  * @param Index - Index of the point
	  * @param Mode - New tangent mode to set
	  */
	void SetPointTangentMode(int32 Index, ETangentMode Mode)
	{
		if (Index < 0 || Index > GetNumPoints())
		{
			return;
		}
        
		TangentModes[Index] = Mode;

		// Update tangents based on new mode
        UpdatePointTangents(Index, Mode);
	}
	
	virtual void Clear() override
	{
		InternalSpline.Clear();
		TangentModes.Empty();
	}
	
	float GetParameter(int32 Index) const
	{
		const int32 ControlPointIndex = (Index == 0) ? 0 : ((Index - 1) * 4 + 3);
		return InternalSpline.GetParameter(ControlPointIndex);
	}

	int32 SetParameter(int32 Index, float NewParameter)
    {
		const int32 ControlPointIndex = (Index == 0) ? 0 : ((Index - 1) * 4 + 3);
		const int32 NewIndex = (InternalSpline.SetParameter(ControlPointIndex, NewParameter) + 1) / 4;

		// We likely need to do better in PolyBezier's SetParameter to account for user tangents, but this works for now.
		if (NewIndex != Index)
		{
			UpdateTangents();

			UE_LOG(LogSpline, Verbose, TEXT("\t"));
			UE_LOG(LogSpline, Verbose, TEXT("After Tangent Recompute:"));
			InternalSpline.Dump();
		}

		return NewIndex;
    }

	virtual FInterval1f GetParameterSpace() const override
	{
		if (GetNumPoints() == 1)
		{
			return FInterval1f(0.f, 0.f);
		}
		
		return InternalSpline.GetParameterSpace();
	}

	int32 FindSegmentIndex(float Parameter, float &OutLocalParam) const
	{
		return InternalSpline.FindSegmentIndex(Parameter, OutLocalParam);
	}

    /**
	 * Updates tangents for all control points based on their tangent modes
	 */
	void UpdateTangents()
	{
	    const int32 NumPoints = GetNumPoints();
	    if (NumPoints < 2)
	    {
	        return;
	    }
		// Update tangents for each point according to its mode
		for (int32 i = 0; i < NumPoints; ++i)
		{
			UpdatePointTangents(i, TangentModes[i]);
		}
	}

	/**
	 * Updates tangents for a specific point based on its tangent mode
	 * @param Index Index of the point to update
	 * @param Mode Tangent mode to apply
	 */
	void UpdatePointTangents(int32 Index, ETangentMode Mode)
	{
		const int32 NumPoints = GetNumPoints();
		if (Index < 0 || Index >= NumPoints || Mode == ETangentMode::User || Mode == ETangentMode::Broken)
		{
			// Invalid index or user-defined mode, no action needed
			return;
		}
		

		// Get the position of the current point
		const ValueType& Current = GetValue(Index);

		// Get previous and next points (handling closed loop case)
		const ValueType& Prev = (Index > 0) ? GetValue(Index - 1) : (IsClosedLoop() ? GetValue(NumPoints - 1) : Current);

		const ValueType& Next = (Index < NumPoints - 1) ? GetValue(Index + 1) : (IsClosedLoop() ? GetValue(0) : Current);

		// Get actual parameter values for points
		float PrevParam = (Index > 0)
			                  ? static_cast<float>(Index - 1)
			                  : (IsClosedLoop() ? static_cast<float>(NumPoints - 1) : static_cast<float>(Index));

		float CurrentParam = static_cast<float>(Index);

		float NextParam = (Index < NumPoints - 1) ? static_cast<float>(Index + 1) : (IsClosedLoop() ? 0.0f : static_cast<float>(Index));

		// Adjust params for closed loop to ensure proper weighting
		if (IsClosedLoop() && Index == 0)
		{
			PrevParam -= static_cast<float>(NumPoints); // Make previous parameter negative for proper delta
		}
		else if (IsClosedLoop() && Index == NumPoints - 1)
		{
			NextParam += static_cast<float>(NumPoints); // Make next parameter greater than NumPoints for proper delta
		}

		// Calculate tangents based on mode
		ValueType InTangent = GetTangentIn(Index); // Keep existing if not changing
		ValueType OutTangent = GetTangentOut(Index);
		
		switch (Mode)
		{
		case ETangentMode::Auto:
		case ETangentMode::AutoClamped:
			{
				// Legacy-style auto tangent calculation with parameter weighting
				float PrevToNextParamDiff = FMath::Max<float>(UE_KINDA_SMALL_NUMBER, NextParam - PrevParam);

				// Calculate basic tangent - matches legacy formula
				ValueType AutoTangent = ((Next - Prev) / PrevToNextParamDiff) * (1.0f - Tension);

				// For AutoClamped mode, apply clamping like the legacy implementation
				if (Mode == ETangentMode::AutoClamped)
				{
					float PrevToCurParamDiff = FMath::Max<float>(UE_KINDA_SMALL_NUMBER, CurrentParam - PrevParam);
					float NextToCurParamDiff = FMath::Max<float>(UE_KINDA_SMALL_NUMBER, NextParam - CurrentParam);

					ValueType PrevToCurTangent = (Current - Prev) / PrevToCurParamDiff;
					ValueType NextToCurTangent = (Next - Current) / NextToCurParamDiff;

					float PrevToCurLength = static_cast<float>(Math::Size(PrevToCurTangent));
					float NextToCurLength = static_cast<float>(Math::Size(NextToCurTangent));

					// Clamp tangent magnitude
					float MaxScale = FMath::Min(PrevToCurLength, NextToCurLength);

					if (Math::Size(AutoTangent) > MaxScale && !FMath::IsNearlyZero(Math::Size(AutoTangent)))
					{
						AutoTangent = Math::GetSafeNormal(AutoTangent) * MaxScale;
					}
				}

				// Handle stationary endpoints
				if (bStationaryEndpoint && (Index == 0 || Index == NumPoints - 1) && !IsClosedLoop())
				{
					AutoTangent = ValueType();
				}

				InTangent = AutoTangent;
				OutTangent = AutoTangent;
			}
			break;

		case ETangentMode::Linear:
			{
				// Previous segment tangent (incoming)
				if (Index > 0 || IsClosedLoop())
				{
					InTangent = Current - Prev;
				}
				else
				{
					InTangent = ValueType();
				}

				// Next segment tangent (outgoing)
				if (Index < NumPoints - 1 || IsClosedLoop())
				{
					OutTangent = Next - Current;
				}
				else
				{
					OutTangent = ValueType();
				}
			}
			break;

		case ETangentMode::Constant:
			{
				// Zero tangents for constant interpolation
				InTangent = ValueType();
				OutTangent = ValueType();
			}
			break;

		case ETangentMode::User:
			break;
			
		case ETangentMode::Unknown:
		default:
			// Default to Auto mode if unknown
			{
				ValueType Tangent = (Next - Prev) * (1.0f - Tension);
				InTangent = Tangent;
				OutTangent = Tangent;
			}
			break;
		}

		// Apply the calculated tangents
		SetTangentIn(Index, InTangent);
		SetTangentOut(Index, OutTangent);

	}

	virtual float FindNearest(const ValueType& Point, float& OutSquaredDistance) const override
	{
        // Delegate to internal spline
        return InternalSpline.FindNearest(Point, OutSquaredDistance);
    }

	void Reparameterize(EParameterizationPolicy Policy = EParameterizationPolicy::Centripetal)
    {
	    InternalSpline.Reparameterize(Policy);
		
	    // Update tangents to maintain proper curve shape
	    UpdateTangents();
    }

	void SetKnotVector(const TArray<FKnot>& InKnots)
	{
		InternalSpline.SetKnotVector(InKnots);

		UpdateTangents();
	}
	
	const TArray<FKnot>& GetKnotVector() const
	{
		return InternalSpline.GetKnotVector();
	}
    
    /** Getter for internal spline */
    const FPolyBezierSpline3d& GetInternalSpline() const
    {
        return InternalSpline;
    }
	
	float GetTension() const
	{
		return Tension;
	}

	void SetTension(const float InTension)
	{
		this->Tension = InTension;
	}

	ETangentMode GetTangentMode(int32 Index) const
	{
		if (Index < 0 || Index >= GetNumPoints())
		{
			return ETangentMode::Unknown;
		}
		return TangentModes[Index];
	}

	void SetTangentModes(const TArray<ETangentMode>& InTangentModes)
	{
		TangentModes = InTangentModes;
	}
	
	const TArray<ETangentMode>& GetTangentModes() const
	{
		return TangentModes;
	}

	void SetStationaryEndpoints(bool bInStationaryEndpoints)
	{
		bStationaryEndpoint = bInStationaryEndpoints;
		UpdateTangents(); // Update tangents when this flag changes
	}

	bool IsStationaryEndpoints() const
	{
		return bStationaryEndpoint;
	}
	
	float FindNearestOnSegment(const ValueType& Point, int32 SegmentIndex, float& OutSquaredDistance) const
		{ return InternalSpline.FindNearestOnSegment(Point, SegmentIndex, OutSquaredDistance); }

protected:
	/** Updates a Bezier control point in the internal spline */
	void UpdateBezierControlPoint(int32 Index, const ValueType& NewValue)
	{
		if (Index < InternalSpline.NumKeys())
		{
			InternalSpline.SetValue(Index, NewValue);
		}
	}
private:

	static bool IsCurveKey(ETangentMode Mode)
	{
		return ((Mode == ETangentMode::Auto) || (Mode == ETangentMode::AutoClamped) || (Mode == ETangentMode::User) || (Mode == ETangentMode::Broken));
	}
	/**
	 * Helper method to add the first segment to the spline
	 * @param ControlPoint - Control point to insert
	 * @param OutPointIndex - Index of the newly created point
	 * @return true if the points were added, false otherwise
	 */
	bool InitializeFirstSegment(const FTangentBezierControlPoint& ControlPoint, int32& OutPointIndex)
    {
		const int32 NumPoints = GetNumPoints();
    
    	if (NumPoints == 0)
    	{
    		// First point - just store it directly with both tangents, these are not used until the first segment is completed
    		InternalSpline.AddValue(ControlPoint.Position);
    		InternalSpline.AddValue(ControlPoint.Position + (ControlPoint.TangentOut / 3.0f));
			InternalSpline.AddValue(ControlPoint.Position - (ControlPoint.TangentIn  / 3.0f));
    		
    		// update tangent mode
            if (TangentModes.IsEmpty())
            {
	            TangentModes.Add(ControlPoint.TangentMode);
            }
    		else
    		{
    			TangentModes[0] = ControlPoint.TangentMode;
    		}
    		
    		OutPointIndex = 0;
    		return true;
    	}
    	else if (NumPoints == 1)
    	{
    		constexpr bool bAppend = true;
    		if (bAppend)
    		{
				// We have exactly one point stored - this is our second point
				// Get the first point, then clear to start building segments
				
				// Calculate control points
				ValueType P0 = InternalSpline.GetValue(0);
				ValueType P1, P2;
				ValueType P3 = ControlPoint.Position;
				
				ValueType AutoTangent = (P3 - P0) * (1.0f - Tension);
				
				if (ControlPoint.TangentMode == ETangentMode::Auto ||
					ControlPoint.TangentMode == ETangentMode::AutoClamped)
				{
					P2 = P3 - AutoTangent / 3.0f;
				}
				else
				{
					// Use provided tangents
					P2 = P3 - ControlPoint.TangentIn / 3.0f;
				}
				
				if (IsAutoTangent(0))
				{
					P1 = P0 + AutoTangent / 3.0f;
				}
				else
				{
					// Values[1] is already the proper out tangent value (Values[2] is the in tangent we'd use if prepending instead of appending here)
					P1 = InternalSpline.GetValue(1);
				}
				
				// Add the first segment
				InternalSpline.SetControlPoints( {P0, P1, P2, P3}, EParameterizationPolicy::Centripetal);
				
				// update tangent mode
				TangentModes.AddDefaulted();
				SetPointTangentMode(1, ControlPoint.TangentMode);
				
				OutPointIndex = 1;
				return true;
    		}
    		else
    		{
    			// todo: prepend
    		}
    	}
    	
    	return false;
    }

protected:
	/* Tension parameter for auto-computed tangents [0,1] */
	float Tension = 0.0f;

	/* How tangents should be computed for each point */
	TArray<ETangentMode> TangentModes;

	bool bStationaryEndpoint = false;
};

using FTangentBezierSpline3f = TTangentBezierSpline<FVector3f>; 
using FTangentBezierSpline3d = TTangentBezierSpline<FVector3d>; 
} // end namespace UE::Geometry::Spline
} // end namespace UE::Geometry
} // end namespace UE