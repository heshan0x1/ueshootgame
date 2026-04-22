// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "SplineMath.h"
#include "ParameterizedTypes.h"

namespace UE
{
namespace Math
{
	template<typename T>
	struct TQuat;
}
}

namespace UE
{
namespace Geometry
{
namespace Spline
{
	
// Trait to detect if type T supports math operations needed for interpolation.
template<typename T, typename = void>
struct TIsMathInterpolable : std::false_type {};

template<typename T>
struct TIsMathInterpolable<T, std::void_t<
	decltype(FMath::Lerp(std::declval<T>(), std::declval<T>(), 0.5f)),
	decltype(std::declval<T>() + std::declval<T>()),
	decltype(std::declval<T>() * std::declval<float>())
	>> : std::true_type {};
/**
 * Base spline interpolation policy that works across all spline types
 * Extends the existing parameter-based interpolation with window-based methods
 */
template<typename T>
class TSplineInterpolationPolicy
{
public:
	// Keep existing parameter-based interpolation (used by attribute channels)
	static T Interpolate(TArrayView<const T* const> Window, float Parameter)
	{
		if (Window.Num() < 2)
		{
			return Window.Num() == 1 ? *Window[0] : T();
		}
		if constexpr (TIsMathInterpolable<T>::value)
		{
			return FMath::Lerp(*Window[0], *Window[1], Parameter);
		}
		else
		{
			// Fallback for non-math types:
			return Parameter < 0.5f ? *Window[0] : *Window[1];
		}
	}

	// Add window-based interpolation for spline implementations
	static T InterpolateWithBasis(TArrayView<const T* const> Window, TArrayView<const float> Basis)
	{
		if constexpr (TIsMathInterpolable<T>::value)
		{
			T Result = T();  // Assumes T() gives a neutral value (e.g. zero vector)
			int32 Count = FMath::Min(Window.Num(), Basis.Num());
			for (int32 i = 0; i < Count; ++i)
			{
				// Assumes T supports addition and multiplication by float.
				Result = Result + (*Window[i] * Basis[i]);
			}
			return Result;
		}
		else
		{
			// Fallback: simply return the first element if available.
			return Window.Num() > 0 ? *Window[0] : T();
		}
	}

	// Template-based n-th derivative calculation
	template<int32 Order>
	static T EvaluateDerivative(TArrayView<const T* const> Window, float Parameter)
	{
		if (Order == 0)
		{
			return Interpolate(Window, Parameter);
		}
		if constexpr (TIsMathInterpolable<T>::value)
		{
			// For math types, use the generic derivative helper.
			return Math::TGenericDerivativeHelper<T, Order>::Compute(Window, Parameter);
		}
		else
		{
			// For non-math types, derivatives are not meaningful.
			// Return a default constructed value.
			return T();
		}
	}
};

// Specialization only for int32
template<>
class TSplineInterpolationPolicy<int32>
{
public:
	static int32 Interpolate(TArrayView<const int32* const> Window, float Parameter)
    {
        if (Window.Num() < 2)
        {
            return Window.Num() == 1 ? *Window[0] : 0;
        }
        
        // Handle boundary cases
        if (Parameter <= 0.0f) return *Window[0];
        if (Parameter >= 1.0f) return *Window[1];
        
        // For intermediate values, explicitly convert to avoid implicit conversion warnings
        const float Start = static_cast<float>(*Window[0]);
        const float End = static_cast<float>(*Window[1]);
        const float Result = Start + Parameter * (End - Start);
        
        // Round to nearest integer
        return FMath::RoundToInt(Result);
    }
    
    static int32 InterpolateWithBasis(TArrayView<const int32* const> Window, TArrayView<const float> Basis)
    {
        float Result = 0.0f;
        for (int32 i = 0; i < FMath::Min(Window.Num(), Basis.Num()); ++i)
        {
            Result += static_cast<float>(*Window[i]) * Basis[i];
        }
        return FMath::RoundToInt(Result);
    }

	// Template-based n-th derivative calculation
	template<int32 Order>
	static int32 EvaluateDerivative(TArrayView<const int32* const> Window, float Parameter)
	{
		if (Order == 0)
		{
			return Interpolate(Window, Parameter);
		}
		return Math::TGenericDerivativeHelper<int32, Order>::Compute(Window, Parameter);
	}
};
	
// FLinearColor interpolation policy
template<>
class TSplineInterpolationPolicy<FLinearColor>
{
public:
	static FLinearColor Interpolate(TArrayView<const FLinearColor* const> Window, float Parameter)
	{
		if (Window.Num() < 2)
		{
			return Window.Num() == 1 ? *Window[0] : FLinearColor();
		}

		// Convert to linear color for better interpolation
		return FLinearColor::LerpUsingHSV(*Window[0],*Window[1], Parameter).ToFColor(true);
	}

	static FLinearColor InterpolateWithBasis(TArrayView<const FLinearColor* const> Window, TArrayView<const float> Basis)
	{
		// Convert all colors to linear space first
		TArray<FLinearColor> LinearColors;
		LinearColors.Reserve(Window.Num());
		for (const FLinearColor* Color : Window)
		{
			LinearColors.Add(*Color);
		}

		// Perform weighted sum in linear space
		FLinearColor Result = FLinearColor::Black;
		for (int32 i = 0; i < FMath::Min(Window.Num(), Basis.Num()); ++i)
		{
			Result += LinearColors[i] * Basis[i];
		}

		// Convert back to FColor
		return Result;
	}

	// Template-based n-th derivative calculation
	template<int32 Order>
	static FLinearColor EvaluateDerivative(TArrayView<const FLinearColor* const> Window, float Parameter)
	{
		if (Order == 0)
		{
			return Interpolate(Window, Parameter);
		}
		return Math::TGenericDerivativeHelper<FLinearColor, Order>::Compute(Window, Parameter);
	}
};

// Partial specialization for any quaternion type (TQuat)
template <typename RealType>
class TSplineInterpolationPolicy<UE::Math::TQuat<RealType>>
{
	using TQuat = UE::Math::TQuat<RealType>;
public:
    // Parameter-based interpolation using Slerp
	static TQuat Interpolate(TArrayView<const TQuat* const> Window, float Parameter)
	{
		if (Window.Num() < 2)
		{
			return Window.Num() == 1 ? *Window[0] : TQuat::Identity;
		}
        return TQuat::Slerp(*Window[0], *Window[1], static_cast<RealType>(Parameter));
	}

	// Weighted quaternion interpolation 
	// based on eigenvector approach from Markley et al.,
	// which handles antipodal quaternions and degenerate cases effectively.
	static TQuat InterpolateWithBasis(TArrayView<const TQuat* const> Window, TArrayView<const float> Basis)
	{
		const int32 Count = FMath::Min(Window.Num(), Basis.Num());
		if (Count == 0)
		{
			return TQuat::Identity;
		}

		// Use first quaternion as reference for hemisphere checks
		const TQuat& ReferenceQuat = *Window[0];

        // Build 4x4 correlation matrix
		RealType M[4][4] = {};
        RealType TotalWeight = 0;

		for (int32 Index = 0; Index < Count; ++Index)
		{
            const RealType Weight = static_cast<RealType>(Basis[Index]);
            if (Weight > static_cast<RealType>(UE_KINDA_SMALL_NUMBER))
			{
				TQuat Quat = *Window[Index];

                // Ensure consistent hemisphere to avoid cancellation
				if ((Quat | ReferenceQuat) < static_cast<RealType>(0))
				{
					Quat *= static_cast<RealType>(-1);
				}

				// Accumulate outer product contribution
				const RealType Q[4] = {Quat.X, Quat.Y, Quat.Z, Quat.W};
				for (int32 Row = 0; Row < 4; ++Row)
				{
					for (int32 Col = 0; Col < 4; ++Col)
					{
						M[Row][Col] += Weight * Q[Row] * Q[Col];
					}
				}

				TotalWeight += Weight;
			}
		}

		if (FMath::IsNearlyZero(TotalWeight))
		{
			return TQuat::Identity;
		}

		// Precompute inverse for efficiency
		double InvTotalWeight = 1.0 / TotalWeight;

        // Normalize matrix for numerical stability
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				M[Row][Col] *= InvTotalWeight;
			}
		}

		// Find dominant eigenvector using power iteration
        RealType EigenVec[4] = {0, 0, 0, 1};
        RealType PrevEigenValue = 0;

		constexpr int32 MaxIterations = 32;
		for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
		{
			// Matrix-vector multiplication: v' = M*v
            RealType NewVec[4] = {0};
			for (int32 Row = 0; Row < 4; ++Row)
			{
				for (int32 Col = 0; Col < 4; ++Col)
				{
					NewVec[Row] += M[Row][Col] * EigenVec[Col];
				}
			}

            // Normalize vector
			RealType LengthSq = 
				NewVec[0] * NewVec[0] + 
				NewVec[1] * NewVec[1] +
				NewVec[2] * NewVec[2] + 
				NewVec[3] * NewVec[3];

			// Possible with certain opposing quaternion configurations
			if (FMath::IsNearlyZero(LengthSq))
			{
				return TQuat::Identity;
			}

			RealType InvLength = FMath::InvSqrt(LengthSq);
			for (int32 i = 0; i < 4; ++i)
			{
				EigenVec[i] = NewVec[i] * InvLength;
			}

			// Calculate Rayleigh quotient for convergence check
            RealType EigenValue = 0;
			for (int32 Row = 0; Row < 4; ++Row)
			{
				for (int32 Col = 0; Col < 4; ++Col)
				{
					EigenValue += EigenVec[Row] * M[Row][Col] * EigenVec[Col];
				}
			}

			// Check for sufficient convergence
			if (FMath::IsNearlyEqual(EigenValue, PrevEigenValue))
			{
				break;
			}

			PrevEigenValue = EigenValue;
		}

        // Convert eigenvector to TQuat
		TQuat ResultQuat(
            EigenVec[0],
            EigenVec[1],
            EigenVec[2],
            EigenVec[3]
		);

		ResultQuat.Normalize();
		return ResultQuat;
	}


	// Template-based n-th derivative calculation
	template<int32 Order>
	static TQuat EvaluateDerivative(TArrayView<const TQuat* const> Window, float Parameter)
	{
		if (Order == 0)
		{
			return Interpolate(Window, Parameter);
		}

		// First derivative - angular velocity
		if (Order == 1)
		{
            // Adaptive step size based on quaternion difference
            RealType base_h = static_cast<RealType>(0.01);
			TQuat Q0 = Interpolate(Window, Parameter);
            TQuat Q1base = Interpolate(Window, Parameter + static_cast<float>(base_h));
            RealType diff = Q0.AngularDistance(Q1base);
            RealType h = base_h * FMath::Clamp(
                diff, 
                static_cast<RealType>(0.001),
                static_cast<RealType>(0.1)
            );

            TQuat Q1 = Interpolate(Window, Parameter + static_cast<float>(h));
            return (Q1 * Q0.Inverse()).Log() * (static_cast<RealType>(1) / h);
		}

		// Second derivative - angular acceleration
		if (Order == 2)
		{
            RealType base_h = static_cast<RealType>(0.01);
            TQuat Q_m1 = Interpolate(Window, Parameter - static_cast<float>(base_h));
            TQuat Q_p1 = Interpolate(Window, Parameter + static_cast<float>(base_h));
            RealType diff = Q_m1.AngularDistance(Q_p1);
            RealType h = base_h * FMath::Clamp(
                diff / static_cast<RealType>(2),
                static_cast<RealType>(0.001),
                static_cast<RealType>(0.1)
            );

            TQuat V0 = EvaluateDerivative<1>(Window, Parameter - static_cast<float>(h));
            TQuat V1 = EvaluateDerivative<1>(Window, Parameter + static_cast<float>(h));
            return (V1 * V0.Inverse()).Log() * (static_cast<RealType>(1) / (static_cast<RealType>(2) * h));
		}

		// Higher orders generally not meaningful for rotations
		return TQuat::Identity;
	}
};
	
// FTransform interpolation policy
template<>
class TSplineInterpolationPolicy<FTransform>
{
public:
	static FTransform Interpolate(TArrayView<const FTransform* const> Window, float Parameter)
	{
		if (Window.Num() < 2)
		{
			return Window.Num() == 1 ? *Window[0] : FTransform::Identity;
		}

		const FTransform& A = *Window[0];
		const FTransform& B = *Window[1];

		FTransform Result;
		Result.SetLocation(FMath::Lerp(A.GetLocation(), B.GetLocation(), Parameter));
		Result.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Parameter));
		Result.SetScale3D(FMath::Lerp(A.GetScale3D(), B.GetScale3D(), Parameter));

		return Result;
	}

	static FTransform InterpolateWithBasis(TArrayView<const FTransform* const> Window, TArrayView<const float> Basis)
	{
		// Handle empty case
		if (Window.Num() == 0 || Basis.Num() == 0)
		{
			return FTransform::Identity;
		}

		// Interpolate components separately
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Scale = FVector(1.0f);

		float TotalRotWeight = 0.0f;

		// Weighted sum of locations
		for (int32 i = 0; i < FMath::Min(Window.Num(), Basis.Num()); ++i)
		{
			const float Weight = Basis[i];
			if (Weight > UE_SMALL_NUMBER)
			{
				Location += Window[i]->GetLocation() * Weight;
				Scale += Window[i]->GetScale3D() * Weight;

				// Handle rotation separately with proper quaternion blending
				const FQuat& Q = Window[i]->GetRotation();
				const double Dot = Rotation.W * Q.W + Rotation.X * Q.X + Rotation.Y * Q.Y + Rotation.Z * Q.Z;
				Rotation += (Dot >= 0.0f ? Q : -Q) * Weight;
				TotalRotWeight += Weight;
			}
		}

		// Normalize the quaternion if we accumulated any weights
		if (TotalRotWeight > UE_SMALL_NUMBER)
		{
			Rotation.Normalize();
		}

		return FTransform(Rotation, Location, Scale);
	}

	template<int32 Order>
	static FTransform EvaluateDerivative(TArrayView<const FTransform* const> Window, float Parameter)
	{
		if (Order == 0)
		{
			return Interpolate(Window, Parameter);
		}

		// Split into components
		TArray<FVector> Translations;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
        
		Translations.SetNum(Window.Num());
		Rotations.SetNum(Window.Num());
		Scales.SetNum(Window.Num());
        
		for (int32 i = 0; i < Window.Num(); ++i)
		{
			Translations[i] = Window[i]->GetTranslation();
			Rotations[i] = Window[i]->GetRotation();
			Scales[i] = Window[i]->GetScale3D();
		}

		// Calculate derivatives for each component
		TArrayView<const FVector> TransView(Translations.GetData(), Translations.Num());
		TArrayView<const FQuat> RotView(Rotations.GetData(), Rotations.Num());
		TArrayView<const FVector> ScaleView(Scales.GetData(), Scales.Num());

		FVector TransResult = TSplineInterpolationPolicy<FVector>::EvaluateDerivative<Order>(TransView, Parameter);
		FQuat RotResult = TSplineInterpolationPolicy<FQuat>::EvaluateDerivative<Order>(RotView, Parameter);
		FVector ScaleResult = TSplineInterpolationPolicy<FVector>::EvaluateDerivative<Order>(ScaleView, Parameter);

		return FTransform(RotResult, TransResult, ScaleResult);
	}
};


// Base derivative calculator
template <typename T, int32 Order>
struct TDerivativePolicy
{
	static T Compute(TArrayView<const T* const> Window, float Parameter)
	{
		return TSplineInterpolationPolicy<T>::template EvaluateDerivative<Order>(Window, Parameter);
	}
};
} // end namespace UE::Geometry::Spline
} // end namespace UE::Geometry
} // end namespace UE