// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

FORCEINLINE float InverseLerp(
	float y,
	float x0, float y0,
	float x1, float y1 )
{
	return ( x0 * (y1 - y) - x1 * (y0 - y) ) / ( y1 - y0 );
}

FORCEINLINE float InverseLerp(
	float y,
	float x0, float y0,
	float x1, float y1,
	float x2, float y2 )
{
	// Inverse quadratic interpolation
#if 0
	float a = (y0 - y) * (x1 - x0) * (y1 - y2);
	float b = (y1 - y) * (x1 - x2) * (x1 - x0) * (y2 - y0);
	float c = (y2 - y) * (x1 - x2) * (y0 - y1);
				
	return x1 + b / (a + c);
#else
	return
		(y - y1) * (y - y2) * x0 / ( (y0 - y1) * (y0 - y2) ) +
		(y - y2) * (y - y0) * x1 / ( (y1 - y2) * (y1 - y0) ) +
		(y - y0) * (y - y1) * x2 / ( (y2 - y0) * (y2 - y1) );
#endif
}

// Brent's method
template< typename FuncType >
float BrentRootFind(
	float y, float Tolerance,
	float xA, float yA,
	float xB, float yB,
	float xGuess, bool bInitialGuess,
	int32 MaxIter,
	FuncType&& Func )
{
	if( FMath::Abs( yA - y ) < FMath::Abs( yB - y ) )
	{
		Swap( xA, xB );
		Swap( yA, yB );
	}

	float xC = xA;
	float yC = yA;
	float xD = xA;

	bool bBisection = true;

	for( int32 i = 0; i < MaxIter; i++ )
	{
		if( FMath::Abs( xB - xA ) < SMALL_NUMBER ||
			FMath::Abs( yB - y ) <= Tolerance )
			break;

		if( yC != yA && yC != yB )
		{
			xGuess = InverseLerp(
				y,
				xA, yA,
				xB, yB,
				xC, yC );
		}
		else if( !bInitialGuess )
		{
			xGuess = InverseLerp(
				y,
				xA, yA,
				xB, yB );
		}
		bInitialGuess = false;

		if( bBisection )
		{
			bBisection =
				FMath::Abs( xGuess - xB ) >= 0.5f * FMath::Abs( xB - xC ) ||
				FMath::Abs( xB - xC ) < SMALL_NUMBER;
		}
		else
		{
			bBisection =
				FMath::Abs( xGuess - xB ) >= 0.5f * FMath::Abs( xC - xD ) ||
				FMath::Abs( xC - xD ) < SMALL_NUMBER;
		}

		// Outside of interval
		if( ( xGuess - ( 0.75f * xA + 0.25f * xB ) ) * ( xGuess - xB ) >= 0.0f )
			bBisection = true;

		if( bBisection )
			xGuess = 0.5f * ( xA + xB );

		float yGuess = Func( xGuess );

		xD = xC;
		xC = xB;
		yC = yB;

		if( ( yA - y ) * ( yGuess - y ) < 0.0f )
		{
			xB = xGuess;
			yB = yGuess;
		}
		else
		{
			xA = xGuess;
			yA = yGuess;
		}

		if( FMath::Abs( yA - y ) < FMath::Abs( yB - y ) )
		{
			Swap( xA, xB );
			Swap( yA, yB );
		}
	}

	return xB;
}


// Nelder-Mead optimization
// TODO This is only 2D, make any dimensions.
template< typename FuncType >
float DownhillSimplex( FVector2f& x, float xDelta, float Tolerance, int32 MaxIter, FuncType&& Func )
{
	// Simplex
	FVector2f	sX[3];
	float		sY[3];

	for( int32 i = 0; i < MaxIter; i++ )
	{
		// Sort
		if( sY[1] < sY[2] )
		{
			Swap( sX[1], sX[2] );
			Swap( sY[1], sY[2] );
		}
		if( sY[0] < sY[1] )
		{
			Swap( sX[0], sX[1] );
			Swap( sY[0], sY[1] );
		}

		if( 2.0f * FMath::Abs( sY[0] - sY[2] ) < ( FMath::Abs( sY[0] ) + FMath::Abs( sY[2] ) ) * Tolerance )
			break;

		FVector2f	ReflectedX = sX[0] + sX[1] - sX[2];
		float		ReflectedY = Func( ReflectedX );

		if( ReflectedY > sY[0] )
		{
			FVector2f	ExpandedX = 2.0f * ReflectedX - 0.5f * ( sX[0] + sX[1] );
			float		ExpandedY = Func( ExpandedX );

			if( ExpandedY > ReflectedY )
			{
				sX[2] = ExpandedX;
				sY[2] = ExpandedY;
			}
			else
			{
				sX[2] = ReflectedX;
				sY[2] = ReflectedY;
			}
		}
		else if( ReflectedY > sY[1] )
		{
			sX[2] = ReflectedX;
			sY[2] = ReflectedY;
		}
		else
		{
			FVector2f	ContractedX = 0.25f * ( sX[0] + sX[1] ) + 0.5f * sX[2];
			float		ContractedY = Func( ContractedX );

			if( ContractedY > sY[2] )
			{
				sX[2] = ContractedX;
				sY[2] = ContractedY;
			}
			else
			{
				// Shrink
				sX[1] = 0.5f * ( sX[0] + sX[1] );
				sX[2] = 0.5f * ( sX[0] + sX[2] );

				sY[1] = Func( sX[1] );
				sY[2] = Func( sX[2] );
			}
		}
	}

	x = sX[0];
	return sY[0];
}