// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/Map.h"

namespace uLang
{
class CDefinition;

class CCaptureScope
{
public:
    virtual ~CCaptureScope() = default;

    virtual CCaptureScope* GetParentCaptureScope() const = 0;

    bool HasEmptyTransitiveCaptures() const
    {
        return _bEmptyTransitiveCaptures;
    }

    uint32_t NumCaptures() const
    {
        return _Captures.Num();
    }

    TArray<const CDefinition*> GetCaptures() const
    {
        TArray<const CDefinition*> Captures(_Captures.Num());
        for (auto [Definition, Index] : _Captures)
        {
            Captures[Index] = Definition;
        }
        return Captures;
    }

    const uint32_t* GetCapture(const CDefinition& Definition) const
    {
        return _Captures.Find(&Definition);
    }

    /// Add a definition to the captures of the ancestor capture scope where
    /// the argument definition is free, but not free in the ancestor capture
    /// scopes's enclosing capture scope.
    void MaybeAddCapture(const CDefinition&);

    void AddAncestorCapture(const CDefinition& Definition, const CCaptureScope& DefinitionCaptureScope);

private:
    void AddCapture(const CDefinition& Definition)
    {
        _Captures.FindOrInsert(&Definition, _Captures.Num());
    }

    /// `true` if this capture scope or any enclosed capture scope references
    /// definitions free in this capture scope, i.e. if the corresponding
    /// function needs a valid scope.
    bool _bEmptyTransitiveCaptures{true};

    /// Definitions free in this capture scope and not free in the enclosing
    /// capture scope.
    TMap<const CDefinition*, uint32_t> _Captures;
};
}
