// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Semantics/CaptureScope.h"
#include "uLang/Semantics/ControlScope.h"

namespace uLang
{
class CCaptureControlScope : public CControlScope, public CCaptureScope
{
public:
    CCaptureControlScope(CScope* Parent, CSemanticProgram& Program)
        : CControlScope{Parent, Program}
    {
    }

    virtual const CCaptureScope* AsCaptureScopeNullable() const override { return this; }

    virtual CCaptureScope* GetParentCaptureScope() const override
    {
        return _Parent->GetCaptureScope();
    }
};
}
