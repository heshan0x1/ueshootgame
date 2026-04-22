// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/PCGHLSLSyntaxTokenizer.h"

#if WITH_EDITOR

FPCGHLSLSyntaxTokenizer::FPCGHLSLSyntaxTokenizer(const FPCGSyntaxTokenizerParams& InParams)
	: FHlslSyntaxTokenizer()
{
	Keywords.Append(InParams.AdditionalKeywords);
}

#endif // WITH_EDITOR
