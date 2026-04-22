// Copyright Epic Games, Inc. All Rights Reserved.

#include <stdint.h>

#include "uLang/SourceProject/IndexedSourceText.h"

namespace uLang
{

int64_t SIndexedSourceText::CalculateOffsetForLine(const int32_t TargetLine) const
{
    if (TargetLine < _LineIndexCache.Num())
    {
        return _LineIndexCache[TargetLine];
    }

    CUTF8StringView Text = _SourceText.ToStringView();

    // Get the highest known offset
    int32_t Line = _LineIndexCache.Num() - 1; // line-0 should always be present and always offset-0
    int64_t Offset = _LineIndexCache.Last();

    const UTF8Char* Ch = Text._Begin + Offset;

    while (Line < TargetLine && Ch < Text._End)
    {
        if (*Ch == '\n')
        {
            ++Ch;
            ++Line;
            ULANG_ENSURE(_LineIndexCache.Num() == Line);
            _LineIndexCache.Add(int64_t(Ch - Text._Begin));
        }
        else if (*Ch == '\r')
        {
            ++Ch;
            ++Line;
            if (*Ch == '\n')
            {
                ++Ch;
            }
            ULANG_ENSURE(_LineIndexCache.Num() == Line);
            _LineIndexCache.Add(int64_t(Ch - Text._Begin));
        }
        else
        {
            ++Ch;
        }
    }

    if (Ch >= Text._End)
    {
        return Text._End - Text._Begin;
    }

    return _LineIndexCache[TargetLine];
}

}