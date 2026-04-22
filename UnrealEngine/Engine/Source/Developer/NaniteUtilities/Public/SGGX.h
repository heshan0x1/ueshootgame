// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorUtil.h"
#include "MatrixUtil.h"

// [ Heitz et al. 2015, "The SGGX Microflake Distribution" ]

// Linearly filtering SGGX, which is the same as using the covariance matrix, directly is a decent fit.
// Reprojecting area to eigenvectors can be better but requires a second pass.

template< typename T >
struct TSGGX
{
	template< typename U > using TVector2 = UE::Math::TVector2<U>;
	template< typename U > using TVector  = UE::Math::TVector<U>;

	T	xx;
	T	yy;
	T	zz;
		
	T	xy;
	T	xz;
	T	yz;

			TSGGX() = default;
			TSGGX( const TVector<T>& Normal );

	TSGGX&	operator+=( const TSGGX& Other );
	TSGGX&	operator+=( const TVector<T>& Normal );

	TSGGX&	operator*=( T Scalar );
	TSGGX&	operator/=( T Scalar );

	TSGGX	operator+( const TSGGX& Other ) const;
	TSGGX	operator*( T Scalar ) const;

	T		ProjectedArea( const TVector<T>& Direction ) const;
	T		SurfaceArea() const;

	void	EigenSolve( TVector<T>& Values, TVector<T> Vectors[3] ) const;
	void	FitIsotropic( TVector<T>& Center, TVector2<T>& Alpha ) const;
	
	static T	SurfaceArea( const TVector<T>& EigenValues );
};

template< typename T >
FORCEINLINE TSGGX<T>::TSGGX( const TVector<T>& Normal )
{
	// n n^T
	xx = Normal.X * Normal.X;
	yy = Normal.Y * Normal.Y;
	zz = Normal.Z * Normal.Z;
		 
	xy = Normal.X * Normal.Y;
	xz = Normal.X * Normal.Z;
	yz = Normal.Y * Normal.Z;
}

template< typename T >
FORCEINLINE TSGGX<T>& TSGGX<T>::operator+=( const TSGGX<T>& Other )
{
	xx += Other.xx;
	yy += Other.yy;
	zz += Other.zz;
	            
	xy += Other.xy;
	xz += Other.xz;
	yz += Other.yz;
		
	return *this;
}

template< typename T >
FORCEINLINE TSGGX<T>& TSGGX<T>::operator+=( const TVector<T>& Normal )
{
	return *this += TSGGX( Normal );
}

template< typename T >
FORCEINLINE TSGGX<T>& TSGGX<T>::operator*=( T Scalar )
{
	xx *= Scalar;
	yy *= Scalar;
	zz *= Scalar;
	
	xy *= Scalar;
	xz *= Scalar;
	yz *= Scalar;
		
	return *this;
}

template< typename T >
FORCEINLINE TSGGX<T>& TSGGX<T>::operator/=( T Scalar )
{
	xx /= Scalar;
	yy /= Scalar;
	zz /= Scalar;
	
	xy /= Scalar;
	xz /= Scalar;
	yz /= Scalar;
		
	return *this;
}

template< typename T >
FORCEINLINE TSGGX<T> TSGGX<T>::operator+( const TSGGX<T>& Other ) const
{
	return TSGGX<T>( *this ) += Other;
}

template< typename T >
FORCEINLINE TSGGX<T> TSGGX<T>::operator*( T Scalar ) const
{
	return TSGGX<T>( *this ) *= Scalar;
}

template< typename T >
FORCEINLINE TSGGX<T> operator*( T Scalar, const TSGGX<T>& SGGX )
{
	return SGGX * Scalar;
}

template< typename T >
FORCEINLINE T TSGGX<T>::ProjectedArea( const TVector<T>& Direction ) const
{
	// Projected area
	// alpha = sqrt( w^T S w )
	// alpha = sqrt( w^T n n^T w )
	// alpha = abs( dot( n, w ) )

	T x		= Direction | TVector<T>( xx, xy, xz );
	T y		= Direction | TVector<T>( xy, yy, yz );
	T z		= Direction | TVector<T>( xz, yz, zz );
	T wSw	= Direction | TVector<T>( x, y, z );
	return FMath::Sqrt( FMath::Max( 0.0f, wSw ) );
}

template< typename T >
void TSGGX<T>::EigenSolve( TVector<T>& Values, TVector<T> Vectors[3] ) const
{
	// Diagonalize matrix
	// A = V S V^T
	T A[] =
	{
		xx, xy, xz,
		xy, yy, yz,
		xz, yz, zz
	};
	T S[3];
	T V[9];

	JacobiSVD::EigenSolver3( A, S, V, 1e-8f );

	for( uint32 i = 0; i < 3; i++ )
	{
		Values[i]		= S[i];
		Vectors[i][0]	= V[i+0];
		Vectors[i][1]	= V[i+3];
		Vectors[i][2]	= V[i+6];
	}
}

template< typename T >
void TSGGX<T>::FitIsotropic( TVector<T>& Center, TVector2<T>& Alpha ) const
{
	TVector<T> Values;
	TVector<T> Vectors[3];
	EigenSolve( Values, Vectors );

	T Scale[3];
	for( uint32 k = 0; k < 3; k++ )
		Scale[k] = FMath::Sqrt( FMath::Abs( Values[k] ) );	// Should be positive already

	T		MaxRatio = 0.0;
	int32	MaxIndex = 0;
	for( uint32 k = 0; k < 3; k++ )
	{
		const uint32 k0 = k;
		const uint32 k1 = (1 << k0) & 3;

		T Ratio = FMath::Min( Scale[k0], Scale[k1] )
				/ FMath::Max( Scale[k0], Scale[k1] );
		if( MaxRatio < Ratio )
		{
			MaxRatio = Ratio;
			MaxIndex = k;
		}
	}

	const uint32 k0 = MaxIndex;
	const uint32 k1 = (1 << k0) & 3;
	const uint32 k2 = (1 << k1) & 3;

	Center = Vectors[k2];

	Alpha[0] = 0.5f * ( Scale[k0] + Scale[k1] );
	Alpha[1] = Scale[k2];
}

// Approx surface area of ellipsoid
FORCEINLINE float SurfaceAreaEllipsoid( float a, float b, float c )
{
#if 1
	float a2 = a*a;
	float b2 = b*b;
	float c2 = c*c;

	//area ~= pi * ( (1-1/sqrt3) * (ab + ac + bc) + (1+1/sqrt3) * sqrt( a^2*b^2 + a^2*c^2 + b^2*c^2 ) )
	return	1.32779329f * ( a*b + a*c + b*c ) +
			4.95539202f * FMath::Sqrt( a2 * b2 + a2 * c2 + b2 * c2 );
#else
	const float p = 1.6f;	// or p=ln(3)/ln(2)
	float ap = FMath::Pow( a, p );
	float bp = FMath::Pow( b, p );
	float cp = FMath::Pow( c, p );
	return  (2.0f * PI) * FMath::Pow( ap * bp + ap * cp + bp * cp, 1.0f / p );
#endif
}

template< typename T >
T TSGGX<T>::SurfaceArea( const TVector<T>& Values )
{
	/*
	integral sqrt(wSw) = pi * SurfaceArea
	Average ProjectedArea() = 1/4 * SurfaceArea

	Ellipsoid scale
	a = ( S1 * S2 / S0 )^0.25 / sqrt(pi);
	b = ( S2 * S0 / S1 )^0.25 / sqrt(pi);
	c = ( S0 * S1 / S2 )^0.25 / sqrt(pi);

	area ~= pi * ( (1-1/sqrt3) * (ab + ac + bc) + (1+1/sqrt3) * sqrt( a^2*b^2 + a^2*c^2 + b^2*c^2 ) )
	*/
	const float sqrt3 = 1.73205080756887729353f;

	return	(1.0f - 1.0f / sqrt3) * ( FMath::Sqrt( Values[0] ) + FMath::Sqrt( Values[1] ) + FMath::Sqrt( Values[2] ) ) +
			(1.0f + 1.0f / sqrt3) * FMath::Sqrt( Values[0] + Values[1] + Values[2] );
}

template< typename T >
T TSGGX<T>::SurfaceArea() const
{
	TVector<T> Values;
	TVector<T> Vectors[3];
	EigenSolve( Values, Vectors );

	return SurfaceArea( Values );
}