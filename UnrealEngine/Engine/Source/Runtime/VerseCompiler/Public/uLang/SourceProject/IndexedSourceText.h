// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/SourceProject/IFileSystem.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Containers/Array.h"

namespace uLang
{

struct SIndexedSourceText
{
    const CUTF8String _SourceText;
    TArray<int64_t>& _LineIndexCache;
    int64_t CalculateOffsetForLine(const int32_t TargetLine) const;
};

}