// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/AvailableAttributeUtils.h"

#include "uLang/Common/Common.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{
namespace
{
TOptional<uint64_t> GetIntegerDefinitionValue(TSPtr<CExprDefinition> ExprDefinition, const CSemanticProgram& SemanticProgram)
{
    const CNormalType& ArgType = ExprDefinition->GetResultType(SemanticProgram)->GetNormalType();
    if (const CIntType* IntArg = ArgType.AsNullable<CIntType>())
    {
        if (TSPtr<CExprInvokeType> ValueInvokeExpr = AsNullable<CExprInvokeType>(ExprDefinition->Value()))
        {
            if (TSPtr<CExprNumber> NumberExpr = AsNullable<CExprNumber>(ValueInvokeExpr->_Argument))
            {
                if (!NumberExpr->IsFloat())
                {
                    return static_cast<uint64_t>(NumberExpr->GetIntValue());
                }
            }
        }
    }

    return TOptional<uint64_t>();
}

CSymbol GetArgumentName(TSPtr<CExprDefinition> ExprDefinition)
{
    TSPtr<CExpressionBase> ElementExpr = ExprDefinition->Element();
    if (TSPtr<CExprIdentifierData> IdentifierData = AsNullable<CExprIdentifierData>(ElementExpr))
    {
        return IdentifierData->GetName();
    }

    return CSymbol();
}

} // namespace anonymous

TOptional<uint64_t> GetAvailableAttributeVersion(const SAttribute& AvailableAttribute, const CSemanticProgram& SemanticProgram)
{
    if (const CExprArchetypeInstantiation* AvailableArchInst = AsNullable<CExprArchetypeInstantiation>(AvailableAttribute._Expression))
    {
        for (TSRef<CExpressionBase> Argument : AvailableArchInst->Arguments())
        {
            if (TSPtr<CExprDefinition> ArgDefinition = AsNullable<CExprDefinition>(Argument))
            {
                if (GetArgumentName(ArgDefinition) == SemanticProgram._IntrinsicSymbols._MinUploadedAtFNVersion)
                {
                    return GetIntegerDefinitionValue(ArgDefinition, SemanticProgram);
                }
            }
        }
    }

    // @available attribute is malformed
    return TOptional<uint64_t>{};
}

TOptional<uint64_t> GetAvailableAttributeVersion(const CDefinition& Definition, const CSemanticProgram& SemanticProgram)
{
    ULANG_ASSERTF(SemanticProgram._availableClass, "Available class definition not found");

    if (const CClass* AvailableClass = SemanticProgram._availableClass)
    {
        if (TOptional<SAttribute> AvailableAttribute = Definition.FindAttribute(AvailableClass, SemanticProgram))
        {
            return GetAvailableAttributeVersion(*AvailableAttribute, SemanticProgram);
        }
    }

    // No @available attribute
    return TOptional<uint64_t>{};
}

// TODO: @available isn't applied to CModulePart correctly - CModuleParts cannot themselves hold attributes, so this snippet becomes a problem:
// 
// @available{ MinUploadedAtFNVersion: = 3000 }
// M<public>: = module {...}
//
// @available{ MinUploadedAtFNVersion: = 4000 }
// M<public>: = module {...}
//
// The first module-M gets an available version of 3000. The second @available attribute is processed, but isn't applied to the CModule type. 
// This kind of attribute should be held on the CModulePart instead. As of this writing, @available can be applied to module parts, but all 
// parts must have the same version.
//
// Combine the available-attribute version with any available-attributes found on the parent scopes.
// A likely case: 
//    @available{MinUploadedAtFNVersion:=3000} 
//    C := class { Value:int=42 }
// The combined available-version for Value is 3000 given it's parent context. This also applies if there
// are multiple versions at different containing scopes - the final applicable version is the most-restrictive one.
uint64_t CalculateCombinedAvailableAttributeVersion(const CDefinition& Definition, const CSemanticProgram& SemanticProgram)
{
    // have we already calculated the combined @available version for this definition?
    if (TOptional<uint64_t> CachedVersion = Definition.GetCombinedAvailableVersion())
    {
        return CachedVersion.GetValue();
    }

    // If not, get any direct @available attributes attached to this definition
    uint64_t CombinedResult = GetAvailableAttributeVersion(Definition, SemanticProgram).Get(0);

    // Now walk up the scopes hierarchy and combine the versions as we go. We'll also stash the combined
    // versions at each scope so later checks don't have to calculate it from nothing.
    const CScope* Scope = &Definition._EnclosingScope;
    while (Scope != nullptr)
    {
        if (const CDefinition* ScopeDefinition = Scope->ScopeAsDefinition())
        {
            uint64_t ScopeResult = CalculateCombinedAvailableAttributeVersion(*ScopeDefinition, SemanticProgram);
            CombinedResult = CMath::Max(CombinedResult, ScopeResult);

            // The recursion completed the walk for us. No need to continue.
            break;
        }
        Scope = Scope->GetParentScope();
    }

    // Whatever we found, we should cache it - even undefined
    Definition.SetCombinedAvailableVersion(CombinedResult);

    return CombinedResult;
}

bool IsDefinitionAvailableAtVersion(const CDefinition& Definition, uint64_t Version, const CSemanticProgram& SemanticProgram)
{
    uint64_t AttributeVersion = CalculateCombinedAvailableAttributeVersion(Definition, SemanticProgram);
    return AttributeVersion <= Version;
}

} // namespace uLang
