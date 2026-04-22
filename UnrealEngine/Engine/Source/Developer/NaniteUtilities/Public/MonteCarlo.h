// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorUtil.h"

FORCEINLINE FVector2f UniformSampleDisk( FVector2f E )
{
	float Radius = FMath::Sqrt( E.X );

	float Theta = 2.0f * PI * E.Y;
	float SinTheta, CosTheta;
	FMath::SinCos( &SinTheta, &CosTheta, Theta );
	return Radius * FVector2f( CosTheta, SinTheta );
}

FORCEINLINE FVector3f UniformSampleSphere( FVector2f E )
{
	float CosPhi = 1.0f - 2.0f * E.X;
	float SinPhi = FMath::Sqrt( 1.0f - CosPhi * CosPhi );

	float Theta = 2.0f * PI * E.Y;
	float SinTheta, CosTheta;
	FMath::SinCos( &SinTheta, &CosTheta, Theta );

	return FVector3f(
		SinPhi * CosTheta,
		SinPhi * SinTheta,
		CosPhi );
}

// exp( -0.5 * x^2 / Sigma^2 )
FORCEINLINE FVector2f GaussianSampleDisk( FVector2f E, float Sigma, float Window )
{
	// Scale distribution to set non-unit variance
	// Variance = Sigma^2

	// Window to [-Window, Window] output
	// Without windowing we could generate samples far away on the infinite tails.
	float InWindow = FMath::Exp( -0.5f * FMath::Square( Window / Sigma ) );
					
	// Box-Muller transform
	float Radius = Sigma * FMath::Sqrt( -2.0f * FMath::Loge( (1.0f - E.X) * InWindow + E.X ) );

	float Theta = 2.0f * PI * E.Y;
	float SinTheta, CosTheta;
	FMath::SinCos( &SinTheta, &CosTheta, Theta );
	return Radius * FVector2f( CosTheta, SinTheta );
}

// All Sobol code adapted from PathTracingRandomSequence.ush
FORCEINLINE uint32 EvolveSobolSeed( uint32& Seed )
{
	// constant from: https://www.pcg-random.org/posts/does-it-beat-the-minimal-standard.html
	const uint32 MCG_C = 2739110765;
	Seed += MCG_C;

	// Generated using https://github.com/skeeto/hash-prospector
	// Estimated Bias ~583
	uint32 Hash = Seed;
	Hash *= 0x92955555u;
	Hash ^= Hash >> 15;
	return Hash;
}

FORCEINLINE FVector4f LatticeSampler( uint32 SampleIndex, uint32& Seed )
{
	// Same as FastOwenScrambling, but without the final reversebits
	uint32 LatticeIndex = SampleIndex + EvolveSobolSeed( Seed );
	LatticeIndex ^= LatticeIndex * 0x9c117646u;
	LatticeIndex ^= LatticeIndex * 0xe0705d72u;

	// Lattice parameters taken from:
	// Weighted compound integration rules with higher order convergence for all N
	// Fred J. Hickernell, Peter Kritzer, Frances Y. Kuo, Dirk Nuyens
	// Numerical Algorithms - February 2012
	FUintVector4 Result = LatticeIndex * FUintVector4( 1, 364981, 245389, 97823 );

	return (Result >> 8) * 5.96046447754e-08f; // * 2^-24
}

FORCEINLINE uint32 FastOwenScrambling( uint32 Index, uint32 Seed )
{
	// Laine and Karras / Stratified Sampling for Stochastic Transparency / EGSR 2011
	Index += Seed; // randomize the index by our seed (pushes bits toward the left)
	Index ^= Index * 0x9c117646u;
	Index ^= Index * 0xe0705d72u;
	return ReverseBits( Index );
}

FORCEINLINE FVector2f SobolSampler( uint32 SampleIndex, uint32& Seed )
{
	// first scramble the index to decorelate from other 4-tuples
	uint32 SobolIndex = FastOwenScrambling( SampleIndex, EvolveSobolSeed( Seed ) );
	// now get Sobol' point from this index
	FUintVector2 Result( SobolIndex );
	// y component can be computed without iteration
	// "An Implementation Algorithm of 2D Sobol Sequence Fast, Elegant, and Compact"
	// Abdalla Ahmed, EGSR 2024
	// See listing (19) in the paper
	// The code is different here because we want the output to be bit-reversed, but
	// the methodology is the same
	Result.Y ^=  Result.Y               >> 16;
	Result.Y ^= (Result.Y & 0xFF00FF00) >>  8;
	Result.Y ^= (Result.Y & 0xF0F0F0F0) >>  4;
	Result.Y ^= (Result.Y & 0xCCCCCCCC) >>  2;
	Result.Y ^= (Result.Y & 0xAAAAAAAA) >>  1;

	// finally scramble the points to avoid structured artifacts
	Result.X = FastOwenScrambling( Result.X, EvolveSobolSeed( Seed ) );
	Result.Y = FastOwenScrambling( Result.Y, EvolveSobolSeed( Seed ) );

	// output as float in [0,1) taking care not to skew the distribution
	// due to the non-uniform spacing of floats in this range
	return (Result >> 8) * 5.96046447754e-08f; // * 2^-24
}