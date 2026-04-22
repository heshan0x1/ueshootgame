// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticClass.h"

namespace uLang
{
static bool IsIdentifierHack(const CExpressionBase& AttributeExpression, const CDefinition* Definition, const CSemanticProgram& Program)
{
    auto UnresolvedIdentifierMatches = [](const CExpressionBase& IdentifierExpression, const CDefinition* Definition)
        {
            const CExprIdentifierUnresolved& Identifier = static_cast<const CExprIdentifierUnresolved&>(IdentifierExpression);
            return
                !Identifier.Context() &&
                !Identifier.Qualifier() &&
                Identifier._Symbol == Definition->GetName();
        };

    if (AttributeExpression.GetNodeType() == EAstNodeType::Identifier_Unresolved)
    {
        return UnresolvedIdentifierMatches(AttributeExpression, Definition);
    }

    if (const CExprIdentifierClass* ClassIdentifier = AsNullable<CExprIdentifierClass>(&AttributeExpression))
    {
        return ClassIdentifier->GetClass(Program)->Definition() == Definition;
    }
    if (const CExprIdentifierFunction* FunctionIdentifier = AsNullable<CExprIdentifierFunction>(&AttributeExpression))
    {
        return &FunctionIdentifier->_Function == Definition;
    }
    if (const CExprArchetypeInstantiation* ArchetypeIdentifier = AsNullable<CExprArchetypeInstantiation>(&AttributeExpression))
    {
        return ArchetypeIdentifier->GetClassOrInterface(Program)->Definition() == Definition;
    }
    if (const CExprMacroCall* MacroCallIdentifier = AsNullable<CExprMacroCall>(&AttributeExpression))
    {
        // unnamed scoped{} attribute macros
        const CExpressionBase& NameExpr = *MacroCallIdentifier->Name();
        if (NameExpr.GetNodeType() == EAstNodeType::Identifier_Unresolved)
        {
            return UnresolvedIdentifierMatches(NameExpr, Definition);
        }
    }

    return false;
}
bool IsAttributeHack(const SAttribute& Attribute, const CClass* AttributeClass, const CSemanticProgram& Program)
{
    return IsIdentifierHack(*Attribute._Expression, AttributeClass->Definition(), Program);
}
bool IsAttributeHack(const SAttribute& Attribute, const CFunction* AttributeFunction, const CSemanticProgram& Program)
{
    if (Attribute._Expression->GetNodeType() == EAstNodeType::Invoke_Invocation)
    {
        const CExprInvocation& Invocation = static_cast<const CExprInvocation&>(*Attribute._Expression);
        return IsIdentifierHack(*Invocation.GetCallee(), AttributeFunction, Program);
    }
    return false;
}

TOptional<CUTF8String> SAttribute::GetTextValue() const
{
    if (_Expression->GetNodeType() == EAstNodeType::Invoke_Invocation)
    {
        const CExprInvocation& AttrInvocation = static_cast<const CExprInvocation&>(*_Expression);
        if (AttrInvocation.GetArgument()->GetNodeType() != EAstNodeType::Invoke_MakeTuple)
        {
            const CExpressionBase& Argument = *AttrInvocation.GetArgument();
            const EAstNodeType ArgumentNodeType = Argument.GetNodeType();
            if (ArgumentNodeType == EAstNodeType::Literal_String)
            {
                return CUTF8String(static_cast<const CExprString&>(Argument)._String);
            }
            else if (ArgumentNodeType == EAstNodeType::Invoke_Type)
            {
                const CExprInvokeType& InvokeType = static_cast<const CExprInvokeType&>(Argument);
                if (InvokeType._Argument->GetNodeType() == EAstNodeType::Literal_String)
                {
                    return CUTF8String(static_cast<const CExprString&>(*InvokeType._Argument)._String);
                }
            }
        }
    }
    return {};
}

bool CAttributable::HasAttributeClass(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return FindAttributeExpr(AttributeClass, Program) != nullptr;
}

bool CAttributable::HasAttributeSubclass(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return FindAttributeSubclassExpr(AttributeClass, Program) != nullptr;
}

int32_t CAttributable::GetAttributeClassCount(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    ULANG_ASSERTF(AttributeClass, "GetAttributeClassCount given a null attribute class");

    int32_t Count = 0;
    for (int32_t Index = 0; Index < _Attributes.Num(); ++Index)
    {
        if (IsAttributeClassImpl<EFindAttributeMode::Exact>(_Attributes[Index], AttributeClass, Program))
        {
            Count++;
        }
    }

    return Count;
}

TArray<const CExpressionBase*> CAttributable::GetAttributesWithAttribute(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<const CExpressionBase*> RetVal;

    ULANG_ASSERTF(AttributeClass, "GetAttributesWithAttribute given a null attribute class");
    for (const SAttribute& Attr : _Attributes)
    {
        const uLang::CExpressionBase* AttrExpression = Attr._Expression;
        if (AttrExpression->HasAttributeClass(AttributeClass, Program))
        {
            RetVal.Add(Attr._Expression);
        }
    }

    return RetVal;
}

template<CAttributable::EFindAttributeMode TMode>
bool CAttributable::IsAttributeClassImpl(const SAttribute& Attr, const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    if (const CTypeBase* ResultType = Attr._Expression->GetResultType(Program))
    {
        const CClass* ClassType = nullptr;

        // @HACK: SOL-972, need better (fuller) support for attribute functions/ctors
        if (const CTypeType* typeType = ResultType->GetNormalType().AsNullable<CTypeType>())
        {
            if (const CTypeBase* positiveType = typeType->PositiveType())
            {
                ClassType = positiveType->GetNormalType().AsNullable<CClass>();
            }
        }

        if (ClassType == nullptr)
        {
            ClassType = ResultType->GetNormalType().AsNullable<CClass>();
        }

        if (ClassType != nullptr)
        {
            if constexpr (TMode == EFindAttributeMode::Exact)
            {
                if (ClassType == AttributeClass)
                {
                    return true;
                }
            }
            else if constexpr (TMode == EFindAttributeMode::IncludeSubtypes)
            {
                if (ClassType->IsClass(*AttributeClass))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

template<CAttributable::EFindAttributeMode TMode>
TOptional<int32_t> CAttributable::FindAttributeImpl(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    ULANG_ASSERTF(AttributeClass, "FindAttributeImpl given a null attribute class");

    for (int32_t Index = 0; Index < _Attributes.Num(); ++Index)
    {
        if (IsAttributeClassImpl<TMode>(_Attributes[Index], AttributeClass, Program))
        {
            return Index;
        }
    }

    return {};
}

template<CAttributable::EFindAttributeMode TMode>
TArray<int32_t> CAttributable::FindAttributesImpl(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    ULANG_ASSERTF(AttributeClass, "FindAttributesImpl given a null attribute class");

    TArray<int32_t> RetVal;

    for (int32_t Index = 0; Index < _Attributes.Num(); ++Index)
    {
        if (IsAttributeClassImpl<TMode>(_Attributes[Index], AttributeClass, Program))
        {
            RetVal.Add(Index);
        }
    }

    return RetVal;
}

TOptional<SAttribute> CAttributable::FindAttribute(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TOptional<int32_t> Index = FindAttributeImpl<EFindAttributeMode::Exact>(AttributeClass, Program);
    return Index ? _Attributes[*Index] : TOptional<SAttribute>{ };
}

TArray<SAttribute> CAttributable::FindAttributes(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<int32_t> Indices = FindAttributesImpl<EFindAttributeMode::Exact>(AttributeClass, Program);

    TArray<SAttribute> RetVal;
    RetVal.Reserve(Indices.Num());

    for (int32_t Index : Indices)
    {
        RetVal.Add(_Attributes[Index]);
    }

    return RetVal;
}

const CExpressionBase* CAttributable::FindAttributeExpr(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TOptional<int32_t> Index = FindAttributeImpl<EFindAttributeMode::Exact>(AttributeClass, Program);
    return Index ? _Attributes[*Index]._Expression.Get() : nullptr;
}

const CExpressionBase* CAttributable::FindAttributeSubclassExpr(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TOptional<int32_t> Index = FindAttributeImpl<EFindAttributeMode::IncludeSubtypes>(AttributeClass, Program);
    return Index ? _Attributes[*Index]._Expression.Get() : nullptr;
}

const TArray<CExpressionBase*> CAttributable::FindAttributeExprs(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<int32_t> Indices = FindAttributesImpl<EFindAttributeMode::Exact>(AttributeClass, Program);

    TArray<CExpressionBase*> RetVal;
    RetVal.Reserve(Indices.Num());

    for (int32_t Index : Indices)
    {
        RetVal.Add(_Attributes[Index]._Expression);
    }

    return RetVal;
}

const TArray<CExpressionBase*> CAttributable::FindAttributeSubclassExprs(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    TArray<int32_t> Indices = FindAttributesImpl<EFindAttributeMode::IncludeSubtypes>(AttributeClass, Program);

    TArray<CExpressionBase*> RetVal;
    RetVal.Reserve(Indices.Num());

    for (int32_t Index : Indices)
    {
        RetVal.Add(_Attributes[Index]._Expression);
    }

    return RetVal;
}

void CAttributable::AddAttributeClass(const CClass* AttributeClass)
{
    SAttribute Attribute{TSRef<CExprIdentifierClass>::New(AttributeClass->GetTypeType()), SAttribute::EType::Specifier};
    _Attributes.Add(Move(Attribute));
}

void CAttributable::AddAttribute(SAttribute Attribute)
{
    _Attributes.Add(Move(Attribute));
}

void CAttributable::RemoveAttributeClass(const CClass* AttributeClass, const CSemanticProgram& Program)
{
    if (TOptional<int32_t> Index = FindAttributeImpl<EFindAttributeMode::IncludeSubtypes>(AttributeClass, Program))
    {
        _Attributes.RemoveAt(*Index);
    }
}

// @HACK: SOL-972, We need full proper support for compile-time evaluation of attribute types
TOptional<CUTF8String> CAttributable::GetAttributeTextValue(const TArray<SAttribute>& Attributes, const CClass* AttributeClass, const CSemanticProgram& Program)
{
    ULANG_ASSERTF(AttributeClass, "GetAttributeTextValue given a null attribute class");

    for (const SAttribute& Attr : Attributes)
    {
        const CExpressionBase* AttribExpr = Attr._Expression;

        if (AttribExpr->GetNodeType() == EAstNodeType::Invoke_Invocation)
        {
            const CExprInvocation& AttrInvocation = *static_cast<const CExprInvocation*>(AttribExpr);
            if (&AttrInvocation.GetResolvedCalleeType()->GetReturnType().GetNormalType() == AttributeClass)
            {
                if (AttrInvocation.GetArgument()->GetNodeType() != EAstNodeType::Invoke_MakeTuple)
                {
                    return Attr.GetTextValue();
                }
            }
        }
    }

    return {};
}

TOptional<CUTF8String> CAttributable::GetAttributeTextValue(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    return GetAttributeTextValue(_Attributes, AttributeClass, Program);
}

bool CAttributable::HasAttributeClassHack(const CClass* AttributeClass, const CSemanticProgram& Program) const
{
    auto Last = _Attributes.end();
    return FindAttributeHack(_Attributes.begin(), Last, AttributeClass, Program) != Last;
}

bool CAttributable::HasAttributeFunctionHack(const CFunction* AttributeFunction, const CSemanticProgram& Program) const
{
    auto Last = _Attributes.end();
    return FindAttributeHack(_Attributes.begin(), Last, AttributeFunction, Program) != Last;
}
}
