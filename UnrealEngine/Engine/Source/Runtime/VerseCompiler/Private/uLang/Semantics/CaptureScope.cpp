// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/CaptureScope.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/SemanticFunction.h"

namespace uLang
{
void CCaptureScope::MaybeAddCapture(const CDefinition& Definition)
{
    const CCaptureScope* DefinitionCaptureScope = Definition._EnclosingScope.GetCaptureScope();
    if (!DefinitionCaptureScope)
    {
        return;
    }
    if (this == DefinitionCaptureScope)
    {
        return;
    }
    AddAncestorCapture(Definition, *DefinitionCaptureScope);
}

void CCaptureScope::AddAncestorCapture(const CDefinition& Definition, const CCaptureScope& DefinitionCaptureScope)
{
    auto J = this;
    CCaptureScope* I;
    for (;;)
    {
        I = J;
        J = J->GetParentCaptureScope();
        I->_bEmptyTransitiveCaptures = false;
        if (J == &DefinitionCaptureScope)
        {
            break;
        }
    }
    I->AddCapture(Definition);
}
}
