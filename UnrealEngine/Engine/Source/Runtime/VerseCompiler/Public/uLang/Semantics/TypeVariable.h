// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{
// Forward declarations.
class CDataDefinition;
class CExprDefinition;

class CTypeVariable : public CDefinition, public CNominalType
{
public:
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::TypeVariable;
    static const ETypeKind StaticTypeKind = ETypeKind::Variable;

    // A parameter `X` of type `type(A, B)` is encoded as
    // @code
    // :type(Y, Y) where Y:type(A, Z), Z:type(Y, B)
    // @endcode
    // with all uses resolving to `Z`.  Upon instantiation,
    // @code
    // :type(Y, Y)
    // @endcode
    // is rewritten to effectively
    // @code
    // :type(Y, Z)
    // @endcode
    // Any negative uses (outside of the negative use above) is replaced with
    // `Y`, while positive uses (again, outside of the above) are replaced with
    // `Z`. `_ExplicitParam` points to the corresponding data definition. of the
    // original explicit argument.
    const CDataDefinition* _ExplicitParam = nullptr;
    // `_NegativeTypeVariable` points to `Y` in the original encoding.
    // `_ExplicitParam->_ImplicitParam` can be used to access `Z`.
    const CTypeVariable* _NegativeTypeVariable = nullptr;

    CTypeVariable(const CSymbol& Name, const CTypeBase* NegativeType, const CTypeBase* PositiveType, CScope& EnclosingScope)
        : CDefinition{StaticDefinitionKind, EnclosingScope, Name}
        , CNominalType{StaticTypeKind, EnclosingScope.GetProgram()}
        , _NegativeType{NegativeType}
        , _PositiveType{PositiveType}
    {
    }

    const CTypeBase* GetNegativeType() const { return _NegativeType; }
    void SetNegativeType(const CTypeBase* NegativeType) { _NegativeType = NegativeType; }

    const CTypeBase* GetPositiveType() const { return _PositiveType; }
    void SetPositiveType(const CTypeBase* PositiveType) { _PositiveType = PositiveType; }

    virtual EComparability GetComparability() const override
    {
        const CTypeType* TypeType = GetPositiveType() ? GetPositiveType()->GetNormalType().AsNullable<CTypeType>() : nullptr;
        return TypeType ? TypeType->PositiveType()->GetNormalType().GetComparability() : EComparability::Incomparable;
    }

    // CNominalType interface.
    virtual const CDefinition* Definition() const override { return this; }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const override
    {
        return _ExplicitParam ? _ExplicitParam->_ImplicitParam->AsNameStringView() : AsNameStringView();
    }
    virtual bool CanBeCustomAccessorDataType() const override { return false; };

    // CDefinition interface.
    void SetAstNode(CExprDefinition* AstNode) { CDefinition::SetAstNode(AstNode); }
    CExprDefinition* GetAstNode() const { return static_cast<CExprDefinition*>(CDefinition::GetAstNode()); }

    void SetIrNode(CExprDefinition* AstNode) { CDefinition::SetIrNode(AstNode); }
    CExprDefinition* GetIrNode(bool bForce = false) const { return static_cast<CExprDefinition*>(CDefinition::GetIrNode(bForce)); }

    virtual bool IsPersistenceCompatConstraint() const override { return false; }

    virtual bool IsPersistable() const override { return false; }

    virtual bool IsExplicitlyCastable() const override
    {
        if (!_PositiveType)
        {
            return false;
        }
        const CTypeType* TypeType = _PositiveType->GetNormalType().AsNullable<CTypeType>();
        if (!TypeType)
        {
            return false;
        }
        if (TypeType->IsCastableSubtype())
        {
            return true;
        }
        return false;
    }

    virtual bool IsExplicitlyConcrete() const override
    {
        if (!_PositiveType)
        {
            return false;
        }
        const CTypeType* TypeType = _PositiveType->GetNormalType().AsNullable<CTypeType>();
        if (!TypeType)
        {
            return false;
        }
        if (TypeType->IsConcreteSubtype())
        {
            return true;
        }
        return false;
    }

private:
    const CTypeBase* _NegativeType = nullptr;
    const CTypeBase* _PositiveType = nullptr;
};
}
