// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/SemanticTypes.h"

#include "uLang/Common/Algo/AllOf.h"
#include "uLang/Common/Algo/AnyOf.h"
#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Algo/Contains.h"
#include "uLang/Common/Algo/FindIf.h"
#include "uLang/Common/Containers/Set.h"
#include "uLang/Common/Containers/ValueRange.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/UnknownType.h"

namespace uLang
{
const char* TypeKindAsCString(ETypeKind Type)
{
    switch (Type)
    {
#define VISIT_KIND(Name, CppType) case ETypeKind::Name: return #Name;
        VERSE_ENUM_SEMANTIC_TYPE_KINDS(VISIT_KIND)
#undef VISIT_KIND
    default:
        ULANG_UNREACHABLE();
    }
}

const char* GetConstraintTypeAsCString(ETypeConstraintFlags TypeConstraints, bool bIsSubtype)
{
    const bool bCastableSubtype = Enum_HasAnyFlags(TypeConstraints, ETypeConstraintFlags::Castable);
    const bool bConcreteSubtype = Enum_HasAnyFlags(TypeConstraints, ETypeConstraintFlags::Concrete);

    if (bIsSubtype)
    {
        return bCastableSubtype
            ? "castable_subtype"
            : bConcreteSubtype
                ? "concrete_subtype"
                : "subtype";
    }
    else
    {
        return bCastableSubtype
            ? "castable_type"
            : bConcreteSubtype
                ? "concrete_type"
                : "type";

    }
}

//=======================================================================================
// CNormalType
//=======================================================================================

SmallDefinitionArray CNormalType::FindInstanceMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage) const
{
    return FindInstanceMember(MemberName, Origin, Qualifier, ContextPackage, CScope::GenerateNewVisitStamp());
}

SmallDefinitionArray CNormalType::FindTypeMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier) const
{
    return FindTypeMember(MemberName, Origin, Qualifier, CScope::GenerateNewVisitStamp());
}

//=======================================================================================
// CNominalType
//=======================================================================================

CUTF8String CNominalType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    if (Flag == ETypeStringFlag::Qualified)
    {
        return GetQualifiedNameString(*Definition());
    }
    return Definition()->AsNameStringView();
}


//=======================================================================================
// CPointerType
//=======================================================================================

CPointerType::CPointerType(CSemanticProgram& Program, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
    : CInvariantValueType(ETypeKind::Pointer, Program, NegativeValueType, PositiveValueType)
{
}

//=======================================================================================
// CReferenceType
//=======================================================================================

CReferenceType::CReferenceType(CSemanticProgram& Program, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
    : CInvariantValueType(ETypeKind::Reference, Program, NegativeValueType, PositiveValueType)
{
}

//=======================================================================================
// COptionType
//=======================================================================================

COptionType::COptionType(CSemanticProgram& Program, const CTypeBase* ValueType)
    : CValueType(ETypeKind::Option, Program, ValueType)
{}


//=======================================================================================
// CTypeType
//=======================================================================================
CUTF8String CTypeType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    const CNormalType& NegativeType = _NegativeType->GetNormalType();
    const CNormalType& PositiveType = _PositiveType->GetNormalType();
    if (!bLinkable && &NegativeType == &PositiveType)
    {
        return NegativeType.AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, bLinkable, Flag).AsCString();
    }
    if (&NegativeType == &GetProgram()._falseType)
    {
        if (&PositiveType == &GetProgram()._anyType)
        {
            return "type";
        }

        ETypeConstraintFlags TypeConstraints = ETypeConstraintFlags::None;
        if (PositiveType.IsA<CCastableType>())
        {
            TypeConstraints |= ETypeConstraintFlags::Castable;
        }
        else if (PositiveType.IsA<CConcreteType>())
        {
            TypeConstraints |= ETypeConstraintFlags::Concrete;
        }

        const char* const KeywordString = GetConstraintTypeAsCString(TypeConstraints, true);
        return CUTF8String("%s(%s)", KeywordString, PositiveType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable, Flag).AsCString());
    }
    if (_PositiveType == &GetProgram()._anyType)
    {
        return CUTF8String("supertype(%s)", NegativeType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable, Flag).AsCString());
    }
    // There isn't a good single expression to represent this.
    return CUTF8String(
        "type(%s, %s)",
        NegativeType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable, Flag).AsCString(),
        PositiveType.AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable, Flag).AsCString());
}


//=======================================================================================
// CTupleType
//=======================================================================================

CUTF8String CTupleType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    CUTF8StringBuilder DestCode;
    DestCode.Append("tuple(");
    DestCode.Append(AsParamsCode(OuterPrecedence, VisitedFlowTypes, false, bLinkable, Flag));
    DestCode.Append(')');
    return DestCode.MoveToString();
}

CUTF8String CTupleType::AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon, ETypeStringFlag Flag) const
{
    return AsParamsCode(OuterPrecedence, VisitedFlowTypes, WithColon, false, Flag);
}

CUTF8String CTupleType::AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon, bool bLinkable, ETypeStringFlag Flag) const
{
    CUTF8StringBuilder DestCode;
    for (int32_t ElementIndex = 0; ElementIndex < _Elements.Num(); ++ElementIndex)
    {
        const CTypeBase* Element = _Elements[ElementIndex];
        if (WithColon)
        {
            DestCode.Append(':');
        }
        DestCode.Append(Element->AsCodeRecursive(ETypeSyntaxPrecedence::List, VisitedFlowTypes, bLinkable, Flag));
        if (ElementIndex + 1 < _Elements.Num())
        {
            DestCode.Append(',');
        }
    }
    return DestCode.MoveToString();
}

EComparability CTupleType::GetComparability() const
{
    // Use the comparability of the least comparable element of tuple.
    bool bAllDataMembersAreHashable = true;
    for (const CTypeBase* Element : _Elements)
    {
        switch (Element->GetNormalType().GetComparability())
        {
        case EComparability::Incomparable: return EComparability::Incomparable;
        case EComparability::Comparable: bAllDataMembersAreHashable = false; break;
        case EComparability::ComparableAndHashable: break;
        default: ULANG_UNREACHABLE();
        }
    }
    return bAllDataMembersAreHashable ? EComparability::ComparableAndHashable : EComparability::Comparable;
}

bool CTupleType::IsPersistable() const
{
    for (const CTypeBase* Element : _Elements)
    {
        if (!Element->GetNormalType().IsPersistable())
        {
            return false;
        }
    }
    return true;
}

CTupleType::ElementArray CTupleType::ElementsWithSortedNames() const
{
    ElementArray Elements = GetElements();
    Algo::Sort(TRangeView{Elements.begin() + GetFirstNamedIndex(), Elements.end()},
        [] (const CTypeBase* Type1, const CTypeBase* Type2)
        {
            const CNamedType* NamedType1 = Type1->GetNormalType().AsNullable<CNamedType>();
            const CNamedType* NamedType2 = Type2->GetNormalType().AsNullable<CNamedType>();
            if (NamedType1 && NamedType2)
            {
                return NamedType1->GetName() < NamedType2->GetName();
            }
            else // Something is not as expected, in all known cases a glitch has already been reported, try to limp along without crashing.
            {
                return NamedType1 < NamedType2;
            }
        });
    return Elements;
}

const CNamedType* CTupleType::FindNamedType(CSymbol Name) const
{
    for (int32_t I = GetFirstNamedIndex(); I < Num(); ++I)
    {
        const CNamedType& MaybeMatch = _Elements[I]->GetNormalType().AsChecked<CNamedType>();
        if (MaybeMatch.GetName() == Name)
        {
            return &MaybeMatch;
        }
    }
    return nullptr;
}

//=======================================================================================
// CFunctionType
//=======================================================================================

template <typename... ArgTypes>
static const CTypeBase* GetOrCreateParamTypeImpl(CSemanticProgram& Program, CTupleType::ElementArray&& ParamTypes, ArgTypes&&... Args)
{
    if (ParamTypes.Num() == 1)
    {
        return ParamTypes[0];
    }
    return &Program.GetOrCreateTupleType(Move(ParamTypes), uLang::ForwardArg<ArgTypes>(Args)...);
}

const CTypeBase* CFunctionType::GetOrCreateParamType(CSemanticProgram& Program, CTupleType::ElementArray&& ParamTypes)
{
    return GetOrCreateParamTypeImpl(Program, uLang::Move(ParamTypes));
}

const CTypeBase* CFunctionType::GetOrCreateParamType(CSemanticProgram& Program, CTupleType::ElementArray&& ParamTypes, int32_t FirstNamedIndex)
{
    return GetOrCreateParamTypeImpl(Program, uLang::Move(ParamTypes), FirstNamedIndex);
}

void CFunctionType::BuildTypeVariableCode(CUTF8StringBuilder& Builder, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    const char* TypeVariableSeparator = " where ";
    for (const CTypeVariable* TypeVariable : GetTypeVariables())
    {
        if (TypeVariable->_ExplicitParam && TypeVariable->_ExplicitParam->_ImplicitParam != TypeVariable)
        {
            continue;
        }
        Builder.Append(TypeVariableSeparator);
        TypeVariableSeparator = ",";
        Builder.Append(TypeVariable->AsCodeRecursive(ETypeSyntaxPrecedence::Min, VisitedFlowTypes, bLinkable, Flag).AsCString());
    }
}

void CFunctionType::BuildEffectAttributeCode(CUTF8StringBuilder& Builder) const
{
    if (TOptional<TArray<const CClass*>> EffectClasses = GetProgram().ConvertEffectSetToEffectClasses(_Effects, EffectSets::FunctionDefault))
    {
        for (const CClass* EffectClass : EffectClasses.GetValue())
        {
            Builder.Append('<');
            Builder.Append(EffectClass->AsCode());
            Builder.Append('>');
        }
    }
}

void CFunctionType::BuildParameterBlockCode(CUTF8StringBuilder& Builder, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    Builder.Append('(');

    const char* ParamSeparator = "";
    for (const CTypeBase* ParamType : GetParamTypes())
    {
        Builder.Append(ParamSeparator);
        ParamSeparator = ",";
        Builder.Append(':');
        Builder.Append(ParamType->AsCodeRecursive(ETypeSyntaxPrecedence::Definition, VisitedFlowTypes, false, Flag));
    }

    BuildTypeVariableCode(Builder, VisitedFlowTypes, bLinkable, Flag);

    Builder.Append(')');

    BuildEffectAttributeCode(Builder);
}

CUTF8String CFunctionType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    CUTF8StringBuilder DestCode;
    if (_TypeVariables.Num() || _Effects != EffectSets::FunctionDefault)
    {
        DestCode.Append("type{_");
        BuildParameterBlockCode(DestCode, VisitedFlowTypes, bLinkable, Flag);
        DestCode.Append(':');
        DestCode.Append(_ReturnType.AsCodeRecursive(ETypeSyntaxPrecedence::Definition, VisitedFlowTypes, bLinkable, Flag));
        DestCode.Append('}');
    }
    else
    {
        const bool bNeedsParentheses = OuterPrecedence >= ETypeSyntaxPrecedence::To;
        if (bNeedsParentheses)
        {
            DestCode.Append('(');
        }
        DestCode.Append(_ParamsType->AsCodeRecursive(ETypeSyntaxPrecedence::To, VisitedFlowTypes, bLinkable, Flag));
        DestCode.Append("->");
        DestCode.Append(_ReturnType.AsCodeRecursive(ETypeSyntaxPrecedence::To, VisitedFlowTypes, bLinkable, Flag));
        if (bNeedsParentheses)
        {
            DestCode.Append(')');
        }
    }

    return DestCode.MoveToString();
}

bool CFunctionType::CanBeCalledFromPredicts() const
{
    const SEffectSet Effects = GetEffects();
    return !Effects[EEffect::dictates];
}

//=======================================================================================
// CIntType
//=======================================================================================

CUTF8String CIntType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    if (GetMin().IsInfinity() && GetMax().IsInfinity())
    {
        return "int";
    }

    if (!IsInhabitable())
    {
        return "false";
    }

    CUTF8StringBuilder DestCode;
    if (GetMin() == GetMax())
    {
        ULANG_ASSERT(GetMin().IsFinite()); // There shouldn't be a way to get a CIntType where both sides are the same infinity.
        DestCode.AppendFormat("type{%ld}", GetMin().GetFiniteInt());
        return DestCode.MoveToString();
    }

    DestCode.Append("type{_X:int where ");
    const char* Separator = "";

    if (GetMin().IsFinite())
    {
        DestCode.AppendFormat("%ld <= _X", GetMin().GetFiniteInt());
        Separator = ", ";
    }

    if (GetMax().IsFinite())
    {
        DestCode.Append(Separator);
        DestCode.AppendFormat("_X <= %ld", GetMax().GetFiniteInt());
    }
    DestCode.Append("}");
    return DestCode.MoveToString();
}

//=======================================================================================
// CFloatType
//=======================================================================================

CUTF8String CFloatType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    if (GetMin() == -INFINITY && std::isnan(GetMax()))
    {
        return "float";
    }

    CUTF8StringBuilder DestCode;
    auto AppendFloat = [&DestCode](double Value)
    {
        if (Value == INFINITY)
        {
            DestCode.Append("Inf");
        }
        else if (Value == -INFINITY)
        {
            DestCode.Append("-Inf");
        }
        else if (std::isnan(Value))
        {
            DestCode.Append("NaN");
        }
        else
        {
            int Exponent;
            double Unused = std::frexp(Value, &Exponent);
            // Suppress unused result warning.
            (void)Unused;
            if (std::abs(Exponent) > 5)
            {
                DestCode.AppendFormat("%e", Value);
            }
            else
            {
                DestCode.AppendFormat("%f", Value);
            }
        }
    };

    if (GetMin() == GetMax() || std::isnan(GetMax()))
    {
        DestCode.Append("type{");
        AppendFloat(GetMin());
        DestCode.Append("}");
        return DestCode.MoveToString();
    }

    ULANG_ASSERTF(!std::isnan(GetMin()) && !std::isnan(GetMax()), "only the intrinsic float type / type{NaN} should contain nan");
    // Unlike with ints we always print the upper and lower bound this is because
    // 1) it's actually always possible to have an upper and lower bound in MaxVerse
    // 2) floats are not totally ordered and have unintuitive semantics for new programmers so both bounds might help more.
    DestCode.Append("type{_X:float where ");
    AppendFloat(GetMin());
    DestCode.Append(" <= _X, _X <= ");
    AppendFloat(GetMax());
    DestCode.Append("}");
    return DestCode.MoveToString();
}

ETypePolarity CFlowType::Polarity() const
{
    return _Polarity;
}

const CTypeBase* CFlowType::GetChild() const
{
    return _Child;
}

void CFlowType::SetChild(const CTypeBase* Child) const
{
    ULANG_ASSERT(!Child || !Child->AsFlowType())
    _Child = Child;
}

void CFlowType::AddFlowEdge(const CFlowType* FlowType) const
{
    if (_FlowEdges.Contains(FlowType))
    {
        return;
    }
    _FlowEdges.Insert(FlowType);
}

void CFlowType::EmptyFlowEdges() const
{
    for (const CFlowType* NegativeFlowType : _FlowEdges)
    {
        NegativeFlowType->_FlowEdges.Remove(this);
    }
    _FlowEdges.Empty();
}

namespace {
    void Merge(const CFlowType& Dest, const CFlowType& Src, ETypePolarity, const uint32_t UploadedAtFnVersion);

    void MergeChild(const CFlowType& Dest, const CTypeBase* Src, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
    {
        ULANG_ASSERTF(Dest.Polarity() == Polarity, "`Dest`'s polarity must match `Polarity`");
        const CTypeBase* DestChild = Dest.GetChild();
        const CTypeBase* NewType;
        switch (Polarity)
        {
        case ETypePolarity::Negative:
            NewType = SemanticTypeUtils::Meet(DestChild, Src, UploadedAtFnVersion);
            break;
        case ETypePolarity::Positive:
            NewType = SemanticTypeUtils::Join(DestChild, Src, UploadedAtFnVersion);
            break;
        default:
            ULANG_UNREACHABLE();
        }
        if (const CFlowType* NewSrc = NewType->AsFlowType())
        {
            Merge(Dest, *NewSrc, Polarity, UploadedAtFnVersion);
        }
        else
        {
            Dest.SetChild(NewType);
        }
    }

    void MergeFlowEdges(const CFlowType& Dest, const CFlowType& Src)
    {
        for (const CFlowType* FlowType : Src.FlowEdges())
        {
            Dest.AddFlowEdge(FlowType);
            FlowType->AddFlowEdge(&Dest);
        }
    }

    void Merge(const CFlowType& Dest, const CFlowType& Src, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
    {
        ULANG_ASSERTF(Dest.Polarity() == Polarity, "`Dest`'s polarity must match `Polarity`");
        ULANG_ASSERTF(Src.Polarity() == Polarity, "`Src`'s polarity must match `Polarity`");
        if (&Dest == &Src)
        {
            return;
        }
        MergeChild(Dest, Src.GetChild(), Polarity, UploadedAtFnVersion);
        MergeFlowEdges(Dest, Src);
    }

    void MergeNegativeChild(const CFlowType& Dest, const CTypeBase* Src, const uint32_t UploadedAtFnVersion)
    {
        MergeChild(Dest, Src, ETypePolarity::Negative, UploadedAtFnVersion);
    }

    void MergeNegative(const CFlowType& Dest, const CFlowType& Src, const uint32_t UploadedAtFnVersion)
    {
        Merge(Dest, Src, ETypePolarity::Negative, UploadedAtFnVersion);
    }

    void MergePositiveChild(const CFlowType& Dest, const CTypeBase* Src, const uint32_t UploadedAtFnVersion)
    {
        MergeChild(Dest, Src, ETypePolarity::Positive, UploadedAtFnVersion);
    }

    void MergePositive(const CFlowType& Dest, const CFlowType& Src, const uint32_t UploadedAtFnVersion)
    {
        Merge(Dest, Src, ETypePolarity::Positive, UploadedAtFnVersion);
    }
}

const CNormalType& CFlowType::GetNormalType() const
{
    return GetChild()->GetNormalType();
}

CUTF8String CFlowType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    // Guard against trying to print types that have cycles via flow types.
    if (VisitedFlowTypes.Contains(this))
    {
        ULANG_ASSERT(!bLinkable);
        return "...";
    }
    else
    {
        const int32_t Index = VisitedFlowTypes.Add(this);
        const CUTF8String Result = GetChild()->AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, bLinkable, Flag);
        ULANG_ASSERT(Index == VisitedFlowTypes.Num() - 1);
        VisitedFlowTypes.Pop();
        return Result;
    }
}

CUTF8String CNamedType::AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable, ETypeStringFlag Flag) const
{
    CUTF8StringBuilder Builder;
    bool bNeedsParentheses = OuterPrecedence >= ETypeSyntaxPrecedence::Definition;
    if (bNeedsParentheses)
    {
        Builder.Append('(');
    }
    Builder.Append('?')
           .Append(_Name.AsStringView())
           .Append(':')
           .Append(_ValueType->AsCodeRecursive(ETypeSyntaxPrecedence::Definition, VisitedFlowTypes, bLinkable, Flag));
    if (_HasValue)
    {
        Builder.Append(" = ...");
    }
    if (bNeedsParentheses)
    {
        Builder.Append(')');
    }
    return Builder.MoveToString();
}

const CTupleType& CNamedType::ToTupleType() const
{
    int32_t FirstNamedIndex = 0;
    return GetProgram().GetOrCreateTupleType({ this }, FirstNamedIndex);
}

//=======================================================================================
// SemanticTypeUtils
//=======================================================================================

const CClass* SemanticTypeUtils::AsSingleClass(const CNormalType& NegativeType, const CNormalType& PositiveType)
{
    const CClass* NegativeClass = NegativeType.AsNullable<CClass>();
    if (!NegativeClass || NegativeClass->_StructOrClass != EStructOrClass::Class)
    {
        return nullptr;
    }
    const CClass* PositiveClass = PositiveType.AsNullable<CClass>();
    if (!PositiveClass || PositiveClass->_StructOrClass != EStructOrClass::Class)
    {
        return nullptr;
    }
    if (NegativeClass != PositiveClass->_NegativeClass)
    {
        return nullptr;
    }
    return PositiveClass;
}

const CInterface* SemanticTypeUtils::AsSingleInterface(const CNormalType& NegativeType, const CNormalType& PositiveType)
{
    const CInterface* NegativeInterface = NegativeType.AsNullable<CInterface>();
    if (!NegativeInterface)
    {
        return nullptr;
    }
    const CInterface* PositiveInterface = PositiveType.AsNullable<CInterface>();
    if (!PositiveInterface)
    {
        return nullptr;
    }
    if (NegativeInterface != PositiveInterface->_NegativeInterface)
    {
        return nullptr;
    }
    return PositiveInterface;
}

static const CTypeBase* SubstituteMapType(const CMapType& MapType, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& InstTypeVariables)
{
    CSemanticProgram& Program = MapType.GetProgram();
    const CTypeBase* KeyType = MapType.GetKeyType();
    const CTypeBase* ValueType = MapType.GetValueType();
    const CTypeBase* InstKeyType = SemanticTypeUtils::Substitute(*KeyType, Polarity, InstTypeVariables);
    const CTypeBase* InstValueType = SemanticTypeUtils::Substitute(*ValueType, Polarity, InstTypeVariables);
    if (KeyType == InstKeyType && ValueType == InstValueType)
    {
        return &MapType;
    }
    return &Program.GetOrCreateMapType(*InstKeyType, *InstValueType, MapType.IsWeak());
}

const CTypeBase* SemanticTypeUtils::Substitute(const CTypeBase& Type, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& InstTypeVariables)
{
    if (const CFlowType* FlowType = Type.AsFlowType())
    {
        const CTypeBase* Child = FlowType->GetChild();
        const CTypeBase* InstChild = Substitute(*Child, Polarity, InstTypeVariables);
        // Unchecked invariant: flow edges of generalized types point to dead
        // types and need not be instantiated.  This will cease to be true once
        // non-constructor closed-world functions are supported (the result type
        // of such a function may point to a negative type if the result is an
        // instantiated parametric function); or if the `type` macro is
        // supported with arbitrary values.  For example,
        // @code
        // Identity(X:t):t = X
        // F() := Identity
        // @endcode
        // or
        // @code
        // Identity(X:t):t = X
        // class1(t:type) := class:
        //     Property:t
        // MakeIdentityClass1<constructor>() := class1(type{Identity})
        //     Property := Identity
        // @endcode
        // Both of these cases can be handled if all live flow types (through
        // the type graph) are marked.  Flow edges pointing to live flow types
        // should be recreated in the instantiated type (and point to
        // instantiated flow types).  However, this will cease to work correctly
        // once nested closed-world functions are supported.  For example,
        // @code
        // Identity(X:t):t = X
        // F():int =
        //     G := Identity
        //     H() := G
        // @endcode
        return InstChild;
    }

    CSemanticProgram& Program = Type.GetProgram();
    const CNormalType& NormalType = Type.GetNormalType();
    switch (NormalType.GetKind())
    {
    case ETypeKind::Array:
    {
        const CArrayType& ArrayType = NormalType.AsChecked<CArrayType>();
        const CTypeBase* InstElementType = Substitute(*ArrayType.GetElementType(), Polarity, InstTypeVariables);
        return ArrayType.GetElementType() == InstElementType
            ? &ArrayType
            : &Program.GetOrCreateArrayType(InstElementType);
    }
    case ETypeKind::Generator:
    {
        const CGeneratorType& GeneratorType = NormalType.AsChecked<CGeneratorType>();
        const CTypeBase* InstElementType = Substitute(*GeneratorType.GetElementType(), Polarity, InstTypeVariables);
        return GeneratorType.GetElementType() == InstElementType
            ? &GeneratorType
            : &Program.GetOrCreateGeneratorType(InstElementType);
    }
    case ETypeKind::Map:
        return SubstituteMapType(NormalType.AsChecked<CMapType>(), Polarity, InstTypeVariables);
    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        const CTypeBase* NegativeValueType = PointerType.NegativeValueType();
        const CTypeBase* PositiveValueType = PointerType.PositiveValueType();
        const CTypeBase* InstNegativeValueType = Substitute(*NegativeValueType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstPositiveValueType = Substitute(*PositiveValueType, Polarity, InstTypeVariables);
        return NegativeValueType == InstNegativeValueType && PositiveValueType == InstPositiveValueType
            ? &PointerType
            : &Program.GetOrCreatePointerType(InstNegativeValueType, InstPositiveValueType);
    }
    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        const CTypeBase* NegativeValueType = ReferenceType.NegativeValueType();
        const CTypeBase* PositiveValueType = ReferenceType.PositiveValueType();
        const CTypeBase* InstNegativeValueType = Substitute(*NegativeValueType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstPositiveValueType = Substitute(*PositiveValueType, Polarity, InstTypeVariables);
        return NegativeValueType == InstNegativeValueType && PositiveValueType == InstPositiveValueType
            ? &ReferenceType
            : &Program.GetOrCreateReferenceType(InstNegativeValueType, InstPositiveValueType);
    }
    case ETypeKind::Option:
    {
        const COptionType& OptionType = NormalType.AsChecked<COptionType>();
        const CTypeBase* InstValueType = Substitute(*OptionType.GetValueType(), Polarity, InstTypeVariables);
        return OptionType.GetValueType() == InstValueType
            ? &OptionType
            : &Program.GetOrCreateOptionType(InstValueType);
    }
    case ETypeKind::Type:
    {
        const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
        const CTypeBase* NegativeType = TypeType.NegativeType();
        const CTypeBase* PositiveType = TypeType.PositiveType();
        const CTypeBase* InstNegativeType = Substitute(*NegativeType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstPositiveType = Substitute(*PositiveType, Polarity, InstTypeVariables);
        return NegativeType == InstNegativeType && PositiveType == InstPositiveType
            ? &TypeType
            : &Program.GetOrCreateTypeType(InstNegativeType, InstPositiveType);
    }
    case ETypeKind::Castable:
    {
        const CCastableType& CastableType = NormalType.AsChecked<CCastableType>();
        const CTypeBase& SuperType = CastableType.SuperType();
        const CTypeBase* InstSuperType = Substitute(SuperType, Polarity, InstTypeVariables);
        if (&SuperType == InstSuperType)
        {
            return &CastableType;
        }
        const CFlowType* InstSuperFlowType = InstSuperType->AsFlowType();
        if (!InstSuperFlowType)
        {
            return &Program.GetOrCreateCastableType(*InstSuperType);
        }
        if (InstSuperFlowType->GetChild()->GetNormalType().IsA<CCastableType>())
        {
            return InstSuperFlowType;
        }
        CFlowType& Result = Program.CreateFlowType(Polarity, &Program.GetOrCreateCastableType(*InstSuperFlowType->GetChild()));
        MergeFlowEdges(Result, *InstSuperFlowType);
        return &Result;
    }
    case ETypeKind::Concrete:
    {
        const CConcreteType& ConcreteType = NormalType.AsChecked<CConcreteType>();
        const CTypeBase& SuperType = ConcreteType.SuperType();
        const CTypeBase* InstSuperType = Substitute(SuperType, Polarity, InstTypeVariables);
        if (&SuperType == InstSuperType)
        {
            return &ConcreteType;
        }
        const CFlowType* InstSuperFlowType = InstSuperType->AsFlowType();
        if (!InstSuperFlowType)
        {
            return &Program.GetOrCreateConcreteType(*InstSuperType);
        }
        if (InstSuperFlowType->GetChild()->GetNormalType().IsA<CConcreteType>())
        {
            return InstSuperFlowType;
        }
        CFlowType& Result = Program.CreateFlowType(Polarity, &Program.GetOrCreateConcreteType(*InstSuperFlowType->GetChild()));
        MergeFlowEdges(Result, *InstSuperFlowType);
        return &Result;
    }
    case ETypeKind::Class:
        return &Program.CreateInstantiatedClass(
            NormalType.AsChecked<CClass>(),
            Polarity,
            InstTypeVariables);
    case ETypeKind::Interface:
        return &Program.CreateInstantiatedInterface(
            NormalType.AsChecked<CInterface>(),
            Polarity,
            InstTypeVariables);
    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        CTupleType::ElementArray InstantiatedElements;
        bool bInstantiated = false;
        for (const CTypeBase* Element : TupleType.GetElements())
        {
            const CTypeBase* InstElement = Substitute(*Element, Polarity, InstTypeVariables);
            InstantiatedElements.Add(InstElement);
            bInstantiated |= Element != InstElement;
        }
        return !bInstantiated
            ? &TupleType
            : &Program.GetOrCreateTupleType(Move(InstantiatedElements), TupleType.GetFirstNamedIndex());
    }
    case ETypeKind::Function:
    {
        const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
        const CTypeBase* ParamsType = &FunctionType.GetParamsType();
        const CTypeBase* ReturnType = &FunctionType.GetReturnType();
        const CTypeBase* InstParamsType = Substitute(*ParamsType, FlipPolarity(Polarity), InstTypeVariables);
        const CTypeBase* InstReturnType = Substitute(*ReturnType, Polarity, InstTypeVariables);
        // Note, the type variables' types may need to be instantiated if an
        // inner function type's type variables' types refer to an outer
        // function's now-instantiated type variables.  For example,
        // assuming `where` nests when inside a function type,
        // `type{_(:t, F(:u where u:subtype(t)):u where t:type)}`.
        // However, this requires higher rank types, which are currently
        // unimplemented.
        return ParamsType == InstParamsType && ReturnType == InstReturnType
            ? &FunctionType
            : &Program.GetOrCreateFunctionType(
                *InstParamsType,
                *InstReturnType,
                FunctionType.GetEffects(),
                FunctionType.GetTypeVariables(),
                FunctionType.ImplicitlySpecialized());
    }
    case ETypeKind::Variable:
    {
        const CTypeVariable* TypeVariable = &NormalType.AsChecked<CTypeVariable>();
        if (auto Last = InstTypeVariables.end(), I = FindIf(InstTypeVariables.begin(), Last, [=](const STypeVariableSubstitution& Arg) { return Arg._TypeVariable == TypeVariable; }); I != Last)
        {
            switch (Polarity)
            {
            case ETypePolarity::Negative: return I->_NegativeType;
            case ETypePolarity::Positive: return I->_PositiveType;
            default: ULANG_UNREACHABLE();
            }
        }
        return &NormalType;
    }
    case ETypeKind::Named:
    {
        const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
        const CTypeBase* InstValueType = Substitute(*NamedType.GetValueType(), Polarity, InstTypeVariables);
        return NamedType.GetValueType() == InstValueType
            ? &NamedType
            : &Program.GetOrCreateNamedType(
                NamedType.GetName(),
                InstValueType,
                NamedType.HasValue());
    }
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Persistable:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Module:
    case ETypeKind::Enumeration:
    default:
        return &NormalType;
    }
}

static TArray<STypeVariableSubstitution> Compose(TArray<STypeVariableSubstitution> First, TArray<STypeVariableSubstitution> Second)
{
    TArray<STypeVariableSubstitution> Result;
    for (const STypeVariableSubstitution& Substitution : First)
    {
        const CTypeBase* NegativeType = SemanticTypeUtils::Substitute(*Substitution._NegativeType, ETypePolarity::Negative, Second);
        const CTypeBase* PositiveType = SemanticTypeUtils::Substitute(*Substitution._PositiveType, ETypePolarity::Positive, Second);
        Result.Emplace(Substitution._TypeVariable, NegativeType, PositiveType);
    }
    return Result;
}

// See `CTypeVariable` and `AnalyzeParam` for an explanation of why this
// substitution is necessary.
static TArray<STypeVariableSubstitution> ExplicitTypeVariableSubsitutions(const TArray<const CTypeVariable*> TypeVariables)
{
    TArray<STypeVariableSubstitution> Result;
    Result.Reserve(TypeVariables.Num());
    for (const CTypeVariable* TypeVariable : TypeVariables)
    {
        const CTypeVariable* NegativeTypeVariable;
        const CTypeVariable* PositiveTypeVariable;
        if (TypeVariable->_ExplicitParam)
        {
            if (TypeVariable->_NegativeTypeVariable)
            {
                NegativeTypeVariable = TypeVariable->_NegativeTypeVariable;
            }
            else
            {
                NegativeTypeVariable = TypeVariable->_ExplicitParam->_ImplicitParam;
            }
        }
        else
        {
            NegativeTypeVariable = TypeVariable;
        }
        PositiveTypeVariable = TypeVariable;
        Result.Emplace(TypeVariable, NegativeTypeVariable, PositiveTypeVariable);
    }
    return Result;
}

static TArray<STypeVariableSubstitution> FlowTypeVariableSubsitutions(const TArray<const CTypeVariable*> TypeVariables, const uint32_t UploadedAtFnVersion)
{
    TArray<STypeVariableSubstitution> Result;
    Result.Reserve(TypeVariables.Num());
    for (const CTypeVariable* TypeVariable : TypeVariables)
    {
        CSemanticProgram& Program = TypeVariable->GetProgram();
        CFlowType& NegativeFlowType = Program.CreateNegativeFlowType();
        CFlowType& PositiveFlowType = Program.CreatePositiveFlowType();
        NegativeFlowType.AddFlowEdge(&PositiveFlowType);
        PositiveFlowType.AddFlowEdge(&NegativeFlowType);
        Result.Emplace(TypeVariable, &NegativeFlowType, &PositiveFlowType);
    }
    for (auto [TypeVariable, NegativeType, PositiveType] : Result)
    {
        auto NegativeFlowType = NegativeType->AsFlowType();
        ULANG_ASSERT(NegativeFlowType);
        auto PositiveFlowType = PositiveType->AsFlowType();
        ULANG_ASSERT(PositiveFlowType);

        const CTypeType* NegativeTypeType = TypeVariable->GetNegativeType()->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType)
        {
            continue;
        }

        const CTypeBase* NegativeChildType = NegativeTypeType->PositiveType();
        const CTypeBase* InstNegativeType = SemanticTypeUtils::Substitute(
            *NegativeChildType,
            ETypePolarity::Negative,
            Result);
        if (const CFlowType* InstNegativeFlowType = InstNegativeType->AsFlowType())
        {
            // Maintain invariant that a `CFlowType`'s child is not a `CFlowType`.
            Merge(*NegativeFlowType, *InstNegativeFlowType, ETypePolarity::Negative, UploadedAtFnVersion);
        }
        else
        {
            NegativeFlowType->SetChild(InstNegativeType);
        }

        const CTypeBase* PositiveChildType = NegativeTypeType->NegativeType();
        const CTypeBase* InstPositiveType = SemanticTypeUtils::Substitute(
            *PositiveChildType,
            ETypePolarity::Positive,
            Result);
        if (const CFlowType* InstPositiveFlowType = InstPositiveType->AsFlowType())
        {
            // Maintain invariant that a `CFlowType`'s child is not a `CFlowType`.
            Merge(*PositiveFlowType, *InstPositiveFlowType, ETypePolarity::Positive, UploadedAtFnVersion);
        }
        else
        {
            PositiveFlowType->SetChild(InstPositiveType);
        }
    }
    return Result;
}

TArray<STypeVariableSubstitution> SemanticTypeUtils::Instantiate(const TArray<const CTypeVariable*>& TypeVariables, const uint32_t UploadedAtFnVersion)
{
    return Compose(ExplicitTypeVariableSubsitutions(TypeVariables), FlowTypeVariableSubsitutions(TypeVariables, UploadedAtFnVersion));
}

const CFunctionType* SemanticTypeUtils::Instantiate(const CFunctionType* FunctionType, const uint32_t UploadedAtFnVersion)
{
    if (!FunctionType)
    {
        return nullptr;
    }
    const TArray<const CTypeVariable*>& TypeVariables = FunctionType->GetTypeVariables();
    if (TypeVariables.IsEmpty())
    {
        return FunctionType;
    }
    const CTypeBase* ParamsType = &FunctionType->GetParamsType();
    const CTypeBase* ReturnType = &FunctionType->GetReturnType();
    TArray<STypeVariableSubstitution> InstTypeVariables = Instantiate(FunctionType->GetTypeVariables(), UploadedAtFnVersion);
    const CTypeBase* InstParamsType = Substitute(*ParamsType, ETypePolarity::Negative, InstTypeVariables);
    const CTypeBase* InstReturnType = Substitute(*ReturnType, ETypePolarity::Positive, InstTypeVariables);
    return ParamsType == InstParamsType && ReturnType == InstReturnType
        ? FunctionType
        : &FunctionType->GetProgram().GetOrCreateFunctionType(
            *InstParamsType,
            *InstReturnType,
            FunctionType->GetEffects(),
            {},
            FunctionType->ImplicitlySpecialized());
}

namespace {
    struct SInvariantType
    {
        const CTypeBase* _NegativeType;
        const CTypeBase* _PositiveType;
    };

    template <typename Function>
    TOptional<SInvariantType> TransformInvariant(const CTypeBase* NegativeType, const CTypeBase* PositiveType, Function F)
    {
        bool bChanged = false;
        if (const CTypeBase* NewNegativeType = uLang::Invoke(F, *NegativeType))
        {
            NegativeType = NewNegativeType;
            bChanged = true;
        }
        if (const CTypeBase* NewPositiveType = uLang::Invoke(F, *PositiveType))
        {
            PositiveType = NewPositiveType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return {};
        }
        return {{NegativeType, PositiveType}};
    }

    template <typename Function>
    const CTupleType* TransformTuple(const CTupleType& Type, Function F)
    {
        CTupleType::ElementArray Elements = Type.GetElements();
        bool bChanged = false;
        for (const CTypeBase*& Element : Elements)
        {
            if (const CTypeBase* NewElement = uLang::Invoke(F, *Element))
            {
                Element = NewElement;
                bChanged = true;
            }
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateTupleType(Move(Elements), Type.GetFirstNamedIndex());
    }

    template <typename Function>
    const CFunctionType* TransformFunction(const CFunctionType& Type, Function F)
    {
        bool bChanged = false;
        const CTypeBase* ParamsType = &Type.GetParamsType();
        if (const CTypeBase* NewParamsType = uLang::Invoke(F, *ParamsType))
        {
            ParamsType = NewParamsType;
            bChanged = true;
        }
        const CTypeBase* ReturnType = &Type.GetReturnType();
        if (const CTypeBase* NewReturnType = uLang::Invoke(F, *ReturnType))
        {
            ReturnType = NewReturnType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateFunctionType(
            *ParamsType,
            *ReturnType,
            Type.GetEffects(),
            Type.GetTypeVariables(),
            Type.ImplicitlySpecialized());
    }

    template <typename Function>
    const CTypeBase* Transform(const CTypeBase&, Function);

    template <typename Function>
    const CTypeBase* TransformMapType(const CMapType& MapType, Function F)
    {
        bool bChanged = false;
        const CTypeBase* KeyType = MapType.GetKeyType();
        if (const CTypeBase* NewKeyType = uLang::Invoke(F, *KeyType))
        {
            KeyType = NewKeyType;
            bChanged = true;
        }
        const CTypeBase* ValueType = MapType.GetValueType();
        if (const CTypeBase* NewValueType = uLang::Invoke(F, *ValueType))
        {
            ValueType = NewValueType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &MapType.GetProgram().GetOrCreateMapType(*KeyType, *ValueType, MapType.IsWeak());
    }

    template <typename Function>
    const CTypeBase* Transform(const CNormalType& Type, Function F)
    {
        switch (Type.GetKind())
        {
        case ETypeKind::Array:
        {
            const CArrayType& ArrayType = Type.AsChecked<CArrayType>();
            const CTypeBase* NewType = uLang::Invoke(F, *ArrayType.GetElementType());
            if (!NewType)
            {
                return nullptr;
            }
            return &ArrayType.GetProgram().GetOrCreateArrayType(NewType);
        }
        case ETypeKind::Generator:
        {
            const CGeneratorType& GeneratorType = Type.AsChecked<CGeneratorType>();
            const CTypeBase* NewType = uLang::Invoke(F, *GeneratorType.GetElementType());
            if (!NewType)
            {
                return nullptr;
            }
            return &GeneratorType.GetProgram().GetOrCreateGeneratorType(NewType);
        }
        case ETypeKind::Map:
            return TransformMapType(Type.AsChecked<CMapType>(), F);
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType = Type.AsChecked<CPointerType>();
            TOptional<SInvariantType> Result = TransformInvariant(
                PointerType.NegativeValueType(),
                PointerType.PositiveValueType(),
                F);
            if (!Result)
            {
                return nullptr;
            }
            return &PointerType.GetProgram().GetOrCreatePointerType(Result->_NegativeType, Result->_PositiveType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType = Type.AsChecked<CReferenceType>();
            TOptional<SInvariantType> Result = TransformInvariant(
                ReferenceType.NegativeValueType(),
                ReferenceType.PositiveValueType(),
                F);
            if (!Result)
            {
                return nullptr;
            }
            return &ReferenceType.GetProgram().GetOrCreateReferenceType(Result->_NegativeType, Result->_PositiveType);
        }
        case ETypeKind::Option:
        {
            const COptionType& OptionType = Type.AsChecked<COptionType>();
            const CTypeBase* NewValueType = uLang::Invoke(F, *OptionType.GetValueType());
            if (!NewValueType)
            {
                return nullptr;
            }
            return &OptionType.GetProgram().GetOrCreateOptionType(NewValueType);
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType = Type.AsChecked<CTypeType>();
            TOptional<SInvariantType> Result = TransformInvariant(
                TypeType.NegativeType(),
                TypeType.PositiveType(),
                F);
            if (!Result)
            {
                return nullptr;
            }
            return &TypeType.GetProgram().GetOrCreateTypeType(Result->_NegativeType, Result->_PositiveType);
        }
        case ETypeKind::Castable:
        {
            const CCastableType& CastableType = Type.AsChecked<CCastableType>();
            const CTypeBase* NewSuperType = uLang::Invoke(F, CastableType.SuperType());
            if (!NewSuperType)
            {
                return nullptr;
            }
            return &CastableType.GetProgram().GetOrCreateCastableType(*NewSuperType);
        }
        case ETypeKind::Concrete:
        {
            const CConcreteType& ConcreteType = Type.AsChecked<CConcreteType>();
            const CTypeBase* NewSuperType = uLang::Invoke(F, ConcreteType.SuperType());
            if (!NewSuperType)
            {
                return nullptr;
            }
            return &ConcreteType.GetProgram().GetOrCreateConcreteType(*NewSuperType);
        }
        case ETypeKind::Tuple:
            return TransformTuple(Type.AsChecked<CTupleType>(), F);
        case ETypeKind::Function:
            return TransformFunction(Type.AsChecked<CFunctionType>(), F);
        case ETypeKind::Named:
        {
            const CNamedType& NamedType = Type.AsChecked<CNamedType>();
            const CTypeBase* NewValueType = uLang::Invoke(F, *NamedType.GetValueType());
            if (!NewValueType)
            {
                return nullptr;
            }
            return &NamedType.GetProgram().GetOrCreateNamedType(
                NamedType.GetName(),
                NewValueType,
                NamedType.HasValue());
        }
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Class:
        case ETypeKind::Interface:
        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Logic:
        case ETypeKind::Int:
        case ETypeKind::Rational:
        case ETypeKind::Float:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
        case ETypeKind::Variable:
        default:
            return nullptr;
        }
    }

    template <typename Function>
    const CTypeBase* Transform(const CTypeBase& Type, Function F)
    {
        return Transform(Type.GetNormalType(), Move(F));
    }

    const CTypeBase* CanonicalizeImpl(const CTypeBase&);

    const CTypeType* CanonicalizeTypeTypeImpl(const CTypeType& Type)
    {
        bool bChanged = false;
        const CTypeBase* PositiveType = Type.PositiveType();
        ETypeConstraintFlags TypeConstraintFlags = ETypeConstraintFlags::None;
        const CNormalType& StrippedPositiveType = SemanticTypeUtils::StripVariableAndConstraints(PositiveType->GetNormalType(), TypeConstraintFlags);
        if (&StrippedPositiveType != PositiveType)
        {
            PositiveType = &StrippedPositiveType;
            bChanged = true;
        }
        if (const CTypeBase* CanonicalizedPositiveType = CanonicalizeImpl(*PositiveType))
        {
            PositiveType = CanonicalizedPositiveType;
            bChanged = true;
        }
        if (Enum_HasAnyFlags(TypeConstraintFlags, ETypeConstraintFlags::Castable))
        {
            PositiveType = &PositiveType->GetProgram().GetOrCreateCastableType(*PositiveType);
            bChanged = true;
        }
        if (Enum_HasAnyFlags(TypeConstraintFlags, ETypeConstraintFlags::Concrete))
        {
            PositiveType = &PositiveType->GetProgram().GetOrCreateConcreteType(*PositiveType);
            bChanged = true;
        }
        const CTypeBase* NegativeType = Type.NegativeType();
        if (const CTypeBase* CanonicalizedNegativeType = CanonicalizeImpl(*NegativeType))
        {
            NegativeType = CanonicalizedNegativeType;
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateTypeType(NegativeType, PositiveType);
    }

    const CFunctionType* CanonicalizeFunctionImpl(const CFunctionType& Type)
    {
        bool bChanged = false;
        const CTypeBase* ParamsType = &Type.GetParamsType();
        if (const CTypeBase* NewParamsType = CanonicalizeImpl(*ParamsType))
        {
            ParamsType = NewParamsType;
            bChanged = true;
        }
        const CTypeBase* ReturnType = &Type.GetReturnType();
        if (const CTypeBase* NewReturnType = CanonicalizeImpl(*ReturnType))
        {
            ReturnType = NewReturnType;
            bChanged = true;
        }
        if (!Type.GetTypeVariables().IsEmpty())
        {
            bChanged = true;
        }
        if (!bChanged)
        {
            return nullptr;
        }
        return &Type.GetProgram().GetOrCreateFunctionType(
            *ParamsType,
            *ReturnType,
            Type.GetEffects(),
            {},
            Type.ImplicitlySpecialized());
    }

    const CTypeBase* CanonicalizeImpl(const CTypeBase& Type)
    {
        if (const CFlowType* FlowType = Type.AsFlowType())
        {
            return &SemanticTypeUtils::Canonicalize(*FlowType->GetChild());
        }
        if (const CAliasType* AliasType = Type.AsAliasType())
        {
            const CTypeBase& AliasedType = *AliasType->GetAliasedType();
            const CTypeBase* CanonicalizedAliasedType = CanonicalizeImpl(AliasedType);
            return CanonicalizedAliasedType ? CanonicalizedAliasedType : &AliasedType;
        }
        const CNormalType& NormalType = Type.GetNormalType();
        if (const CTypeType* TypeType = NormalType.AsNullable<CTypeType>())
        {
            return CanonicalizeTypeTypeImpl(*TypeType);
        }
        if (const CFunctionType* FunctionType = NormalType.AsNullable<CFunctionType>())
        {
            return CanonicalizeFunctionImpl(*FunctionType);
        }
        if (const CTypeVariable* TypeVariable = NormalType.AsNullable<CTypeVariable>())
        {
            // Canonicalize a type variable by rewriting to the upper bound (the
            // lower bound will currently always be `false`).  This ensures
            // multiple uses of different type variables that are represented as
            // the same type (`any` or some other upper bound) have the same
            // representation.  Additionally, this ensures multiple type variables
            // with the same name and bound (and thus the same mangled name) do
            // not collide when generating `UStruct`s for tuples containing such
            // type variables.
            if (const CTypeType* TypeType = TypeVariable->GetPositiveType()->GetNormalType().AsNullable<CTypeType>())
            {
                return &SemanticTypeUtils::Canonicalize(*TypeType->PositiveType());
            }
            return &TypeVariable->GetProgram()._anyType;
        }
        if (NormalType.GetKind() == ETypeKind::Comparable)
        {
            return &NormalType.GetProgram()._anyType;
        }
        if (const CConcreteType* ConcreteType = NormalType.AsNullable<CConcreteType>())
        {
            return &SemanticTypeUtils::Canonicalize(ConcreteType->SuperType());
        }
        if (const CCastableType* CastableType = NormalType.AsNullable<CCastableType>())
        {
            return &SemanticTypeUtils::Canonicalize(CastableType->SuperType());
        }
        if (const CClass* Class = NormalType.AsNullable<CClass>())
        {
            if (Class->_GeneralizedClass == Class)
            {
                return nullptr;
            }
            return Class->_GeneralizedClass;
        }
        if (const CInterface* Interface = NormalType.AsNullable<CInterface>())
        {
            if (Interface->_GeneralizedInterface == Interface)
            {
                return nullptr;
            }
            return Interface->_GeneralizedInterface;
        }
        if (const CMapType* MapType = NormalType.AsNullable<CMapType>(); MapType && !MapType->IsWeak())
        {
            return &MapType->GetProgram().GetOrCreateMapType(
                SemanticTypeUtils::Canonicalize(*MapType->GetKeyType()),
                SemanticTypeUtils::Canonicalize(*MapType->GetValueType()),
                false);
        }
        return Transform(NormalType, CanonicalizeImpl);
    }
}

const CTypeBase& SemanticTypeUtils::Canonicalize(const CTypeBase& Type)
{
    const CTypeBase* NewType = CanonicalizeImpl(Type);
    return NewType? *NewType : Type;
}

const CTupleType& SemanticTypeUtils::Canonicalize(const CTupleType& Type)
{
    const CTupleType* NewType = TransformTuple(Type, CanonicalizeImpl);
    return NewType? *NewType : Type;
}

const CFunctionType& SemanticTypeUtils::Canonicalize(const CFunctionType& Type)
{
    const CFunctionType* NewType = CanonicalizeFunctionImpl(Type);
    return NewType? *NewType : Type;
}

namespace {
    const CClass* AsPolarityClassImpl(const CClass& Class, ETypePolarity DesiredPolarity)
    {
        switch (DesiredPolarity)
        {
        case ETypePolarity::Positive:
            if (Class._OwnedNegativeClass)
            {
                return nullptr;
            }
            return Class._NegativeClass;
        case ETypePolarity::Negative:
            if (Class._OwnedNegativeClass)
            {
                return Class._NegativeClass;
            }
            return nullptr;
        default:
            ULANG_UNREACHABLE();
        }
    }

    const CClass& AsPositiveClass(const CClass& Class)
    {
        const CClass* NewClass = AsPolarityClassImpl(Class, ETypePolarity::Positive);
        return NewClass? *NewClass : Class;
    }

    const CInterface* AsPolarityInterfaceImpl(const CInterface& Interface, ETypePolarity DesiredPolarity)
    {
        switch (DesiredPolarity)
        {
        case ETypePolarity::Positive:
            if (Interface._OwnedNegativeInterface)
            {
                return nullptr;
            }
            return Interface._NegativeInterface;
        case ETypePolarity::Negative:
            if (Interface._OwnedNegativeInterface)
            {
                return Interface._OwnedNegativeInterface;
            }
            return nullptr;
        default:
            ULANG_UNREACHABLE();
        }
    }

    const CInterface& AsPositiveInterface(const CInterface& Interface)
    {
        const CInterface* NewInterface = AsPolarityInterfaceImpl(Interface, ETypePolarity::Positive);
        return NewInterface? *NewInterface : Interface;
    }

    const CTypeBase* AsPolarityImpl(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions, ETypePolarity DesiredPolarity)
    {
        if (const CFlowType* FlowType = Type.AsFlowType())
        {
            for (auto [NegativeFlowType, PositiveFlowType] : Substitutions)
            {
                if (DesiredPolarity == ETypePolarity::Positive && FlowType == NegativeFlowType)
                {
                    return PositiveFlowType;
                }
                else if (DesiredPolarity == ETypePolarity::Negative && FlowType == PositiveFlowType)
                {
                    return NegativeFlowType;
                }
            }
        }
        const CNormalType& NormalType = Type.GetNormalType();
        if (const CClass* Class = NormalType.AsNullable<CClass>())
        {
            return AsPolarityClassImpl(*Class, DesiredPolarity);
        }
        if (const CInterface* Interface = NormalType.AsNullable<CInterface>())
        {
           return AsPolarityInterfaceImpl(*Interface, DesiredPolarity);
        }
        return Transform(Type, [&](const CTypeBase& ChildType)
        {
            return AsPolarityImpl(ChildType, Substitutions, DesiredPolarity);
        });
    }
}

const CTypeBase& SemanticTypeUtils::AsPolarity(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions, ETypePolarity DesiredPolarity)
{
    if (const CTypeBase* NewType = AsPolarityImpl(Type, Substitutions, DesiredPolarity))
    {
        return *NewType;
    }
    return Type;
}

const CTypeBase& SemanticTypeUtils::AsPositive(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions)
{
    return AsPolarity(Type, Substitutions, ETypePolarity::Positive);
}

const CTypeBase& SemanticTypeUtils::AsNegative(const CTypeBase& Type, const TArray<SInstantiatedTypeVariable>& Substitutions)
{
    return AsPolarity(Type, Substitutions, ETypePolarity::Negative);
}

namespace {
    using TInterfaceSet = TArrayG<const CInterface*, TInlineElementAllocator<8>>;

    // Utility functions for collecting all interfaces implemented by a class or interface (including the interface itself).
    // A set might be a better type for FoundInterfaces (but probably not if they are small).
    void CollectAllInterfaces(TInterfaceSet& FoundInterfaces, const CInterface* Interface)
    {
        if (!FoundInterfaces.Contains(Interface))
        {
            FoundInterfaces.Add(Interface);
            const TArray<CInterface*>& SuperInterfaces = Interface->_SuperInterfaces;
            for (const CInterface* SuperInterface : SuperInterfaces)
            {
                CollectAllInterfaces(FoundInterfaces, SuperInterface);
            }
        }
    }

    void CollectAllInterfaces(TInterfaceSet& FoundInterfaces, const CClass* Class, VisitStampType VisitStamp)
    {
        for (const CClass* SuperClass = Class;
            SuperClass;
            SuperClass = SuperClass->_Superclass)
        {
            if (!SuperClass->TryMarkVisited(VisitStamp))
            {
                break;
            }
            const TArray<CInterface*>& SuperInterfaces = SuperClass->_SuperInterfaces;
            for (const CInterface* SuperInterface : SuperInterfaces)
            {
                CollectAllInterfaces(FoundInterfaces, SuperInterface);
            }
        }
    }

    void CollectAllInterfaces(TInterfaceSet& FoundInterfaces, const CClass* Class)
    {
        CollectAllInterfaces(FoundInterfaces, Class, CScope::GenerateNewVisitStamp());
    }

    TArray<STypeVariableSubstitution> JoinTypeVariableSubstitutions(
        const TArray<STypeVariableSubstitution>& TypeVariables,
        const TArray<STypeVariableSubstitution>& InstantiatedTypeVariables1,
        const TArray<STypeVariableSubstitution>& InstantiatedTypeVariables2,
        const uint32_t UploadedAtFnVersion)
    {
        TArray<STypeVariableSubstitution> TypeVariableSubstitutions;
        using NumType = decltype(TypeVariables.Num());
        NumType NumInstantiatedTypeVariables = TypeVariables.Num();
        ULANG_ASSERT(NumInstantiatedTypeVariables == InstantiatedTypeVariables1.Num());
        ULANG_ASSERT(NumInstantiatedTypeVariables == InstantiatedTypeVariables2.Num());
        for (NumType J = 0; J != NumInstantiatedTypeVariables; ++J)
        {
            TypeVariableSubstitutions.Emplace(
                TypeVariables[J]._TypeVariable,
                SemanticTypeUtils::Meet(
                    InstantiatedTypeVariables1[J]._NegativeType,
                    InstantiatedTypeVariables2[J]._NegativeType,
                    UploadedAtFnVersion),
                SemanticTypeUtils::Join(
                    InstantiatedTypeVariables1[J]._PositiveType,
                    InstantiatedTypeVariables2[J]._PositiveType,
                    UploadedAtFnVersion));
        }
        return TypeVariableSubstitutions;
    }

    // Utility function that takes two containers with interfaces and returns container with the interfaces that are common to both.
    // If a interface is included in the result, then none of its super_interfaces are.
    TInterfaceSet FindCommonInterfaces(const TInterfaceSet& LhsInterfaces, const TInterfaceSet& RhsInterfaces, const uint32_t UploadedAtFnVersion)
    {
        TInterfaceSet CommonInterfaces;
        for (const CInterface* LhsInterface : LhsInterfaces)
        {
            const CInterface* GeneralizedInterface = LhsInterface->_GeneralizedInterface;
            for (const CInterface* RhsInterface : RhsInterfaces)
            {
                if (GeneralizedInterface != RhsInterface->_GeneralizedInterface)
                {
                    continue;
                }
                TArray<STypeVariableSubstitution> TypeVariableSubstitutions = JoinTypeVariableSubstitutions(
                    GeneralizedInterface->_TypeVariableSubstitutions,
                    LhsInterface->_TypeVariableSubstitutions,
                    RhsInterface->_TypeVariableSubstitutions,
                    UploadedAtFnVersion);
                const CInterface* Interface;
                if (auto InstantiatedInterface = InstantiateInterface(*GeneralizedInterface, ETypePolarity::Positive, TypeVariableSubstitutions))
                {
                    Interface = InstantiatedInterface;
                }
                else
                {
                    Interface = GeneralizedInterface;
                }
                if (CommonInterfaces.ContainsByPredicate([Interface, UploadedAtFnVersion](const CInterface* CommonInterface) { return SemanticTypeUtils::IsSubtype(CommonInterface, Interface, UploadedAtFnVersion); }))
                {
                    continue;
                }
                // Need to add, but first remove things implemented by the new interface
                using NumType = decltype(CommonInterfaces.Num());
                for (NumType I = 0, Last = CommonInterfaces.Num(); I != Last; ++I)
                {
                    if (SemanticTypeUtils::IsSubtype(Interface, CommonInterfaces[I], UploadedAtFnVersion))
                    {
                        CommonInterfaces.RemoveAtSwap(I);
                        --I;
                    }
                }
                CommonInterfaces.Add(Interface);
            }
        }
        return CommonInterfaces;
    }

    // A simple, O(n^2) check that two arrays contain the same elements in any order, assuming that each array contains a distinct element at most once.
    template<typename ElementType, typename AllocatorType>
    bool ArraysHaveSameElementsInAnyOrder(const TArrayG<ElementType, AllocatorType>& A, const TArrayG<ElementType, AllocatorType>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }

        for (const ElementType& Element : A)
        {
            if (!B.Contains(Element))
            {
                return false;
            }
        }

        return true;
    }

    /// Compute the join of a Interface and a Interface/Class: the "least" unique interface that is implemented by both the Interface and the Interface/Class.
    /// Return AnyType if no suitable unique interface is found.
    template<typename TClassOrInterface>
    const CTypeBase* JoinInterfaces(const CInterface* Interface, const TClassOrInterface* ClassOrInterface, const uint32_t UploadedAtFnVersion)
    {
        TInterfaceSet Interfaces1;
        CollectAllInterfaces(Interfaces1, Interface);
        TInterfaceSet Interfaces2;
        CollectAllInterfaces(Interfaces2, ClassOrInterface);
        TInterfaceSet Common = FindCommonInterfaces(Interfaces1, Interfaces2, UploadedAtFnVersion);
        if (1 == Common.Num())
        {
            return Common[0];
        }
        else
        {   // No common interface or more than one distinct common interfaces
            return &Interface->CTypeBase::GetProgram()._anyType;
        }
    }

    template <
        typename TArg1,
        typename TArg2,
        typename TFunction>
    bool MatchDataDefinition(
        const CDataDefinition& DataDefinition1, TArg1&& Arg1,
        const CDataDefinition& DataDefinition2, TArg2&& Arg2,
        TFunction F)
    {
        const CTypeBase* Type1 = DataDefinition1.GetType();
        if (!Type1)
        {
            return true;
        }
        const CTypeBase* Type2 = DataDefinition2.GetType();
        if (!Type2)
        {
            return true;
        }
        return uLang::Invoke(
            F,
            Type1, uLang::ForwardArg<TArg1>(Arg1),
            Type2, uLang::ForwardArg<TArg2>(Arg2));
    }

    template <
        typename TArg1,
        typename TArg2,
        typename TFunction>
    bool MatchFunction(
        const CFunction& Function1, TArg1&& Arg1,
        const CFunction& Function2, TArg2&& Arg2,
        TFunction F)
    {
        const CFunctionType* FunctionType1 = Function1._Signature.GetFunctionType();
        if (!FunctionType1)
        {
            return true;
        }
        const CFunctionType* FunctionType2 = Function2._Signature.GetFunctionType();
        if (!FunctionType2)
        {
            return true;
        }
        return uLang::Invoke(
            F,
            Function1._Signature.GetFunctionType(), uLang::ForwardArg<TArg1>(Arg1),
            Function2._Signature.GetFunctionType(), uLang::ForwardArg<TArg2>(Arg2));
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchClassClass(
        const CClass& Class1, TArg1&& Arg1,
        const CClass& Class2, TArg2&& Arg2,
        const uint32_t UploadedAtFnVersion,
        TFunction F)
    {
        if (Class1._GeneralizedClass != Class2._GeneralizedClass)
        {
            if (!VerseFN::UploadedAtFNVersion::AgentInheritsFromEntity(UploadedAtFnVersion) && SemanticTypeUtils::IsAgentTypeExclusive(Class1._GeneralizedClass))
            {
                return false;
            }

            if (const CClass* Superclass = Class1._Superclass)
            {
                return uLang::Invoke(
                    F,
                    Superclass, uLang::ForwardArg<TArg1>(Arg1),
                    &Class2, uLang::ForwardArg<TArg2>(Arg2));
            }
            return false;
        }
        if (&AsPositiveClass(Class1) == &AsPositiveClass(Class2))
        {
            return true;
        }
        int32_t NumDefinitions = Class1.GetDefinitions().Num();
        ULANG_ASSERTF(
            NumDefinitions == Class2.GetDefinitions().Num(),
            "Classes with same definition should have the same number of members");
        for (int32_t DefinitionIndex = 0; DefinitionIndex != NumDefinitions; ++DefinitionIndex)
        {
            const CDefinition* Definition1 = Class1.GetDefinitions()[DefinitionIndex];
            const CDefinition* Definition2 = Class2.GetDefinitions()[DefinitionIndex];
            const CDefinition::EKind DefinitionKind = Definition1->GetKind();
            ULANG_ASSERTF(DefinitionKind == Definition2->GetKind(), "Expected instantiated class members to have the same kind.");
            // The definition types may be `nullptr` if there was an earlier error.
            if (DefinitionKind == CDefinition::EKind::Data)
            {
                const CDataDefinition& DataMember1 = Definition1->AsChecked<CDataDefinition>();
                const CDataDefinition& DataMember2 = Definition2->AsChecked<CDataDefinition>();
                if (!MatchDataDefinition(DataMember1, Arg1, DataMember2, Arg2, F))
                {
                    return false;
                }
            }
            else if (DefinitionKind == CDefinition::EKind::Function)
            {
                const CFunction& Function1 = Definition1->AsChecked<CFunction>();
                const CFunction& Function2 = Definition2->AsChecked<CFunction>();
                if (!MatchFunction(Function1, Arg1, Function2, Arg2, F))
                {
                    return false;
                }
            }
            else
            {
                ULANG_ERRORF("Did not expect class to contain definitions other than methods and data, but found %s '%s'.",
                    DefinitionKindAsCString(Definition1->GetKind()),
                    Definition1->AsNameCString());
                return false;
            }
        }
        return true;
    }

    template <typename TSuperInterfaces, typename TArg1, typename TArg2, typename TFunction, typename TVisited>
    bool MatchAncestorInterfaces(
        const TSuperInterfaces& SuperInterfaces1, TArg1&& Arg1,
        const CInterface& Interface2, TArg2&& Arg2,
        TFunction F,
        bool& Matched,
        TVisited& Visited)
    {
        for (const CInterface* Interface1 : SuperInterfaces1)
        {
            if (Visited.Contains(Interface1))
            {
                continue;
            }
            Visited.Insert(Interface1);
            if (Interface1->_GeneralizedInterface == Interface2._GeneralizedInterface)
            {
                if (!uLang::Invoke(F, Interface1, Arg1, &Interface2, Arg2))
                {
                    // Bail out on failure.  If this is from a `Constrain`
                    // invocation, flow types may have been mutated.
                    return false;
                }
                // Note that a matching interface has been found, but continue
                // searching for repeated inheritance of the same interface with
                // different type arguments.
                Matched = true;
            }
            else if (!MatchAncestorInterfaces(Interface1->_SuperInterfaces, Arg1, Interface2, Arg2, F, Matched, Visited))
            {
                // Recursive call's use of `F` failed.  Bail out.
                return false;
            }
        }
        return true;
    }

    template <typename TSuperInterfaces, typename TArg1, typename TArg2, typename TFunction>
    bool MatchAncestorInterfaces(
        const TSuperInterfaces& SuperInterfaces1, TArg1&& Arg1,
        const CInterface& Interface2, TArg2&& Arg2,
        TFunction F, bool& Matched)
    {
        TSet<const CInterface*> Visited;
        return MatchAncestorInterfaces(
            SuperInterfaces1, uLang::ForwardArg<TArg1>(Arg1),
            Interface2, uLang::ForwardArg<TArg2>(Arg2),
            F,
            Matched,
            Visited);
    }

    template <typename TSuperInterfaces, typename TArg1, typename TArg2, typename TFunction>
    bool MatchAncestorInterfaces(
        const TSuperInterfaces& SuperInterfaces1, TArg1&& Arg1,
        const CInterface& Interface2, TArg2&& Arg2,
        TFunction F)
    {
        bool Matched = false;
        return MatchAncestorInterfaces(
            SuperInterfaces1, uLang::ForwardArg<TArg1>(Arg1),
            Interface2, uLang::ForwardArg<TArg2>(Arg2),
            F,
            Matched) && Matched;
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchInterfaceInterface(
        const CInterface& Interface1, TArg1&& Arg1,
        const CInterface& Interface2, TArg2&& Arg2,
        TFunction F)
    {
        if (Interface1._GeneralizedInterface != Interface2._GeneralizedInterface)
        {
            return MatchAncestorInterfaces(
                Interface1._SuperInterfaces, uLang::ForwardArg<TArg1>(Arg1),
                Interface2, uLang::ForwardArg<TArg2>(Arg2),
                F);
        }
        if (&AsPositiveInterface(Interface1) == &AsPositiveInterface(Interface2))
        {
            return true;
        }
        int32_t NumDefinitions = Interface1.GetDefinitions().Num();
        ULANG_ASSERTF(
            NumDefinitions == Interface2.GetDefinitions().Num(),
            "Interfaces with same definition should have the same number of members: %s %d <> %d",
            Interface1.AsNameCString(), NumDefinitions, Interface2.GetDefinitions().Num());
        for (int32_t DefinitionIndex = 0; DefinitionIndex != NumDefinitions; ++DefinitionIndex)
        {
            const CDefinition* Definition1 = Interface1.GetDefinitions()[DefinitionIndex];
            const CDefinition* Definition2 = Interface2.GetDefinitions()[DefinitionIndex];
            const CDefinition::EKind DefinitionKind = Definition1->GetKind();
            ULANG_ASSERTF(DefinitionKind == Definition2->GetKind(), "Expected instantiated class members to have the same kind.");
            // The definition types may be `nullptr` if there was an earlier error.
            if (DefinitionKind == CDefinition::EKind::Data)
            {
                const CDataDefinition& DataMember1 = Definition1->AsChecked<CDataDefinition>();
                const CDataDefinition& DataMember2 = Definition2->AsChecked<CDataDefinition>();
                if (!MatchDataDefinition(DataMember1, Arg1, DataMember2, Arg2, F))
                {
                    return false;
                }
            }
            else if (DefinitionKind == CDefinition::EKind::Function)
            {
                const CFunction& Function1 = Definition1->AsChecked<CFunction>();
                const CFunction& Function2 = Definition2->AsChecked<CFunction>();
                if (!MatchFunction(Function1, Arg1, Function2, Arg2, F))
                {
                    return false;
                }
            }
            else
            {
                ULANG_ERRORF("Did not expect interface to contain definitions other than methods/fields, but found %s '%s'.",
                    DefinitionKindAsCString(Definition1->GetKind()),
                    Definition1->AsNameCString());
                return false;
            }
        }
        return true;
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchClassInterface(
        const CClass& Class1, TArg1&& Arg1,
        const CInterface& Interface2, TArg2&& Arg2,
        TFunction F)
    {
        bool Matched = false;
        TSet<const CInterface*> Visited;
        for (const CClass* I = &Class1; I; I = I->_Superclass)
        {
            if (!MatchAncestorInterfaces(I->_SuperInterfaces, Arg1, Interface2, Arg2, F, Matched, Visited))
            {
                return false;
            }
        }
        return Matched;
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchNamed(
        const CNamedType& Type1, TArg1&& Arg1,
        const CNamedType& Type2, TArg2&& Arg2,
        TFunction F)
    {
        if (Type1.GetName() != Type2.GetName())
        {
            return false;
        }
        if (!uLang::Invoke(F, Type1.GetValueType(), uLang::ForwardArg<TArg1>(Arg1), Type2.GetValueType(), uLang::ForwardArg<TArg2>(Arg2)))
        {
            return false;
        }
        if (Type1.HasValue() && !Type2.HasValue())
        {
            return false;
        }
        return true;
    }

    template <
        typename TFirstIterator1, typename TLastIterator1, typename TArg1,
        typename TFirstIterator2, typename TLastIterator2, typename TArg2,
        typename TFunction>
    bool MatchElements(
        TFirstIterator1 First1, TLastIterator1 Last1, TArg1&& Arg1,
        TFirstIterator2 First2, TLastIterator2 Last2, TArg2&& Arg2,
        TFunction F)
    {
        if (Last1 - First1 != Last2 - First2)
        {
            return false;
        }
        for (; First1 != Last1; ++First1, ++First2)
        {
            if (!uLang::Invoke(F, *First1, Arg1, *First2, Arg2))
            {
                return false;
            }
        }
        return true;
    }

    template <
        typename TFirstIterator1, typename TLastIterator1, typename TArg1,
        typename TFirstIterator2, typename TLastIterator2, typename TArg2,
        typename TFunction>
    bool MatchNamedElements(
        TFirstIterator1 First1, TLastIterator1 Last1, TArg1&& Arg1,
        TFirstIterator2 First2, TLastIterator2 Last2, TArg2&& Arg2,
        TFunction F)
    {
        while(First1 != Last1 && First2 != Last2)
        {
            const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
            const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
            if (NamedElementType1.GetName() < NamedElementType2.GetName())
            {
                return false;
            }
            else if (NamedElementType2.GetName() < NamedElementType1.GetName())
            {
                if (!NamedElementType2.HasValue())
                {
                    return false;
                }
                ++First2;
            }
            else
            {
                if (!uLang::Invoke(
                    F,
                    NamedElementType1.GetValueType(), Arg1,
                    NamedElementType2.GetValueType(), Arg2))
                {
                    return false;
                }
                if (NamedElementType1.HasValue() && !NamedElementType2.HasValue())
                {
                    return false;
                }
                ++First1;
                ++First2;
            }
        }
        if (First1 != Last1)
        {
            return false;
        }
        for (; First2 != Last2; ++First2)
        {
            const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
            if (!NamedElementType2.HasValue())
            {
                return false;
            }
        }
        return true;
    }

    template <typename TRange1, typename TArg1, typename TRange2, typename TArg2, typename TFunction>
    bool MatchElements(
        TRange1&& ElementTypes1, int32_t FirstNamedIndex1, TArg1&& Arg1,
        TRange2&& ElementTypes2, int32_t FirstNamedIndex2, TArg2&& Arg2,
        TFunction F)
    {
        if (!MatchElements(
            ElementTypes1.begin(), ElementTypes1.begin() + FirstNamedIndex1, Arg1,
            ElementTypes2.begin(), ElementTypes2.begin() + FirstNamedIndex2, Arg2,
            F))
        {
            return false;
        }
        if (!MatchNamedElements(
            ElementTypes1.begin() + FirstNamedIndex1, ElementTypes1.end(), uLang::ForwardArg<TArg1>(Arg1),
            ElementTypes2.begin() + FirstNamedIndex2, ElementTypes2.end(), uLang::ForwardArg<TArg2>(Arg2),
            F))
        {
            return false;
        }
        return true;
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchElements(
        const CTupleType& Type1, TArg1&& Arg1,
        const CTupleType& Type2, TArg2&& Arg2,
        TFunction F)
    {
        return MatchElements(
            Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), uLang::ForwardArg<TArg1>(Arg1),
            Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), uLang::ForwardArg<TArg2>(Arg2),
            F);
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchElements(
        const CTypeBase* Type1, TArg1&& Arg1,
        const CTupleType& Type2, TArg2&& Arg2,
        TFunction F)
    {
        TRangeView ElementTypes1{&Type1, &Type1 + 1};
        int32_t FirstNamedIndex1 = Type1->GetNormalType().IsA<CNamedType>()? 0 : 1;
        return MatchElements(
            ElementTypes1, FirstNamedIndex1, uLang::ForwardArg<TArg1>(Arg1),
            Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), uLang::ForwardArg<TArg2>(Arg2),
            F);
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool MatchElements(
        const CTupleType& Type1, TArg1&& Arg1,
        const CTypeBase* Type2, TArg2&& Arg2,
        TFunction F)
    {
        TRangeView ElementTypes2{&Type2, &Type2 + 1};
        int32_t FirstNamedIndex2 = Type2->GetNormalType().IsA<CNamedType>() ? 0 : 1;
        return MatchElements(
            Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), uLang::ForwardArg<TArg1>(Arg1),
            ElementTypes2, FirstNamedIndex2, uLang::ForwardArg<TArg2>(Arg2),
            F);
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool Match(
        ETypeConstraintFlags Constraint1, const CNormalType& NormalType1, ETypePolarity Type1Polarity, TArg1&& Arg1,
        ETypeConstraintFlags Constraint2, const CNormalType& NormalType2, ETypePolarity Type2Polarity, TArg2&& Arg2,
        const uint32_t UploadedAtFnVersion,
        TFunction F)
    {
        const bool bCastable1 = Enum_HasAnyFlags(Constraint1, ETypeConstraintFlags::Castable) && VerseFN::UploadedAtFNVersion::EnforceCastableSubtype(UploadedAtFnVersion);
        const bool bCastable2 = Enum_HasAnyFlags(Constraint2, ETypeConstraintFlags::Castable) && VerseFN::UploadedAtFNVersion::EnforceCastableSubtype(UploadedAtFnVersion);
        const bool bConcrete1 = Enum_HasAnyFlags(Constraint1, ETypeConstraintFlags::Concrete);
        const bool bConcrete2 = Enum_HasAnyFlags(Constraint2, ETypeConstraintFlags::Concrete);
        if (const CTypeVariable* TypeVariable1 = NormalType1.AsNullable<CTypeVariable>())
        {
            const CTypeType* TypeType1;
            if (Type1Polarity == ETypePolarity::Negative)
            {
                TypeType1 = &TypeVariable1->GetNegativeType()->GetNormalType().AsChecked<CTypeType>();
            }
            else
            {
                TypeType1 = &TypeVariable1->GetPositiveType()->GetNormalType().AsChecked<CTypeType>();
            }
            CSemanticProgram& Program = TypeType1->GetProgram();
            // Don't use `IsExplicitlyCastable` as this will only query if the
            // type is currently castable, but not ensure it stays castable.
            // Note that a positive type may move up the lattice (removing
            // castable) freely if the corresponding negative types are not
            // appropriately moved down the lattice.
            if (!bCastable1 && bCastable2 && !uLang::Invoke(
                F,
                TypeType1->PositiveType(), Arg1,
                &Program.GetOrCreateCastableType(Program._anyType), Arg2))
            {
                return false;
            }
            if (!bConcrete1 && bConcrete2 && !uLang::Invoke(
                F,
                TypeType1->PositiveType(), Arg1,
                &Program.GetOrCreateConcreteType(Program._anyType), Arg2))
            {
                return false;
            }
            if (TypeVariable1 != &NormalType2 && !uLang::Invoke(
                F,
                TypeType1->PositiveType(), uLang::ForwardArg<TArg1>(Arg1),
                &NormalType2, uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
        {
            const CTypeType* TypeType2;
            if (Type2Polarity == ETypePolarity::Negative)
            {
                TypeType2 = &TypeVariable2->GetPositiveType()->GetNormalType().AsChecked<CTypeType>();
            }
            else
            {
                TypeType2 = &TypeVariable2->GetNegativeType()->GetNormalType().AsChecked<CTypeType>();
            }
            // `IsExplicitlyCastable` can be used here, as we know `NormalType1`
            // is not a `CFlowType` or a `CTypeVariable`, and so its bounds are
            // fixed.
            if (!bCastable1 && bCastable2 && !NormalType1.IsExplicitlyCastable())
            {
                return false;
            }
            if (!bConcrete1 && bConcrete2 && !NormalType1.IsExplicitlyConcrete())
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                &NormalType1, uLang::ForwardArg<TArg1>(Arg1),
                TypeType2->NegativeType(), uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        if (!bCastable1 && bCastable2 && !NormalType1.IsExplicitlyCastable())
        {
            return false;
        }
        if (!bConcrete1 && bConcrete2 && !NormalType1.IsExplicitlyConcrete())
        {
            return false;
        }
        if (NormalType1.IsA<CUnknownType>())
        {
            return true;
        }
        if (NormalType1.IsA<CFalseType>())
        {
            return true;
        }
        if (NormalType2.IsA<CAnyType>())
        {
            return true;
        }
        // `void` in the negative position is equivalent to `any`
        if (Type1Polarity == ETypePolarity::Negative && NormalType1.IsA<CVoidType>() && NormalType2.IsA<CAnyType>())
        {
            return true;
        }
        if (Type2Polarity == ETypePolarity::Negative && NormalType2.IsA<CVoidType>())
        {
            return true;
        }
        // `void` in the positive position is equivalent to `true`
        if (Type1Polarity == ETypePolarity::Positive && NormalType1.IsA<CVoidType>() && NormalType2.IsA<CTrueType>())
        {
            return true;
        }
        if (NormalType1.IsA<CTrueType>() && Type2Polarity == ETypePolarity::Positive && NormalType2.IsA<CVoidType>())
        {
            return true;
        }
        if (NormalType2.IsA<CComparableType>() && NormalType1.GetComparability() != EComparability::Incomparable)
        {
            return true;
        }
        if (NormalType2.IsA<CPersistableType>() && NormalType1.IsPersistable())
        {
            return true;
        }
        if (NormalType2.IsA<CRationalType>() && NormalType1.IsA<CIntType>())
        {
            return true;
        }
        if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>(); TupleType1 && NormalType2.IsA<CArrayType>() && TupleType1->GetFirstNamedIndex() == TupleType1->Num())
        {
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            const CTypeBase* ElementType2 = ArrayType2.GetElementType();
            for (const CTypeBase* ElementType1 : TupleType1->GetElements())
            {
                if (!uLang::Invoke(F, ElementType1, Arg1, ElementType2, Arg2))
                {
                    return false;
                }
            }
            return true;
        }
        if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>())
        {
            if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
            {
                return MatchElements(
                    *TupleType1, uLang::ForwardArg<TArg1>(Arg1),
                    *TupleType2, uLang::ForwardArg<TArg2>(Arg2),
                    F);
            }
            if (TupleType1->Num() == 1)
            {
                // A singleton tuple is not a subtype of a single type
                return false;
            }
            // A non-singleton tuple type containing named types with values may be a subtype of a single type
            return MatchElements(
                *TupleType1, uLang::ForwardArg<TArg1>(Arg1),
                &NormalType2, uLang::ForwardArg<TArg2>(Arg2),
                F);
        }
        if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
        {
            if (TupleType2->Num() == 1)
            {
                // A single type is not a subtype of a singleton tuple type
                return false;
            }
            // A single type may be a subtype of a non-singleton tuple type containing named types with values
            return MatchElements(
                &NormalType1, uLang::ForwardArg<TArg1>(Arg1),
                *TupleType2, uLang::ForwardArg<TArg2>(Arg2),
                F);
        }
        if (NormalType1.IsA<CClass>() && NormalType2.IsA<CInterface>())
        {
            // Classes that implement a interface are subtypes of the interface type.
            return MatchClassInterface(
                NormalType1.AsChecked<CClass>(), uLang::ForwardArg<TArg1>(Arg1),
                NormalType2.AsChecked<CInterface>(), uLang::ForwardArg<TArg2>(Arg2),
                F);
        }
        const ETypeKind Kind = NormalType1.GetKind();
        if (Kind != NormalType2.GetKind())
        {
            return false;
        }
        switch (Kind)
        {
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
            // Different module and enumeration types don't have any values in common.
            return false;

        case ETypeKind::Class:
        {
            const CClass& Class1 = NormalType1.AsChecked<CClass>();
            const CClass& Class2 = NormalType2.AsChecked<CClass>();
            return MatchClassClass(
                Class1, uLang::ForwardArg<TArg1>(Arg1),
                Class2, uLang::ForwardArg<TArg2>(Arg2),
                UploadedAtFnVersion,
                F);
        }
        case ETypeKind::Interface:
        {
            const CInterface& Interface1 = NormalType1.AsChecked<CInterface>();
            const CInterface& Interface2 = NormalType2.AsChecked<CInterface>();
            return MatchInterfaceInterface(
                Interface1, uLang::ForwardArg<TArg1>(Arg1),
                Interface2, uLang::ForwardArg<TArg2>(Arg2),
                F);
        }

        case ETypeKind::Array:
        {
            const CArrayType& ArrayType1 = NormalType1.AsChecked<CArrayType>();
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            return uLang::Invoke(
                F,
                ArrayType1.GetElementType(), uLang::ForwardArg<TArg1>(Arg1),
                ArrayType2.GetElementType(), uLang::ForwardArg<TArg2>(Arg2));
        }
        case ETypeKind::Generator:
        {
            const CGeneratorType& GeneratorType1 = NormalType1.AsChecked<CGeneratorType>();
            const CGeneratorType& GeneratorType2 = NormalType2.AsChecked<CGeneratorType>();
            return uLang::Invoke(
                F,
                GeneratorType1.GetElementType(), uLang::ForwardArg<TArg1>(Arg1),
                GeneratorType2.GetElementType(), uLang::ForwardArg<TArg2>(Arg2));
        }
        case ETypeKind::Map:
        {
            const CMapType& MapType1 = static_cast<const CMapType&>(NormalType1);
            const CMapType& MapType2 = static_cast<const CMapType&>(NormalType2);
            if (MapType1.IsWeak() && !MapType2.IsWeak())
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                MapType1.GetKeyType(), Arg1,
                MapType2.GetKeyType(), Arg2))
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                MapType1.GetValueType(), uLang::ForwardArg<TArg1>(Arg1),
                MapType2.GetValueType(), uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType1 = NormalType1.AsChecked<CPointerType>();
            const CPointerType& PointerType2 = NormalType2.AsChecked<CPointerType>();
            if (!uLang::Invoke(
                F,
                PointerType2.NegativeValueType(), Arg2,
                PointerType1.NegativeValueType(), Arg1))
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                PointerType1.PositiveValueType(), uLang::ForwardArg<TArg1>(Arg1),
                PointerType2.PositiveValueType(), uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType1 = NormalType1.AsChecked<CReferenceType>();
            const CReferenceType& ReferenceType2 = NormalType2.AsChecked<CReferenceType>();
            if (!uLang::Invoke(
                F,
                ReferenceType2.NegativeValueType(), Arg2,
                ReferenceType1.NegativeValueType(), Arg1))
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                ReferenceType1.PositiveValueType(), uLang::ForwardArg<TArg1>(Arg1),
                ReferenceType2.PositiveValueType(), uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Option:
        {
            const COptionType& OptionType1 = NormalType1.AsChecked<COptionType>();
            const COptionType& OptionType2 = NormalType2.AsChecked<COptionType>();
            return uLang::Invoke(
                F,
                OptionType1.GetValueType(), uLang::ForwardArg<TArg1>(Arg1),
                OptionType2.GetValueType(), uLang::ForwardArg<TArg2>(Arg2));
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType1 = NormalType1.AsChecked<CTypeType>();
            const CTypeType& TypeType2 = NormalType2.AsChecked<CTypeType>();
            if (!uLang::Invoke(
                F,
                TypeType2.NegativeType(), Arg2,
                TypeType1.NegativeType(), Arg1))
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                TypeType1.PositiveType(), uLang::ForwardArg<TArg1>(Arg1),
                TypeType2.PositiveType(), uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType1 = NormalType1.AsChecked<CFunctionType>();
            const CFunctionType& FunctionType2 = NormalType2.AsChecked<CFunctionType>();
            if (!FunctionType2.GetEffects().HasAll(FunctionType1.GetEffects()))
            {
                return false;
            }
            // Function types are co-variant in return and contra-variant in parameter.
            if (!uLang::Invoke(
                F,
                &FunctionType2.GetParamsType(), Arg2,
                &FunctionType1.GetParamsType(), Arg1))
            {
                return false;
            }
            if (!uLang::Invoke(
                F,
                &FunctionType1.GetReturnType(), uLang::ForwardArg<TArg1>(Arg1),
                &FunctionType2.GetReturnType(), uLang::ForwardArg<TArg2>(Arg2)))
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Variable:
            // Only identical generalized type variables have a subtyping relationship.
            return false;
        case ETypeKind::Named:
            return MatchNamed(
                NormalType1.AsChecked<CNamedType>(), uLang::ForwardArg<TArg1>(Arg1),
                NormalType2.AsChecked<CNamedType>(), uLang::ForwardArg<TArg2>(Arg2),
                F);
        case ETypeKind::Int:
        {
            const CIntType& IntType1 = NormalType1.AsChecked<CIntType>();
            const CIntType& IntType2 = NormalType2.AsChecked<CIntType>();
            if (IntType1.GetMin() < IntType2.GetMin())
            {
                return false;
            }
            if (IntType1.GetMax() > IntType2.GetMax())
            {
                return false;
            }
            return true;
        }
        case ETypeKind::Float:
        {
            const CFloatType& FloatType1 = NormalType1.AsChecked<CFloatType>();
            const CFloatType& FloatType2 = NormalType2.AsChecked<CFloatType>();
            if (FloatType1.MinRanking() < FloatType2.MinRanking())
            {
                return false;
            }
            if (FloatType1.MaxRanking() > FloatType2.MaxRanking())
            {
                return false;
            }
            return true;
        }
        // These cases should be handled by the conditions before the switch.
        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Castable:
        case ETypeKind::Concrete:
        case ETypeKind::Logic:
        case ETypeKind::Rational:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
        case ETypeKind::Tuple:
        default:
            ULANG_UNREACHABLE();
        };
    }

    const CNormalType* StripCastable(const CNormalType& Type)
    {
        if (const CCastableType* CastableType = Type.AsNullable<CCastableType>())
        {
            return &CastableType->SuperType().GetNormalType();
        }
        return nullptr;
    }

    const CNormalType* StripConcrete(const CNormalType& Type)
    {
        if (const CConcreteType* ConcreteType = Type.AsNullable<CConcreteType>())
        {
            return &ConcreteType->SuperType().GetNormalType();
        }
        return nullptr;
    }

    // If a type variable corresponding to the negative use of an explicit type
    // argument, return the corresponding type variable for positive uses.  It
    // is not terribly important which (the type variable for negative use or
    // the type variable for positive use) is used, as long as all type tests
    // use the same one, as pointer equality is important.
    const CNormalType& FixTypeVariable(const CNormalType& Type)
    {
        if (const CTypeVariable* TypeVariable = Type.AsNullable<CTypeVariable>())
        {
            if (const CDataDefinition* ExplicitParam = TypeVariable->_ExplicitParam)
            {
                return *ExplicitParam->_ImplicitParam;
            }
        }
        return Type;
    }

    template <typename T, typename U>
    struct TPair
    {
        T First;
        U Second;

        friend bool operator==(const TPair& Left, const TPair& Right)
        {
            return Left.First == Right.First && Left.Second == Right.Second;
        }
    };

    TPair<ETypeConstraintFlags, const CNormalType*> FlattenType(const CNormalType& Type)
    {
        ETypeConstraintFlags Constraints = ETypeConstraintFlags::None;
        const CNormalType* FlatType = &Type;
        if (const CNormalType* CastableSuperType = StripCastable(*FlatType))
        {
            Constraints |= ETypeConstraintFlags::Castable;
            FlatType = CastableSuperType;
        }
        else if (const CNormalType* ConcreteSuperType = StripConcrete(*FlatType))
        {
            Constraints |= ETypeConstraintFlags::Concrete;
            FlatType = ConcreteSuperType;
        }
        return {Constraints, &FixTypeVariable(*FlatType)};
    }

    template <typename TArg1, typename TArg2, typename TFunction>
    bool Match(
        const CNormalType& NormalType1, ETypePolarity Type1Polarity, TArg1&& Arg1,
        const CNormalType& NormalType2, ETypePolarity Type2Polarity, TArg2&& Arg2,
        const uint32_t UploadedAtFnVersion,
        TFunction F)
    {
        if (&NormalType1 == &NormalType2)
        {
            return true;
        }
        auto [Constraint1, FlatType1] = FlattenType(NormalType1);
        auto [Constraint2, FlatType2] = FlattenType(NormalType2);
        return Match(
            Constraint1, *FlatType1, Type1Polarity, uLang::ForwardArg<TArg1>(Arg1),
            Constraint2, *FlatType2, Type2Polarity, uLang::ForwardArg<TArg2>(Arg2),
            UploadedAtFnVersion,
            F);
    }

    using SConstrainedTypes = TPair<const CTypeBase*, const CTypeBase*>;

    /// Require `Type1` to be a subtype of `Type2`
    /// @returns false if `Type1` cannot be constrained to be a subtype of `Type2`
    bool Constrain(
        const CTypeBase* Type1,
        const CTypeBase* Type2,
        const uint32_t UploadedAtFnVersion,
        TArrayG<SConstrainedTypes, TInlineElementAllocator<16>>& Visited);

    bool Constrain(
        const CTypeBase& Type1,
        const CTypeBase& Type2,
        const uint32_t UploadedAtFnVersion,
        TArrayG<SConstrainedTypes, TInlineElementAllocator<16>>& Visited)
    {
        if (Contains(Visited, SConstrainedTypes{&Type1, &Type2}))
        {
            return true;
        }
        Visited.Add({&Type1, &Type2});
        if (const CFlowType* FlowType1 = Type1.AsFlowType())
        {
            ULANG_ASSERTF(FlowType1->Polarity() == ETypePolarity::Positive, "`Type1` must be positive");
            if (const CFlowType* FlowType2 = Type2.AsFlowType())
            {
                ULANG_ASSERTF(FlowType2->Polarity() == ETypePolarity::Negative, "`Type2` must be negative");
                if (!Constrain(FlowType1->GetChild(), FlowType2->GetChild(), UploadedAtFnVersion, Visited))
                {
                    return false;
                }
                for (const CFlowType* NegativeFlowType1 : FlowType1->FlowEdges())
                {
                    MergeNegative(*NegativeFlowType1, *FlowType2, UploadedAtFnVersion);
                }
                for (const CFlowType* PositiveFlowType2 : FlowType2->FlowEdges())
                {
                    MergePositive(*PositiveFlowType2, *FlowType1, UploadedAtFnVersion);
                }
                return true;
            }
            if (!Constrain(FlowType1->GetChild(), &Type2, UploadedAtFnVersion, Visited))
            {
                return false;
            }
            for (const CFlowType* NegativeFlowType1 : FlowType1->FlowEdges())
            {
                MergeNegativeChild(*NegativeFlowType1, &Type2, UploadedAtFnVersion);
            }
            return true;
        }
        else if (const CFlowType* FlowType2 = Type2.AsFlowType())
        {
            ULANG_ASSERTF(FlowType2->Polarity() == ETypePolarity::Negative, "`Type2` must be negative");
            if (!Constrain(&Type1, FlowType2->GetChild(), UploadedAtFnVersion, Visited))
            {
                return false;
            }
            for (const CFlowType* PositiveFlowType2 : FlowType2->FlowEdges())
            {
                MergePositiveChild(*PositiveFlowType2, &Type1, UploadedAtFnVersion);
            }
            return true;
        }
        const CNormalType& NormalType1 = Type1.GetNormalType();
        const CNormalType& NormalType2 = Type2.GetNormalType();
        return Match(
            NormalType1, ETypePolarity::Positive, nullptr,
            NormalType2, ETypePolarity::Negative, nullptr,
            UploadedAtFnVersion,
            [&](const CTypeBase* ElementType1, auto, const CTypeBase* ElementType2, auto) {
                return Constrain(ElementType1, ElementType2, UploadedAtFnVersion, Visited);
            });
    }

    bool Constrain(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion, TArrayG<SConstrainedTypes, TInlineElementAllocator<16>>& Visited)
    {
        ULANG_ASSERTF(Type1, "Expected non-`nullptr` `Type1`");
        ULANG_ASSERTF(Type2, "Expected non-`nullptr` `Type2`");
        return Constrain(*Type1, *Type2, UploadedAtFnVersion, Visited);
    }

    using SSubsumedTypes = TPair<const CTypeBase*, const CTypeBase*>;

    /// @returns true if all instances of `Type1` ignoring flow types are subtypes of `Type2`
    /// @see Algebraic Subtyping, chapter 8
    template <typename TType1Type2, typename TType2Type1>
    bool Subsumes(
        const CTypeBase& Type1, ETypePolarity Type1Polarity, TType1Type2& Type1Type2,
        const CTypeBase& Type2, ETypePolarity Type2Polarity, TType2Type1& Type2Type1,
        const uint32_t UploadedAtFnVersion)
    {
        if (Contains(Type1Type2, SSubsumedTypes{&Type1, &Type2}))
        {
            return true;
        }
        Type1Type2.Add({&Type1, &Type2});
        const CNormalType& NormalType1 = Type1.GetNormalType();
        const CNormalType& NormalType2 = Type2.GetNormalType();
        return Match(
            NormalType1, Type1Polarity, Type1Type2,
            NormalType2, Type2Polarity, Type2Type1,
            UploadedAtFnVersion,
            [&](const CTypeBase* ElementType1, auto& Type1Type2, const CTypeBase* ElementType2, auto& Type2Type1) {
                return Subsumes(
                    *ElementType1, Type1Polarity, Type1Type2,
                    *ElementType2, Type2Polarity, Type2Type1,
                    UploadedAtFnVersion);
            });
    }

    template <typename TNegativeFlowTypes, typename TPositiveFlowTypes>
    bool Subsumes(
        const CTypeBase& Type1,
        const CTypeBase& Type2,
        const uint32_t UploadedAtFnVersion,
        TNegativeFlowTypes& NegativeFlowTypes,
        TPositiveFlowTypes& PositiveFlowTypes)
    {
        return Subsumes(
            Type1, ETypePolarity::Positive, PositiveFlowTypes,
            Type2, ETypePolarity::Positive, NegativeFlowTypes,
            UploadedAtFnVersion);
    }

    bool ConnectedFlowTypes(const CTypeBase& Type1, const CTypeBase& Type2)
    {
        if (const CFlowType* FlowType1 = Type1.AsFlowType())
        {
            if (const CFlowType* FlowType2 = Type2.AsFlowType())
            {
                if (FlowType1->FlowEdges().Num() < FlowType2->FlowEdges().Num())
                {
                    return Contains(FlowType1->FlowEdges(), FlowType2);
                }
                return Contains(FlowType2->FlowEdges(), FlowType1);
            }
        }
        return false;
    }

    using SAdmissableTypes = TPair<const CTypeBase*, const CTypeBase*>;

    /// @see Algebraic Subtyping, chapter 8
    bool Admissable(
        const CTypeBase& NegativeType,
        const CTypeBase& PositiveType,
        const uint32_t UploadedAtFnVersion,
        TArrayG<SAdmissableTypes, TInlineElementAllocator<16>>& Visited)
    {
        if (Contains(Visited, SAdmissableTypes{&NegativeType, &PositiveType}))
        {
            return true;
        }
        Visited.Add({&NegativeType, &PositiveType});
        if (ConnectedFlowTypes(NegativeType, PositiveType))
        {
            return true;
        }
        const CNormalType& NegativeNormalType = NegativeType.GetNormalType();
        const CNormalType& PositiveNormalType = PositiveType.GetNormalType();
        return Match(
            NegativeNormalType, ETypePolarity::Negative, nullptr,
            PositiveNormalType, ETypePolarity::Positive, nullptr,
            UploadedAtFnVersion,
            [&](const CTypeBase* NegativeElementType, auto, const CTypeBase* PositiveElementType, auto) {
                return Admissable(*NegativeElementType, *PositiveElementType, UploadedAtFnVersion, Visited);
            });
    }

    bool Admissable(const CTypeBase& NegativeType, const CTypeBase& PositiveType, const uint32_t UploadedAtFnVersion)
    {
        TArrayG<SAdmissableTypes, TInlineElementAllocator<16>> Visited;
        return Admissable(NegativeType, PositiveType, UploadedAtFnVersion, Visited);
    }

    template <typename TNegativeTypes, typename TPositiveTypes>
    bool Admissable(
        const TNegativeTypes& NegativeTypes,
        const TPositiveTypes& PositiveTypes,
        const uint32_t UploadedAtFnVersion)
    {
        for (auto&& [NegativeType2, NegativeType1] : NegativeTypes)
        {
            const CFlowType* NegativeFlowType1 = NegativeType1->AsFlowType();
            if (!NegativeFlowType1)
            {
                continue;
            }
            for (const CFlowType* PositiveFlowType1 : NegativeFlowType1->FlowEdges())
            {
                auto Last = PositiveTypes.end();
                auto I = FindIf(PositiveTypes.begin(), Last, [=](auto&& Arg) { return Arg.First == PositiveFlowType1; });
                if (I == Last)
                {
                    continue;
                }
                const CTypeBase* PositiveType2 = I->Second;
                if (!Admissable(*NegativeType2, *PositiveType2, UploadedAtFnVersion))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// @returns true if all instances of `Type1` are subtypes of `Type2`
    /// @see Algebraic Subtyping, chapter 8
    bool IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion);

    bool IsSubtype(const CTypeBase& Type1, const CTypeBase& Type2, const uint32_t UploadedAtFnVersion)
    {
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> NegativeFlowTypes;
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> PositiveFlowTypes;
        if (!Subsumes(Type1, Type2, UploadedAtFnVersion, NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        if (!Admissable(NegativeFlowTypes, PositiveFlowTypes, UploadedAtFnVersion))
        {
            return false;
        }
        return true;
    }

    bool IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
    {
        ULANG_ASSERTF(Type1, "Expected non-`nullptr` `Type1`");
        ULANG_ASSERTF(Type2, "Expected non-`nullptr` `Type2`");
        return IsSubtype(*Type1, *Type2, UploadedAtFnVersion);
    }

    bool IsEquivalent(const CTypeBase& Type1, const CTypeBase& Type2, const uint32_t UploadedAtFnVersion)
    {
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> NegativeFlowTypes;
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> PositiveFlowTypes;
        if (!Subsumes(Type1, Type2, UploadedAtFnVersion, NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        if (!Subsumes(Type2, Type1, UploadedAtFnVersion, NegativeFlowTypes, PositiveFlowTypes))
        {
            return false;
        }
        if (!Admissable(NegativeFlowTypes, PositiveFlowTypes, UploadedAtFnVersion))
        {
            return false;
        }
        return true;
    }

    bool IsEquivalent(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
    {
        ULANG_ASSERTF(Type1, "Expected non-`nullptr` `Type1`");
        ULANG_ASSERTF(Type2, "Expected non-`nullptr` `Type2`");
        return IsEquivalent(*Type1, *Type2, UploadedAtFnVersion);
    }

    using SMatchedTypes = TPair<const CNormalType*, const CNormalType*>;

    bool Matches(
        const CTypeBase* Type1,
        const CTypeBase* Type2,
        const uint32_t UploadedAtFnVersion,
        TArrayG<SMatchedTypes, TInlineElementAllocator<16>>& Visited)
    {
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> PositiveType1NegativeType2;
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> PositiveType2NegativeType1;
        if (!Subsumes(
            *Type1, ETypePolarity::Positive, PositiveType1NegativeType2,
            *Type2, ETypePolarity::Negative, PositiveType2NegativeType1,
            UploadedAtFnVersion))
        {
            return false;
        }
        TArrayG<SSubsumedTypes, TInlineElementAllocator<16>> PositiveType1NegativeType1;
        for (auto&& [PositiveType1, NegativeType2] : PositiveType1NegativeType2)
        {
            const CFlowType* NegativeFlowType2 = NegativeType2->AsFlowType();
            if (!NegativeFlowType2)
            {
                continue;
            }
            for (const CFlowType* PositiveFlowType2 : NegativeFlowType2->FlowEdges())
            {
                auto Last = PositiveType2NegativeType1.end();
                auto I = FindIf(PositiveType2NegativeType1.begin(), Last, [=](auto&& Arg) { return Arg.First == PositiveFlowType2; });
                if (I == Last)
                {
                    continue;
                }
                const CTypeBase* NegativeType1 = I->Second;
                if (!Subsumes(
                    *PositiveType1, ETypePolarity::Positive, PositiveType1NegativeType1,
                    *NegativeType1, ETypePolarity::Negative, PositiveType1NegativeType1,
                    UploadedAtFnVersion))
                {
                    return false;
                }
            }
        }
        return true;
    }
}

bool SemanticTypeUtils::Constrain(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    TArrayG<SConstrainedTypes, TInlineElementAllocator<16>> Visited;
    return uLang::Constrain(Type1, Type2, UploadedAtFnVersion, Visited);
}

bool SemanticTypeUtils::IsSubtype(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    return uLang::IsSubtype(Type1, Type2, UploadedAtFnVersion);
}

bool SemanticTypeUtils::IsEquivalent(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    return uLang::IsEquivalent(Type1, Type2, UploadedAtFnVersion);
}

bool SemanticTypeUtils::Matches(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    TArrayG<SMatchedTypes, TInlineElementAllocator<16>> Visited;
    return uLang::Matches(Type1, Type2, UploadedAtFnVersion, Visited);
}

namespace {
void RemoveAdmissableFlowEdges(const CFlowType& FlowType, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
{
    TArrayG<SAdmissableTypes, TInlineElementAllocator<16>> Visited;
    const CTypeBase* Child = FlowType.GetChild();
    TSet<const CFlowType*>& NegativeFlowTypes = FlowType.FlowEdges();
    auto Last = NegativeFlowTypes.end();
    for (auto I = NegativeFlowTypes.begin(); I != Last;)
    {
        const CFlowType* NegativeFlowType = *I;
        const CTypeBase* NegativeChild = NegativeFlowType->GetChild();
        bool bAdmissable;
        switch (Polarity)
        {
        case ETypePolarity::Negative:
            bAdmissable = Admissable(*Child, *NegativeChild, UploadedAtFnVersion, Visited);
            break;
        case ETypePolarity::Positive:
            bAdmissable = Admissable(*NegativeChild, *Child, UploadedAtFnVersion, Visited);
            break;
        default:
            ULANG_UNREACHABLE();
        }
        if (bAdmissable)
        {
            NegativeFlowType->FlowEdges().Remove(&FlowType);
            NegativeFlowTypes.Remove(NegativeFlowType);
            // Rely on backwards shifting of elements in `TSet`.
        }
        else
        {
            ++I;
        }
    }
}

const CTypeBase* SkipIdentityFlowType(const CTypeBase&, ETypePolarity, const uint32_t UploadedAtFnVersion);

const CTypeBase* SkipIdentityFlowTypeImpl(const CFlowType& FlowType, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
{
    RemoveAdmissableFlowEdges(FlowType, Polarity, UploadedAtFnVersion);
    if (FlowType.FlowEdges().IsEmpty())
    {
        if (const CTypeBase* NewChild = SkipIdentityFlowType(*FlowType.GetChild(), Polarity, UploadedAtFnVersion))
        {
            FlowType.SetChild(NewChild);
        }
        return FlowType.GetChild();
    }
    return nullptr;
}

const CTypeBase* SkipIdentityFlowType(const CTypeBase& Type, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
{
    const CFlowType* FlowType = Type.AsFlowType();
    if (!FlowType)
    {
        return nullptr;
    }
    return SkipIdentityFlowTypeImpl(*FlowType, Polarity, UploadedAtFnVersion);
}
}

const CTypeBase& SemanticTypeUtils::SkipIdentityFlowType(const CFlowType& FlowType, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
{
    if (const CTypeBase* NewType = SkipIdentityFlowTypeImpl(FlowType, Polarity, UploadedAtFnVersion))
    {
        return *NewType;
    }
    return FlowType;
}

const CTypeBase& SemanticTypeUtils::SkipIdentityFlowType(const CTypeBase& Type, ETypePolarity Polarity, const uint32_t UploadedAtFnVersion)
{
    const CFlowType* FlowType = Type.AsFlowType();
    if (!FlowType)
    {
        return Type;
    }
    return SkipIdentityFlowType(*FlowType, Polarity, UploadedAtFnVersion);
}

const CTypeBase& SemanticTypeUtils::SkipEmptyFlowType(const CTypeBase& Type)
{
    const CFlowType* FlowType = Type.AsFlowType();
    if (!FlowType)
    {
        return Type;
    }
    if (!FlowType->FlowEdges().IsEmpty())
    {
        return Type;
    }
    return *FlowType->GetChild();
}

namespace {
const CNamedType& GetOrCreateNamedType(CSemanticProgram& Program, const CNamedType& Type, bool HasValue)
{
    if (Type.HasValue() == HasValue)
    {
        return Type;
    }
    return Program.GetOrCreateNamedType(
        Type.GetName(),
        Type.GetValueType(),
        true);
}

const CTypeBase& JoinNamed(CSemanticProgram& Program, const CNamedType& Type1, const CNamedType& Type2, const uint32_t UploadedAtFnVersion)
{
    CSymbol Name = Type1.GetName();
    if (Name != Type2.GetName())
    {
        CTupleType::ElementArray JoinedElements;
        JoinedElements.Add(&GetOrCreateNamedType(Program, Type1, true));
        JoinedElements.Add(&GetOrCreateNamedType(Program, Type2, true));
        return Program.GetOrCreateTupleType(Move(JoinedElements), 0);
    }
    return Program.GetOrCreateNamedType(
        Name,
        SemanticTypeUtils::Join(Type1.GetValueType(), Type2.GetValueType(), UploadedAtFnVersion),
        Type1.HasValue() || Type2.HasValue());
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
bool JoinElements(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, const uint32_t UploadedAtFnVersion, CTupleType::ElementArray& Result)
{
    if (Last1 - First1 != Last2 - First2)
    {
        return false;
    }
    for (; First1 != Last1; ++First1, ++First2)
    {
        Result.Add(SemanticTypeUtils::Join(*First1, *First2, UploadedAtFnVersion));
    }
    return true;
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
void JoinNamedElements(CSemanticProgram& Program, FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, const uint32_t UploadedAtFnVersion, CTupleType::ElementArray& Result)
{
    while (First1 != Last1 && First2 != Last2)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        if (NamedElementType1.GetName() < NamedElementType2.GetName())
        {
            Result.Add(&GetOrCreateNamedType(Program, NamedElementType1, true));
            ++First1;
        }
        else if (NamedElementType2.GetName() < NamedElementType1.GetName())
        {
            Result.Add(&GetOrCreateNamedType(Program, NamedElementType2, true));
            ++First2;
        }
        else
        {
            Result.Add(&Program.GetOrCreateNamedType(
                NamedElementType1.GetName(),
                SemanticTypeUtils::Join(NamedElementType1.GetValueType(), NamedElementType2.GetValueType(), UploadedAtFnVersion),
                NamedElementType1.HasValue() || NamedElementType2.HasValue()));
            ++First1;
            ++First2;
        }
    }
    for (; First1 != Last1; ++First1)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        Result.Add(&GetOrCreateNamedType(Program, NamedElementType1, true));
    }
    for (; First2 != Last2; ++First2)
    {
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        Result.Add(&GetOrCreateNamedType(Program, NamedElementType2, true));
    }
}

template <typename Range1, typename Range2>
TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, Range1&& ElementTypes1, int32_t FirstNamedIndex1, Range2&& ElementTypes2, int32_t FirstNamedIndex2, const uint32_t UploadedAtFnVersion)
{
    CTupleType::ElementArray Result;
    if (!JoinElements(ElementTypes1.begin(), ElementTypes1.begin() + FirstNamedIndex1, ElementTypes2.begin(), ElementTypes2.begin() + FirstNamedIndex2, UploadedAtFnVersion, Result))
    {
        return {};
    }
    JoinNamedElements(Program, ElementTypes1.begin() + FirstNamedIndex1, ElementTypes1.end(), ElementTypes2.begin() + FirstNamedIndex2, ElementTypes2.end(), UploadedAtFnVersion, Result);
    return Result;
}

TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, const CTupleType& Type1, const CTupleType& Type2, const uint32_t UploadedAtFnVersion)
{
    return JoinElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), UploadedAtFnVersion);
}

TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, const CTypeBase* Type1, const CTupleType& Type2, const uint32_t UploadedAtFnVersion)
{
    TRangeView ElementTypes1{&Type1, &Type1 + 1};
    int32_t FirstNamedIndex1 = Type1->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return JoinElements(Program, ElementTypes1, FirstNamedIndex1, Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), UploadedAtFnVersion);
}

TOptional<CTupleType::ElementArray> JoinElements(CSemanticProgram& Program, const CTupleType& Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    TRangeView ElementTypes2{&Type2, &Type2 + 1};
    int32_t FirstNamedIndex2 = Type2->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return JoinElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), ElementTypes2, FirstNamedIndex2, UploadedAtFnVersion);
}

const CClass* JoinClasses(const CClass& Class1, const CClass& Class2, const uint32_t UploadedAtFnVersion)
{
    auto CollectHierarchy = [] (const CClass* Class) -> TArray<const CClass*>
    {
        TArray<const CClass*> Hierarchy;
        VisitStampType VisitStamp = CScope::GenerateNewVisitStamp();
        while (Class)
        {
            if (!Class->TryMarkVisited(VisitStamp))
            {
                return {};
            }
            Hierarchy.Push(Class);
            Class = Class->_Superclass;
        }
        return Hierarchy;
    };

    TArray<const CClass*> Hierarchy1 = CollectHierarchy(&Class1);
    TArray<const CClass*> Hierarchy2 = CollectHierarchy(&Class2);
    if (Hierarchy1.Num() > Hierarchy2.Num())
    {
        uLang::Swap(Hierarchy1, Hierarchy2);
    }

    using NumType = decltype(Hierarchy1.Num());
    NumType Offset = Hierarchy2.Num() - Hierarchy1.Num();
    for (NumType I = 0, NumHierarchy1 = Hierarchy1.Num(); I != NumHierarchy1; ++I)
    {
        const CClass* HierarchyClass1 = Hierarchy1[I];
        const CClass* HierarchyClass2 = Hierarchy2[I + Offset];
        const CClass* GeneralizedClass = HierarchyClass1->_GeneralizedClass;
        if (GeneralizedClass == HierarchyClass2->_GeneralizedClass)
        {
            TArray<STypeVariableSubstitution> TypeVariableSubstitutions = JoinTypeVariableSubstitutions(
                GeneralizedClass->_TypeVariableSubstitutions,
                HierarchyClass1->_TypeVariableSubstitutions,
                HierarchyClass2->_TypeVariableSubstitutions,
                UploadedAtFnVersion);
            if (auto InstantiatedClass = InstantiateClass(*GeneralizedClass, ETypePolarity::Positive, TypeVariableSubstitutions))
            {
                return InstantiatedClass;
            }
            return GeneralizedClass;
        }
    }

    return nullptr;
}

const CTypeBase& JoinInt(CSemanticProgram& Program, const CIntType& IntType1, const CNormalType& Type2)
{
    if (const CIntType* IntType2 = Type2.AsNullable<CIntType>())
    {
        FIntOrNegativeInfinity Min = CMath::Min(IntType1.GetMin(), IntType2->GetMin());
        FIntOrPositiveInfinity Max = CMath::Max(IntType1.GetMax(), IntType2->GetMax());
        return Program.GetOrCreateConstrainedIntType(Min, Max);
    }
    if (Type2.IsA<CRationalType>())
    {
        return Type2;
    }
    if (Type2.GetComparability() != EComparability::Incomparable)
    {
        return Program._comparableType;
    }
    return Program._anyType;
}

const CTypeBase* JoinTypeVariable(const CTypeVariable* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    // These `IsSubtype` calls hold in general for `Join`, but are
    // necessary here to emulate
    // @code
    // Type1 /\ Type2 == Type1 <=> Type2 <= Type1
    // @endcode
    if (IsSubtype(Type2, Type1, UploadedAtFnVersion))
    {
        return Type1;
    }
    // and
    // @code
    // Type1 /\ Type2 == Type2 <=> Type1 <= Type2
    // @endcode
    if (IsSubtype(Type1, Type2, UploadedAtFnVersion))
    {
        return Type2;
    }
    if (const CDataDefinition* ExplicitParam = Type1->_ExplicitParam)
    {
        Type1 = ExplicitParam->_ImplicitParam;
    }
    const CTypeType* PositiveTypeType1 = Type1->GetPositiveType()->GetNormalType().AsNullable<CTypeType>();
    if (!PositiveTypeType1)
    {
        PositiveTypeType1 = Type1->GetProgram()._typeType;
    }
    return SemanticTypeUtils::Join(PositiveTypeType1->PositiveType(), Type2, UploadedAtFnVersion);
}
}

const CTypeBase* SemanticTypeUtils::Join(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    ULANG_ASSERTF(Type1 && Type2, "Expected non-null arguments to Join");
    ULANG_ASSERTF(
        &Type1->GetProgram() == &Type2->GetProgram(),
        "Types '%s' and '%s' are from different programs",
        Type1->AsCode().AsCString(),
        Type2->AsCode().AsCString());
    CSemanticProgram& Program = Type1->GetProgram();

    if (const CFlowType* FlowType1 = Type1->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType1->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        Merge(Result, *FlowType1, Polarity, UploadedAtFnVersion);
        if (const CFlowType* FlowType2 = Type2->AsFlowType())
        {
            Merge(Result, *FlowType2, Polarity, UploadedAtFnVersion);
        }
        else
        {
            MergeChild(Result, Type2, Polarity, UploadedAtFnVersion);
        }
        return &Result;
    }
    if (const CFlowType* FlowType2 = Type2->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType2->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        MergeChild(Result, Type1, Polarity, UploadedAtFnVersion);
        Merge(Result, *FlowType2, Polarity, UploadedAtFnVersion);
        return &Result;
    }

    const CNormalType& NormalType1 = Type1->GetNormalType();
    const CNormalType& NormalType2 = Type2->GetNormalType();
    if (&NormalType1 == &NormalType2)
    {
        return Type1;
    }
    else if ((NormalType1.IsA<CTupleType>() && NormalType2.IsA<CArrayType>())
          || (NormalType2.IsA<CTupleType>() && NormalType1.IsA<CArrayType>()))
    {
        const CTupleType* TupleType = &(NormalType1.IsA<CTupleType>() ? NormalType1 : NormalType2).AsChecked<CTupleType>();
        const CArrayType* ArrayType = &(NormalType1.IsA<CArrayType>() ? NormalType1 : NormalType2).AsChecked<CArrayType>();
        const CTupleType::ElementArray& TupleElementTypes = TupleType->GetElements();
        if (TupleType->NumNonNamedElements() == TupleElementTypes.Num())
        {
            // If there are no named elements of the tuple, the join is the
            // array of joined elements.
            const CTypeBase* ResultElementType = ArrayType->GetElementType();
            for (auto I = TupleElementTypes.begin(), Last = TupleElementTypes.begin() + TupleType->NumNonNamedElements(); I != Last; ++I)
            {
                ResultElementType = Join(ResultElementType, *I, UploadedAtFnVersion);
            }
            return &Program.GetOrCreateArrayType(ResultElementType);
        }
        // If there are any named elements, then the join must also allow for
        // them.  However, given one argument to the join certainly does not
        // have them (the array type), they mustn't be required (i.e. must have
        // defaults).  Furthermore, any number of unnamed elements must be
        // allowed when no named elements exist.  This is impossible to
        // represent with the current vocabulary of types.  Approximate with
        // `any`.
        return &Program._anyType;
    }
    // If one type is $class, and the other is $interface, the result is $interface if $class implements its, otherwise try to find a common $interface.
    else if ((NormalType1.IsA<CClass>() && NormalType2.IsA<CInterface>())
          || (NormalType2.IsA<CClass>() && NormalType1.IsA<CInterface>()))
    {
        const CInterface* Interface = &(NormalType1.IsA<CInterface>() ? NormalType1 : NormalType2).AsChecked<CInterface>();
        const CClass* Class         = &(NormalType1.IsA<CClass>()     ? NormalType1 : NormalType2).AsChecked<CClass>();
        return JoinInterfaces(Interface, Class, UploadedAtFnVersion);
    }
    else if (NormalType1.IsA<CVoidType>() && NormalType2.IsA<CTrueType>()) { return Type2; }
    else if (NormalType1.IsA<CTrueType>() && NormalType2.IsA<CVoidType>()) { return Type1; }
    // If either type is unknown or false, the result is the other type.
    else if (NormalType1.IsA<CUnknownType>()) { return Type2; }
    else if (NormalType2.IsA<CUnknownType>()) { return Type1; }
    else if (NormalType1.IsA<CFalseType>()) { return Type2; }
    else if (NormalType2.IsA<CFalseType>()) { return Type1; }
    else if (const CTypeVariable* TypeVariable1 = NormalType1.AsNullable<CTypeVariable>())
    {
        return JoinTypeVariable(TypeVariable1, Type2, UploadedAtFnVersion);
    }
    else if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
    {
        return JoinTypeVariable(TypeVariable2, Type1, UploadedAtFnVersion);
    }
    else if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>())
    {
        if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
        {
            if (TOptional<CTupleType::ElementArray> Elements = JoinElements(Program, *TupleType1, *TupleType2, UploadedAtFnVersion))
            {
                return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
            }
        }
        else if (TupleType1->Num() != 1)
        {
            if (TOptional<CTupleType::ElementArray> Elements = JoinElements(Program, *TupleType1, Type2, UploadedAtFnVersion))
            {
                return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
            }
        }
        if (TupleType1->GetComparability() != EComparability::Incomparable && NormalType2.GetComparability() != EComparability::Incomparable)
        {
            return &Program._comparableType;
        }
        return &Program._anyType;
    }
    else if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
    {
        if (TupleType2->Num() != 1)
        {
            if (TOptional<CTupleType::ElementArray> Elements = JoinElements(Program, Type1, *TupleType2, UploadedAtFnVersion))
            {
                return &Program.GetOrCreateTupleType(Move(*Elements), TupleType2->GetFirstNamedIndex());
            }
        }
        if (NormalType1.GetComparability() != EComparability::Incomparable && TupleType2->GetComparability() != EComparability::Incomparable)
        {
            return &Program._comparableType;
        }
        return &Program._anyType;
    }
    else if (const CIntType* IntType1 = NormalType1.AsNullable<CIntType>()) { return &JoinInt(Program, *IntType1, NormalType2); }
    else if (const CIntType* IntType2 = NormalType2.AsNullable<CIntType>()) { return &JoinInt(Program, *IntType2, NormalType1); }
    else if (const CCastableType* CastableType1 = NormalType1.AsNullable<CCastableType>())
    {
        if (const CCastableType* CastableType2 = NormalType2.AsNullable<CCastableType>())
        {
            return &Program.GetOrCreateCastableType(*Join(&CastableType1->SuperType(), &CastableType2->SuperType(), UploadedAtFnVersion));
        }
        const CTypeBase* JoinType = Join(&CastableType1->SuperType(), &NormalType2, UploadedAtFnVersion);
        if (JoinType->GetNormalType().IsExplicitlyCastable())
        {
            return JoinType;
        }
        if (!NormalType2.IsExplicitlyCastable())
        {
            return JoinType;
        }
        return &Program.GetOrCreateCastableType(*JoinType);
    }
    else if (const CCastableType* CastableType2 = NormalType2.AsNullable<CCastableType>())
    {
        const CTypeBase* JoinType = Join(&NormalType1, &CastableType2->SuperType(), UploadedAtFnVersion);
        if (JoinType->GetNormalType().IsExplicitlyCastable())
        {
            return JoinType;
        }
        if (!NormalType1.IsExplicitlyCastable())
        {
            return JoinType;
        }
        return &Program.GetOrCreateCastableType(*JoinType);
    }
    else if (const CConcreteType* ConcreteType1 = NormalType1.AsNullable<CConcreteType>())
    {
        if (const CConcreteType* ConcreteType2 = NormalType2.AsNullable<CConcreteType>())
        {
            return &Program.GetOrCreateConcreteType(*Join(&ConcreteType1->SuperType(), &ConcreteType2->SuperType(), UploadedAtFnVersion));
        }
        const CTypeBase* JoinType = Join(&ConcreteType1->SuperType(), &NormalType2, UploadedAtFnVersion);
        if (JoinType->GetNormalType().IsExplicitlyConcrete())
        {
            return JoinType;
        }
        if (!NormalType2.IsExplicitlyConcrete())
        {
            return JoinType;
        }
        return &Program.GetOrCreateConcreteType(*JoinType);
        }
    else if (const CConcreteType* ConcreteType2 = NormalType2.AsNullable<CConcreteType>())
    {
        const CTypeBase* JoinType = Join(&NormalType1, &ConcreteType2->SuperType(), UploadedAtFnVersion);
        if (JoinType->GetNormalType().IsExplicitlyConcrete())
        {
            return JoinType;
        }
        if (!NormalType1.IsExplicitlyConcrete())
        {
            return JoinType;
        }
        return &Program.GetOrCreateConcreteType(*JoinType);
    }
    else if (NormalType1.GetKind() != NormalType2.GetKind())
    {
        if (NormalType1.GetComparability() != EComparability::Incomparable && NormalType2.GetComparability() != EComparability::Incomparable)
        {
            return &Program._comparableType;
        }
        return &Program._anyType;
    }
    else
    {
        const ETypeKind CommonKind = NormalType1.GetKind();
        switch (CommonKind)
        {
        case ETypeKind::Module:
            // These types have no join less than any.
            return &Program._anyType;

        case ETypeKind::Enumeration:
            return &Program._comparableType;

        case ETypeKind::Class:
        {
            const CClass& Class1 = NormalType1.AsChecked<CClass>();
            const CClass& Class2 = NormalType2.AsChecked<CClass>();

            // For classes, find the most derived common ancestor
            const CClass* CommonClass = JoinClasses(Class1, Class2, UploadedAtFnVersion);

            // Find the set of interfaces both classes implement.
            TInterfaceSet Interfaces1;
            CollectAllInterfaces(Interfaces1, &Class1);
            TInterfaceSet Interfaces2;
            CollectAllInterfaces(Interfaces2, &Class2);
            TInterfaceSet CommonInterfaces = FindCommonInterfaces(Interfaces1, Interfaces2, UploadedAtFnVersion);

            // If there is a join of the two classes ignoring interfaces and it
            // is a subtype of the joins of the interfaces, use it.
            if (CommonClass)
            {
                if (AllOf(CommonInterfaces, [=](const CInterface* CommonInterface) { return IsSubtype(CommonClass, CommonInterface, UploadedAtFnVersion); }))
                {
                    return CommonClass;
                }
            }
            // If there is no join of the two classes ignoring interfaces, if
            // there is a single interface join, use it.  Note if there is a
            // join of the two classes ignoring interfaces and a single
            // interface join, but the class join is not a subtype of the
            // interface join, neither should be used.
            else if (CommonInterfaces.Num() == 1)
            {
                return CommonInterfaces[0];
            }

            if (Class1.GetComparability() != EComparability::Incomparable && Class2.GetComparability() != EComparability::Incomparable)
            {
                return &Program._comparableType;
            }
            return &Program._anyType;
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType1 = NormalType1.AsChecked<CTypeType>();
            const CTypeType& TypeType2 = NormalType2.AsChecked<CTypeType>();
            const CTypeBase* NegativeType = Meet(TypeType1.NegativeType(), TypeType2.NegativeType(), UploadedAtFnVersion);
            const CTypeBase* PositiveType = Join(TypeType1.PositiveType(), TypeType2.PositiveType(), UploadedAtFnVersion);
            return &Program.GetOrCreateTypeType(NegativeType, PositiveType);
        }
        case ETypeKind::Interface:
        {
            // For interfaces, find the most derived common ancestor
            const CInterface* Interface1 = &NormalType1.AsChecked<CInterface>();
            const CInterface* Interface2 = &NormalType2.AsChecked<CInterface>();
            return JoinInterfaces(Interface1, Interface2, UploadedAtFnVersion);
        }
        case ETypeKind::Array:
        {
            // For array types, return an array type with the join of both element types.
            const CArrayType& ArrayType1 = NormalType1.AsChecked<CArrayType>();
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            const CTypeBase* JoinElementType = Join(ArrayType1.GetElementType(), ArrayType2.GetElementType(), UploadedAtFnVersion);
            return &Program.GetOrCreateArrayType(JoinElementType);
        }
        case ETypeKind::Generator:
        {
            // For generator types, return an generator type with the join of both element types.
            const CGeneratorType& GeneratorType1 = NormalType1.AsChecked<CGeneratorType>();
            const CGeneratorType& GeneratorType2 = NormalType2.AsChecked<CGeneratorType>();
            const CTypeBase* JoinElementType = Join(GeneratorType1.GetElementType(), GeneratorType2.GetElementType(), UploadedAtFnVersion);
            return &Program.GetOrCreateGeneratorType(JoinElementType);
        }
        case ETypeKind::Map:
        {
            // The join of two map types is a map with the join (union) of their key type and the join (union) of their value type.
            const CMapType& MapType1 = NormalType1.AsChecked<CMapType>();
            const CMapType& MapType2 = NormalType2.AsChecked<CMapType>();
            const CTypeBase* JoinKeyType = Join(MapType1.GetKeyType(), MapType2.GetKeyType(), UploadedAtFnVersion);
            const CTypeBase* JoinValueType = Join(MapType1.GetValueType(), MapType2.GetValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreateMapType(*JoinKeyType, *JoinValueType, MapType1.IsWeak() || MapType2.IsWeak());
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType1 = NormalType1.AsChecked<CPointerType>();
            const CPointerType& PointerType2 = NormalType2.AsChecked<CPointerType>();
            const CTypeBase* MeetNegativeValueType = Meet(PointerType1.NegativeValueType(), PointerType2.NegativeValueType(), UploadedAtFnVersion);
            const CTypeBase* JoinPositiveValueType = Join(PointerType1.PositiveValueType(), PointerType2.PositiveValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreatePointerType(MeetNegativeValueType, JoinPositiveValueType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType1 = NormalType1.AsChecked<CReferenceType>();
            const CReferenceType& ReferenceType2 = NormalType2.AsChecked<CReferenceType>();
            const CTypeBase* MeetNegativeValueType = Meet(ReferenceType1.NegativeValueType(), ReferenceType2.NegativeValueType(), UploadedAtFnVersion);
            const CTypeBase* JoinPositiveValueType = Join(ReferenceType1.PositiveValueType(), ReferenceType2.PositiveValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreateReferenceType(MeetNegativeValueType, JoinPositiveValueType);
        }
        case ETypeKind::Option:
        {
            // For option types, return an option type with the join of both value types.
            const COptionType& OptionType1 = NormalType1.AsChecked<COptionType>();
            const COptionType& OptionType2 = NormalType2.AsChecked<COptionType>();

            const CTypeBase* CommonValueType = Join(OptionType1.GetValueType(), OptionType2.GetValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreateOptionType(CommonValueType);
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType1 = NormalType1.AsChecked<CFunctionType>();
            const CFunctionType& FunctionType2 = NormalType2.AsChecked<CFunctionType>();
            // The join of two function types is the meet (intersection) of their parameter type and the join (union) of their return type.
            const CTypeBase* MeetParamsType = Meet(&FunctionType1.GetParamsType(), &FunctionType2.GetParamsType(), UploadedAtFnVersion);
            const CTypeBase* JoinReturnType = Join(&FunctionType1.GetReturnType(), &FunctionType2.GetReturnType(), UploadedAtFnVersion);
            SEffectSet JoinEffects = FunctionType1.GetEffects() | FunctionType2.GetEffects();
            return &Program.GetOrCreateFunctionType(*MeetParamsType, *JoinReturnType, JoinEffects);
        }
        case ETypeKind::Named:
            return &JoinNamed(Program, NormalType1.AsChecked<CNamedType>(), NormalType2.AsChecked<CNamedType>(), UploadedAtFnVersion);

        case ETypeKind::Float:
        {
            const CFloatType& FloatType1 = NormalType1.AsChecked<CFloatType>();
            const CFloatType& FloatType2 = NormalType2.AsChecked<CFloatType>();

            double Min = (FloatType1.MinRanking() <= FloatType2.MinRanking()) ? FloatType1.GetMin() : FloatType2.GetMin();
            double Max = (FloatType1.MaxRanking() >= FloatType2.MaxRanking()) ? FloatType1.GetMax() : FloatType2.GetMax();

            return &Program.GetOrCreateConstrainedFloatType(Min, Max);
        }

        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Castable:
        case ETypeKind::Concrete:
        case ETypeKind::Logic:
        case ETypeKind::Rational:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
            // It shouldn't be possible to reach here for one of the global types; it should be
            // handled by the first Type1==Type2 case.
            ULANG_FALLTHROUGH;
        case ETypeKind::Int:
        case ETypeKind::Tuple:
        case ETypeKind::Variable:
        default:
            ULANG_UNREACHABLE();
        }
    }
}

namespace {
const CTypeBase& MeetNamed(CSemanticProgram& Program, const CNamedType& Type1, const CNamedType& Type2, const uint32_t UploadedAtFnVersion)
{
    CSymbol Name = Type1.GetName();
    if (Name != Type2.GetName())
    {
        if (!Type1.HasValue())
        {
            return Program._falseType;
        }
        if (!Type2.HasValue())
        {
            return Program._falseType;
        }
        return Program.GetOrCreateTupleType({});
    }
    return Program.GetOrCreateNamedType(
        Name,
        SemanticTypeUtils::Meet(Type1.GetValueType(), Type2.GetValueType(), UploadedAtFnVersion),
        Type1.HasValue() && Type2.HasValue());
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
bool MeetElements(FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, const uint32_t UploadedAtFnVersion, CTupleType::ElementArray& Result)
{
    if (Last1 - First1 != Last2 - First2)
    {
        return false;
    }
    for (; First1 != Last1; ++First1, ++First2)
    {
        Result.Add(SemanticTypeUtils::Meet(*First1, *First2, UploadedAtFnVersion));
    }
    return true;
}

template <typename FirstIterator1, typename LastIterator1, typename FirstIterator2, typename LastIterator2>
bool MeetNamedElements(CSemanticProgram& Program, FirstIterator1 First1, LastIterator1 Last1, FirstIterator2 First2, LastIterator2 Last2, const uint32_t UploadedAtFnVersion, CTupleType::ElementArray& Result)
{
    while (First1 != Last1 && First2 != Last2)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        if (NamedElementType1.GetName() < NamedElementType2.GetName())
        {
            if (!NamedElementType1.HasValue())
            {
                return false;
            }
            ++First1;
        }
        else if (NamedElementType2.GetName() < NamedElementType1.GetName())
        {
            if (!NamedElementType2.HasValue())
            {
                return false;
            }
            ++First2;
        }
        else
        {
            Result.Add(&Program.GetOrCreateNamedType(
                NamedElementType1.GetName(),
                SemanticTypeUtils::Meet(NamedElementType1.GetValueType(), NamedElementType2.GetValueType(), UploadedAtFnVersion),
                NamedElementType1.HasValue() && NamedElementType2.HasValue()));
            ++First1;
            ++First2;
        }
    }
    for (; First1 != Last1; ++First1)
    {
        const CNamedType& NamedElementType1 = (*First1)->GetNormalType().template AsChecked<CNamedType>();
        if (!NamedElementType1.HasValue())
        {
            return false;
        }
    }
    for (; First2 != Last2; ++First2)
    {
        const CNamedType& NamedElementType2 = (*First2)->GetNormalType().template AsChecked<CNamedType>();
        if (!NamedElementType2.HasValue())
        {
            return false;
        }
    }
    return true;
}

template <typename Range1, typename Range2>
TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, Range1&& ElementTypes1, int32_t FirstNamedIndex1, Range2&& ElementTypes2, int32_t FirstNamedIndex2, const uint32_t UploadedAtFnVersion)
{
    CTupleType::ElementArray Result;
    if (!MeetElements(ElementTypes1.begin(), ElementTypes1.begin() + FirstNamedIndex1, ElementTypes2.begin(), ElementTypes2.begin() + FirstNamedIndex2, UploadedAtFnVersion, Result))
    {
        return {};
    }
    if (!MeetNamedElements(Program, ElementTypes1.begin() + FirstNamedIndex1, ElementTypes1.end(), ElementTypes2.begin() + FirstNamedIndex2, ElementTypes2.end(), UploadedAtFnVersion, Result))
    {
        return {};
    }
    return Result;
}

TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, const CTupleType& Type1, const CTupleType& Type2, const uint32_t UploadedAtFnVersion)
{
    return MeetElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), UploadedAtFnVersion);
}

TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, const CTypeBase* Type1, const CTupleType& Type2, const uint32_t UploadedAtFnVersion)
{
    TRangeView ElementTypes1{&Type1, &Type1 + 1};
    int32_t FirstNamedIndex1 = Type1->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return MeetElements(Program, ElementTypes1, FirstNamedIndex1, Type2.ElementsWithSortedNames(), Type2.GetFirstNamedIndex(), UploadedAtFnVersion);
}

TOptional<CTupleType::ElementArray> MeetElements(CSemanticProgram& Program, const CTupleType& Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    TRangeView ElementTypes2{&Type2, &Type2 + 1};
    int32_t FirstNamedIndex2 = Type2->GetNormalType().IsA<CNamedType>() ? 0 : 1;
    return MeetElements(Program, Type1.ElementsWithSortedNames(), Type1.GetFirstNamedIndex(), ElementTypes2, FirstNamedIndex2, UploadedAtFnVersion);
}

const CTypeBase& MeetInt(CSemanticProgram& Program, const CIntType& IntType1, const CNormalType& Type2)
{
    if (const CIntType* IntType2 = Type2.AsNullable<CIntType>())
    {
        FIntOrNegativeInfinity Min = CMath::Max(IntType1.GetMin(), IntType2->GetMin());
        FIntOrPositiveInfinity Max = CMath::Min(IntType1.GetMax(), IntType2->GetMax());
        return Program.GetOrCreateConstrainedIntType(Min, Max);
    }
    if (Type2.IsA<CRationalType>())
    {
        return IntType1;
    }
    return Program._falseType;
}

const CTypeBase* MeetTypeVariable(const CTypeVariable* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    // These `IsSubtype` calls hold in general for `Meet`, but are
    // necessary here to emulate
    // @code
    // Type1 \/ Type2 == Type1 <=> Type1 <= Type2
    // @endcode
    if (IsSubtype(Type1, Type2, UploadedAtFnVersion))
    {
        return Type1;
    }
    // and
    // @code
    // Type1 \/ Type2 == Type2 <=> Type2 <= Type1
    // @endcode
    if (IsSubtype(Type2, Type1, UploadedAtFnVersion))
    {
        return Type2;
    }
    if (const CDataDefinition* ExplicitParam = Type1->_ExplicitParam)
    {
        Type1 = ExplicitParam->_ImplicitParam;
    }
    const CTypeType* PositiveTypeType1 = Type1->GetPositiveType()->GetNormalType().AsNullable<CTypeType>();
    if (!PositiveTypeType1)
    {
        PositiveTypeType1 = Type1->GetProgram()._typeType;
    }
    return SemanticTypeUtils::Meet(PositiveTypeType1->NegativeType(), Type2, UploadedAtFnVersion);
}
}

const CTypeBase* SemanticTypeUtils::Meet(const CTypeBase* Type1, const CTypeBase* Type2, const uint32_t UploadedAtFnVersion)
{
    ULANG_ASSERTF(Type1 && Type2, "Expected non-null arguments to Meet");
    ULANG_ASSERTF(
        &Type1->GetProgram() == &Type2->GetProgram(),
        "Types '%s' and '%s' are from different programs",
        Type1->AsCode().AsCString(),
        Type2->AsCode().AsCString());
    CSemanticProgram& Program = Type1->GetProgram();

    if (const CFlowType* FlowType1 = Type1->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType1->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        Merge(Result, *FlowType1, Polarity, UploadedAtFnVersion);
        if (const CFlowType* FlowType2 = Type2->AsFlowType())
        {
            Merge(Result, *FlowType2, Polarity, UploadedAtFnVersion);
        }
        else
        {
            MergeChild(Result, Type2, Polarity, UploadedAtFnVersion);
        }
        return &Result;
    }
    if (const CFlowType* FlowType2 = Type2->AsFlowType())
    {
        const ETypePolarity Polarity = FlowType2->Polarity();
        CFlowType& Result = Program.CreateFlowType(Polarity);
        MergeChild(Result, Type1, Polarity, UploadedAtFnVersion);
        Merge(Result, *FlowType2, Polarity, UploadedAtFnVersion);
        return &Result;
    }

    const CNormalType& NormalType1 = Type1->GetNormalType();
    const CNormalType& NormalType2 = Type2->GetNormalType();

    if (&NormalType1 == &NormalType2)
    {
        return Type1;
    }
    else if (NormalType1.IsA<CComparableType>() && NormalType2.GetComparability() != EComparability::Incomparable) { return Type2; }
    else if (NormalType2.IsA<CComparableType>() && NormalType1.GetComparability() != EComparability::Incomparable) { return Type1; }
    else if (NormalType1.IsA<CPersistableType>() && NormalType2.IsPersistable()) { return Type2; }
    else if (NormalType2.IsA<CPersistableType>() && NormalType1.IsPersistable()) { return Type1; }
    else if (const CCastableType* CastableType1 = NormalType1.AsNullable<CCastableType>())
    {
        if (const CCastableType* CastableType2 = NormalType2.AsNullable<CCastableType>())
        {
            return &Program.GetOrCreateCastableType(*Meet(&CastableType1->SuperType(), &CastableType2->SuperType(), UploadedAtFnVersion));
        }
        const CTypeBase* MeetType = Meet(&CastableType1->SuperType(), &NormalType2, UploadedAtFnVersion);
        return &Program.GetOrCreateCastableType(*MeetType);
    }
    else if (const CCastableType* CastableType2 = NormalType2.AsNullable<CCastableType>())
    {
        const CTypeBase* MeetType = Meet(&NormalType1, &CastableType2->SuperType(), UploadedAtFnVersion);
        return &Program.GetOrCreateCastableType(*MeetType);
    }
    else if (const CConcreteType* ConcreteType1 = NormalType1.AsNullable<CConcreteType>())
    {
        if (const CConcreteType* ConcreteType2 = NormalType2.AsNullable<CConcreteType>())
        {
            return &Program.GetOrCreateConcreteType(*Meet(&ConcreteType1->SuperType(), &ConcreteType2->SuperType(), UploadedAtFnVersion));
        }
        const CTypeBase* MeetType = Meet(&ConcreteType1->SuperType(), &NormalType2, UploadedAtFnVersion);
        return &Program.GetOrCreateConcreteType(*MeetType);
    }
    else if (const CConcreteType* ConcreteType2 = NormalType2.AsNullable<CConcreteType>())
    {
        const CTypeBase* MeetType = Meet(&NormalType1, &ConcreteType2->SuperType(), UploadedAtFnVersion);
        return &Program.GetOrCreateConcreteType(*MeetType);
    }
    // If either type is any, the result is the other type.
    else if (NormalType1.IsA<CAnyType>()) { return Type2; }
    else if (NormalType2.IsA<CAnyType>()) { return Type1; }
    else if (NormalType1.IsA<CVoidType>()) { return Type2; }
    else if (NormalType2.IsA<CVoidType>()) { return Type1; }
    else if ((NormalType1.IsA<CTupleType>() && NormalType2.IsA<CArrayType>())
          || (NormalType2.IsA<CTupleType>() && NormalType1.IsA<CArrayType>()))
    {
        const CTupleType& TupleType = (NormalType1.IsA<CTupleType>() ? NormalType1 : NormalType2).AsChecked<CTupleType>();
        const CArrayType& ArrayType = (NormalType1.IsA<CArrayType>() ? NormalType1 : NormalType2).AsChecked<CArrayType>();
        const CTupleType::ElementArray& TupleElementTypes = TupleType.GetElements();
        if (!AllOf(TupleElementTypes.begin() + TupleType.GetFirstNamedIndex(), TupleElementTypes.end(),
            [](const CTypeBase* Element) { return Element->GetNormalType().AsChecked<CNamedType>().HasValue(); }))
        {
            // An array cannot provide named elements.  If any are present
            // lacking a default in the tuple, the meet is `false`.
            return &Program._falseType;
        }
        if (TupleType.NumNonNamedElements() == 1 && TupleElementTypes.Num() != 1)
        {
            // If named elements are present in the tuple and there is a single
            // unnamed tuple element, the meet may be the meet of the single
            // unnamed tuple element and the array.
            const CTypeBase* ResultType = Meet(TupleElementTypes[0], &ArrayType, UploadedAtFnVersion);
            if (!ResultType->GetNormalType().IsA<CFalseType>())
            {
                return ResultType;
            }
            // However, if `false`, a higher (non-`false`) type will certainly
            // be found via element-wise meet on the tuple, as such a type will
            // at least be `tuple(false)`, which is (arguably) higher than
            // `false`.  Note the element-wise case may also produce lower types,
            // e.g. `[]any \/ tuple([]any, ?X:int = 0)` would produce
            // `tuple([]any)`, which is lower than what is produced by the
            // above (`[]any`).
        }
        CTupleType::ElementArray ResultElements;
        ResultElements.Reserve(TupleType.NumNonNamedElements());
        for (auto I = TupleElementTypes.begin(), Last = TupleElementTypes.begin() + TupleType.NumNonNamedElements(); I != Last; ++I)
        {
            ResultElements.Add(Meet(*I, ArrayType.GetElementType(), UploadedAtFnVersion));
        }
        return &Program.GetOrCreateTupleType(Move(ResultElements));
    }
    // If one type is a class, and the other is a interface, the result is a class if the class implements the interface, otherwise false.
    else if ((NormalType1.IsA<CClass>() && NormalType2.IsA<CInterface>())
          || (NormalType2.IsA<CClass>() && NormalType1.IsA<CInterface>()))
    {
        const CInterface& Interface = (NormalType1.IsA<CInterface>() ? NormalType1 : NormalType2).AsChecked<CInterface>();
        const CClass&     Class     = (NormalType1.IsA<CClass>()     ? NormalType1 : NormalType2).AsChecked<CClass>();
        if (SemanticTypeUtils::IsSubtype(&Class, &Interface, UploadedAtFnVersion))
        {
            return &Class;
        }
        return &Program._falseType;
    }
    // If either type is false or unknown, the result is that type.
    else if (NormalType1.IsA<CFalseType>()) { return Type1; }
    else if (NormalType2.IsA<CFalseType>()) { return Type2; }
    else if (NormalType1.IsA<CUnknownType>()) { return Type1; }
    else if (NormalType2.IsA<CUnknownType>()) { return Type2; }
    else if (const CTypeVariable* TypeVariable1 = NormalType1.AsNullable<CTypeVariable>())
    {
        return MeetTypeVariable(TypeVariable1, Type2, UploadedAtFnVersion);
    }
    else if (const CTypeVariable* TypeVariable2 = NormalType2.AsNullable<CTypeVariable>())
    {
        return MeetTypeVariable(TypeVariable2, Type1, UploadedAtFnVersion);
    }
    else if (const CTupleType* TupleType1 = NormalType1.AsNullable<CTupleType>())
    {
        if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
        {
            TOptional<CTupleType::ElementArray> Elements = MeetElements(Program, *TupleType1, *TupleType2, UploadedAtFnVersion);
            if (!Elements)
            {
                return &Program._falseType;
            }
            return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
        }
        if (TupleType1->Num() == 1)
        {
            return &Program._falseType;
        }
        TOptional<CTupleType::ElementArray> Elements = MeetElements(Program, *TupleType1, Type2, UploadedAtFnVersion);
        if (!Elements)
        {
            return &Program._falseType;
        }
        if (Elements->Num() == 1)
        {
            // For `TupleType1` of size != 1, this may only hold if `TupleType1`'s
            // named elements all have values and `TupleType1` has a single unnamed
            // element.
            return (*Elements)[0];
        }
        return &Program.GetOrCreateTupleType(Move(*Elements), TupleType1->GetFirstNamedIndex());
    }
    else if (const CTupleType* TupleType2 = NormalType2.AsNullable<CTupleType>())
    {
        if (TupleType2->Num() == 1)
        {
            return &Program._falseType;
        }
        TOptional<CTupleType::ElementArray> Elements = MeetElements(Program, Type1, *TupleType2, UploadedAtFnVersion);
        if (!Elements)
        {
            return &Program._falseType;
        }
        if (Elements->Num() == 1)
        {
            // For `TupleType2` of size != 1, this may only hold if `TupleType2`'s
            // named elements all have values and `TupleType2` has a single unnamed
            // element.
            return (*Elements)[0];
        }
        return &Program.GetOrCreateTupleType(Move(*Elements), TupleType2->GetFirstNamedIndex());
    }
    else if (const CIntType* IntType1 = NormalType1.AsNullable<CIntType>()) { return &MeetInt(Program, *IntType1, NormalType2); }
    else if (const CIntType* IntType2 = NormalType2.AsNullable<CIntType>()) { return &MeetInt(Program, *IntType2, NormalType1); }
    else if (NormalType1.GetKind() != NormalType2.GetKind())
    {
        return &Program._falseType;
    }
    else
    {
        const ETypeKind CommonKind = NormalType1.GetKind();
        switch(CommonKind)
        {
        case ETypeKind::Module:
        case ETypeKind::Enumeration:
            // These types have no meet greater than false.
            return &Program._falseType;

        case ETypeKind::Class:
        {
            // For classes, if one is a subclass of the other, that is the meet of the two classes.
            const CClass& Class1 = NormalType1.AsChecked<CClass>();
            const CClass& Class2 = NormalType2.AsChecked<CClass>();
            if (SemanticTypeUtils::IsSubtype(&Class1, &Class2, UploadedAtFnVersion)) { return Type1; }
            if (SemanticTypeUtils::IsSubtype(&Class2, &Class1, UploadedAtFnVersion)) { return Type2; }
            return &Program._falseType;
        }
        case ETypeKind::Interface:
        {
            // For interfaces, if one is a subinterface of the other, that is the meet of the two interfaces.
            const CInterface& Interface1 = NormalType1.AsChecked<CInterface>();
            const CInterface& Interface2 = NormalType2.AsChecked<CInterface>();
            if (SemanticTypeUtils::IsSubtype(&Interface2, &Interface1, UploadedAtFnVersion)) { return Type2; }
            if (SemanticTypeUtils::IsSubtype(&Interface1, &Interface2, UploadedAtFnVersion)) { return Type1; }
            return &Program._falseType;
        }
        case ETypeKind::Type:
        {
            const CTypeType& TypeType1 = NormalType1.AsChecked<CTypeType>();
            const CTypeType& TypeType2 = NormalType2.AsChecked<CTypeType>();
            const CTypeBase* NegativeType = Join(TypeType1.NegativeType(), TypeType2.NegativeType(), UploadedAtFnVersion);
            const CTypeBase* PositiveType = Meet(TypeType1.PositiveType(), TypeType2.PositiveType(), UploadedAtFnVersion);
            return &Program.GetOrCreateTypeType(NegativeType, PositiveType);
        }
        case ETypeKind::Array:
        {
            // For array types, return an array type with the meet of both element types.
            const CArrayType& ArrayType1 = NormalType1.AsChecked<CArrayType>();
            const CArrayType& ArrayType2 = NormalType2.AsChecked<CArrayType>();
            const CTypeBase* MeetElementType = Meet(ArrayType1.GetElementType(), ArrayType2.GetElementType(), UploadedAtFnVersion);
            return &Program.GetOrCreateArrayType(MeetElementType);
        }
        case ETypeKind::Generator:
        {
            // For generator types, return an generator type with the meet of both element types.
            const CGeneratorType& GeneratorType1 = NormalType1.AsChecked<CGeneratorType>();
            const CGeneratorType& GeneratorType2 = NormalType2.AsChecked<CGeneratorType>();
            const CTypeBase* MeetElementType = Meet(GeneratorType1.GetElementType(), GeneratorType2.GetElementType(), UploadedAtFnVersion);
            return &Program.GetOrCreateGeneratorType(MeetElementType);
        }
        case ETypeKind::Map:
        {
            // The meet of two map types is a map with the meet (intersection) of their key type and the meet (intersection) of their value type.
            const CMapType& MapType1 = NormalType1.AsChecked<CMapType>();
            const CMapType& MapType2 = NormalType2.AsChecked<CMapType>();
            const CTypeBase* MeetKeyType   = Meet(MapType1.GetKeyType()  , MapType2.GetKeyType(), UploadedAtFnVersion);
            const CTypeBase* MeetValueType = Meet(MapType1.GetValueType(), MapType2.GetValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreateMapType(*MeetKeyType, *MeetValueType, MapType1.IsWeak() && MapType2.IsWeak());
        }
        case ETypeKind::Pointer:
        {
            const CPointerType& PointerType1 = NormalType1.AsChecked<CPointerType>();
            const CPointerType& PointerType2 = NormalType2.AsChecked<CPointerType>();
            const CTypeBase* JoinNegativeValueType = Join(PointerType1.NegativeValueType(), PointerType2.NegativeValueType(), UploadedAtFnVersion);
            const CTypeBase* MeetPositiveValueType = Meet(PointerType1.PositiveValueType(), PointerType2.PositiveValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreatePointerType(JoinNegativeValueType, MeetPositiveValueType);
        }
        case ETypeKind::Reference:
        {
            const CReferenceType& ReferenceType1 = NormalType1.AsChecked<CReferenceType>();
            const CReferenceType& ReferenceType2 = NormalType2.AsChecked<CReferenceType>();
            const CTypeBase* JoinNegativeValueType = Join(ReferenceType1.NegativeValueType(), ReferenceType2.NegativeValueType(), UploadedAtFnVersion);
            const CTypeBase* MeetPositiveValueType = Meet(ReferenceType1.PositiveValueType(), ReferenceType2.PositiveValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreateReferenceType(JoinNegativeValueType, MeetPositiveValueType);
        }
        case ETypeKind::Option:
        {
            // For option types, return an option type with the meet of both value types.
            const COptionType& OptionType1 = NormalType1.AsChecked<COptionType>();
            const COptionType& OptionType2 = NormalType2.AsChecked<COptionType>();
            const CTypeBase* MeetValueType = Meet(OptionType1.GetValueType(), OptionType2.GetValueType(), UploadedAtFnVersion);
            return &Program.GetOrCreateOptionType(MeetValueType);
        }
        case ETypeKind::Function:
        {
            const CFunctionType& FunctionType1 = NormalType1.AsChecked<CFunctionType>();
            const CFunctionType& FunctionType2 = NormalType2.AsChecked<CFunctionType>();
            // The meet type of two functions is the join (union) of their parameter type and the meet (intersection) of their return type.
            const CTypeBase* JoinParamsType = Join(&FunctionType1.GetParamsType(), &FunctionType2.GetParamsType(), UploadedAtFnVersion);
            const CTypeBase* MeetReturnType = Meet(&FunctionType1.GetReturnType(), &FunctionType2.GetReturnType(), UploadedAtFnVersion);
            SEffectSet MeetEffects = FunctionType1.GetEffects() & FunctionType2.GetEffects();
            return &Program.GetOrCreateFunctionType(*JoinParamsType, *MeetReturnType, MeetEffects);
        }
        case ETypeKind::Named:
            return &MeetNamed(Program, NormalType1.AsChecked<CNamedType>(), NormalType2.AsChecked<CNamedType>(), UploadedAtFnVersion);

        case ETypeKind::Float:
        {
            const CFloatType& FloatType1 = NormalType1.AsChecked<CFloatType>();
            const CFloatType& FloatType2 = NormalType2.AsChecked<CFloatType>();

            double Min = (FloatType1.MinRanking() >= FloatType2.MinRanking()) ? FloatType1.GetMin() : FloatType2.GetMin();
            double Max = (FloatType1.MaxRanking() <= FloatType2.MaxRanking()) ? FloatType1.GetMax() : FloatType2.GetMax();

            return &Program.GetOrCreateConstrainedFloatType(Min, Max);
        }

        case ETypeKind::Unknown:
        case ETypeKind::False:
        case ETypeKind::True:
        case ETypeKind::Void:
        case ETypeKind::Any:
        case ETypeKind::Comparable:
        case ETypeKind::Persistable:
        case ETypeKind::Castable:
        case ETypeKind::Concrete:
        case ETypeKind::Logic:
        case ETypeKind::Rational:
        case ETypeKind::Char8:
        case ETypeKind::Char32:
        case ETypeKind::Path:
        case ETypeKind::Range:
            // It shouldn't be possible to reach here for one of the global types; it should be
            // handled by the first Type1==Type2 case.
            ULANG_FALLTHROUGH;
        case ETypeKind::Int:
        case ETypeKind::Tuple:
        case ETypeKind::Variable:
        default:
            ULANG_UNREACHABLE();
        }
    }
}

namespace {

bool AreDomainsDistinct(const CNormalType&, const CNormalType&, const uint32_t UploadedAtFnVersion);
bool AreDomainsDistinct(const CTypeBase*, const CTypeBase*, const uint32_t UploadedAtFnVersion);

bool IsDomainTop(const CNormalType& DomainType)
{
    return DomainType.GetKind() == Cases<ETypeKind::Void, ETypeKind::Any>;
}

bool IsBottom(const CNormalType& Type)
{
    return Type.GetKind() == Cases<ETypeKind::Unknown, ETypeKind::False>;
}

// Note, all specific `Is<...>Distinct` predicates assume `IsBottom` and `IsDomainTop`
// have already been checked by `AreDomainsDistinct`.

bool IsDomainTrueDistinct(const CNormalType& DomainType)
{
    if (DomainType.GetKind() == Cases<
        ETypeKind::Function,
        ETypeKind::Map,
        ETypeKind::Array,
        ETypeKind::Generator,
        ETypeKind::Logic,
        ETypeKind::Option,
        ETypeKind::True>)
    {
        return false;
    }
    if (const CTupleType* DomainTupleType = DomainType.AsNullable<CTupleType>())
    {
        return DomainTupleType->Num() != 0;
    }
    return true;
}

bool IsNamedTypeDistinct(const CNamedType& NamedType1, const CNormalType& Type2, const uint32_t UploadedAtFnVersion)
{
    // We need to handle cases like this, which are not distinct. The first F
    // has a single parameter which is a named type, and the second F has a tuple
    // with the named types in it.
    // F(?X:int=42)
    // F(?X:int=10, ?Y:int=42)
    //
    // Also, note that this works as expected when explicitly declaring
    // a tuple as a singular parameter. These should not be distinct:
    // F(?X:int)
    // F(P:tuple(?X:int))
    //
    // Also, if we had a syntax to write this, this property nests, so this
    // wouldn't be distinct:
    //
    // F(A:tuple(int, ?X:int))
    // F(A:tuple(int, tuple(?X:int, ?Y:int=42)))
    if (const CTupleType* Tuple = Type2.AsNullable<CTupleType>())
    {
        return AreDomainsDistinct(NamedType1.ToTupleType(), *Tuple, UploadedAtFnVersion);
    }

    const CNamedType* NamedType2 = Type2.AsNullable<CNamedType>();
    if (!NamedType2)
    {
        return true;
    }

    if (NamedType1.HasValue() && NamedType2->HasValue())
    {
        return false;
    }

    if (NamedType1.GetName() != NamedType2->GetName())
    {
        return true;
    }

    // If only one or neither named types have a default value, then we are
    // distinct if the types are distinct. Consider the example:
    // F(X?:int=42)
    // F(X?:float)
    // when deciding which function to invoke. If a value is provided for X, we can
    // decide based on that value's type. If a value isn't provided for X, we can
    // decide based only having one function with a default value. Naturally, if
    // neither has a default value, the only way to tell a difference is via their types.
    return AreDomainsDistinct(NamedType1.GetValueType(), NamedType2->GetValueType(), UploadedAtFnVersion);
}

bool IsDomainNonEmptyTupleDistinct(const CNormalType& DomainType1, const CTupleType& DomainTupleType2, const uint32_t UploadedAtFnVersion)
{
    if (DomainType1.IsA<CFunctionType>())
    {
        return false;
    }
    if (const CMapType* MapType = DomainType1.AsNullable<CMapType>())
    {
        if (!MapType->GetKeyType()->GetNormalType().IsA<CIntType>())
        {
            return true;
        }
        const CNormalType& ValueType = MapType->GetValueType()->GetNormalType();
        return AnyOf(
            DomainTupleType2.GetElements(),
            [&](const CTypeBase* Arg) { return AreDomainsDistinct(ValueType, Arg->GetNormalType(), UploadedAtFnVersion); });
    }
    if (const CArrayType* ArrayType = DomainType1.AsNullable<CArrayType>())
    {
        const CNormalType& ElementType = ArrayType->GetElementType()->GetNormalType();
        return AnyOf(
            DomainTupleType2.GetElements(),
            [&](const CTypeBase* Arg) { return AreDomainsDistinct(ElementType, Arg->GetNormalType(), UploadedAtFnVersion); });
    }
    if (DomainType1.IsA<CLogicType>())
    {
        return DomainTupleType2.Num() != 1;
    }
    if (const COptionType* OptionType = DomainType1.AsNullable<COptionType>())
    {
        if (DomainTupleType2.Num() != 1)
        {
            return true;
        }
        const CNormalType& ValueType = OptionType->GetValueType()->GetNormalType();
        return
            !ValueType.IsA<CIntType>() ||
            AreDomainsDistinct(ValueType, DomainTupleType2[0]->GetNormalType(), UploadedAtFnVersion);
    }
    if (const CTupleType* DomainTupleType1 = DomainType1.AsNullable<CTupleType>())
    {
        auto NumNonNamedElements = DomainTupleType1->NumNonNamedElements();
        if (NumNonNamedElements != DomainTupleType2.NumNonNamedElements())
        {
            return true;
        }

        bool bAreAnyNonNamedDistinct = AnyOf(TUntil{NumNonNamedElements},
            [&](auto I) { return AreDomainsDistinct((*DomainTupleType1)[I], DomainTupleType2[I], UploadedAtFnVersion); });
        if (bAreAnyNonNamedDistinct)
        {
            return true;
        }

        // The ways named sections of tuples can be distinct:
        // - If the named value shows up in both tuples and is distinct.
        // - If a named value is present in one tuple, but not the other, and is a required value in one.
        //   Notably, if it's optional in one, but not present in the other, then we can't use it as
        //   a form of distinction.
        ULANG_ASSERTF(DomainTupleType1->GetFirstNamedIndex() == DomainTupleType2.GetFirstNamedIndex(), "Otherwise we would've already said they're distinct.");
        TArray<CSymbol> SeenNames;
        for (int32_t I = DomainTupleType1->GetFirstNamedIndex(); I < DomainTupleType1->Num(); ++I)
        {
            const CNamedType& NamedType = (*DomainTupleType1)[I]->GetNormalType().AsChecked<CNamedType>();
            SeenNames.Push(NamedType.GetName());

            if (const CNamedType* Match = DomainTupleType2.FindNamedType(NamedType.GetName()))
            {
                if (IsNamedTypeDistinct(NamedType, *Match, UploadedAtFnVersion))
                {
                    return true;
                }
            }
            else if (!NamedType.HasValue())
            {
                return true;
            }
        }

        for (int32_t I = DomainTupleType2.GetFirstNamedIndex(); I < DomainTupleType2.Num(); ++I)
        {
            const CNamedType& NamedType = DomainTupleType2[I]->GetNormalType().AsChecked<CNamedType>();
            if (!SeenNames.Contains(NamedType.GetName()) && !NamedType.HasValue())
            {
                return true;
            }
        }

        return false;
    }
    return true;
}

bool IsAnyClassDistinct(const CNormalType& Type)
{
    if (Type.GetKind() == ETypeKind::Interface)
    {
        return false;
    }
    return true;
}

bool IsClassDistinct(const CNormalType& Type1, const CClass& Class2, const uint32_t UploadedAtFnVersion)
{
    if (Type1.GetKind() == ETypeKind::Interface)
    {
        return false;
    }
    if (const CClass* Class1 = Type1.AsNullable<CClass>())
    {
        if (Class1->IsStruct())
        {
            return true;
        }
        const CClass& PositiveClass1 = AsPositiveClass(*Class1);
        const CClass& PositiveClass2 = AsPositiveClass(Class2);
        return !IsSubtype(&PositiveClass1, &PositiveClass2, UploadedAtFnVersion) && !IsSubtype(&PositiveClass2, &PositiveClass1, UploadedAtFnVersion);
    }
    return true;
}

bool IsStructDistinct(const CNormalType& Type1, const CClass& Struct2)
{
    if (Type1.IsA<CClass>())
    {
        return &Type1 != &Struct2;
    }
    return true;
}

bool IsDomainTypeDistinct(const CNormalType& DomainType1, const CTypeType& DomainTypeType2, const uint32_t UploadedAtFnVersion)
{
    if (const CTypeType* DomainTypeType1 = DomainType1.AsNullable<CTypeType>())
    {
        if (!AreDomainsDistinct(DomainTypeType1->NegativeType(), DomainTypeType2.NegativeType(), UploadedAtFnVersion))
        {
            return false;
        }
        if (!AreDomainsDistinct(DomainTypeType1->PositiveType(), DomainTypeType2.PositiveType(), UploadedAtFnVersion))
        {
            return false;
        }
    }
    return true;
}

bool IsPointerDistinct(const CNormalType& Type1, const CPointerType& PointerType2, const uint32_t UploadedAtFnVersion)
{
    if (const CPointerType* PointerType1 = Type1.AsNullable<CPointerType>())
    {
        if (!AreDomainsDistinct(PointerType1->NegativeValueType(), PointerType2.NegativeValueType(), UploadedAtFnVersion))
        {
            return false;
        }
        if (!AreDomainsDistinct(PointerType1->PositiveValueType(), PointerType2.PositiveValueType(), UploadedAtFnVersion))
        {
            return false;
        }
    }
    return true;
}

bool IsReferenceDistinct(const CNormalType& Type1, const CReferenceType& ReferenceType2, const uint32_t UploadedAtFnVersion)
{
    if (const CReferenceType* ReferenceType1 = Type1.AsNullable<CReferenceType>())
    {
        if (!AreDomainsDistinct(ReferenceType1->NegativeValueType(), ReferenceType2.NegativeValueType(), UploadedAtFnVersion))
        {
            return false;
        }
        if (!AreDomainsDistinct(ReferenceType1->PositiveValueType(), ReferenceType2.PositiveValueType(), UploadedAtFnVersion))
        {
            return false;
        }
    }
    return true;
}

bool IsEnumerationDistinct(const CNormalType& Type1, const CEnumeration& Enumeration2)
{
    if (Type1.IsA<CEnumeration>())
    {
        return &Type1 != &Enumeration2;
    }
    return true;
}

bool IsIntDistinct(const CNormalType& Type1, const CIntType& Int2)
{
    if (const CIntType* Int1 = Type1.AsNullable<CIntType>())
    {
        return !Int1->IsInhabitable() || !Int2.IsInhabitable()
            || (Int1->GetMin() < Int2.GetMin() && Int1->GetMax() < Int2.GetMin())
            || (Int2.GetMin() < Int1->GetMin() && Int2.GetMax() < Int1->GetMin());
    }
    return !Type1.IsA<CRationalType>();
}

bool IsFloatDistinct(const CNormalType& Type1, const CFloatType& Float2)
{
    if (const CFloatType* Float1 = Type1.AsNullable<CFloatType>())
    {

        return !Float1->IsInhabitable() || !Float2.IsInhabitable()
            || (Float1->MinRanking() < Float2.MinRanking() && Float1->MaxRanking() < Float2.MinRanking())
            || (Float2.MinRanking() < Float1->MinRanking() && Float2.MaxRanking() < Float1->MinRanking());
    }
    return true;
}

bool AreDomainsDistinct(const CNormalType& DomainType1, const CNormalType& DomainType2, const uint32_t UploadedAtFnVersion)
{
    // If two types do not share a subtype above `false`, they are distinct. In
    // other words, if the intersection of the sets of values contained in two
    // types is empty, they are distinct. All types other than `false` must
    // reach a type just above `false` (where the lattice edges point down), so
    // if two types reach the same type above `false`, they may share a
    // possibly-inhabited subtype, i.e. they are not distinct. In terms of sets,
    // if two sets of values contain the same subset, they are not distinct.
    // Importantly, the subtype need not currently exist, just be possible to
    // exist. Furthermore, this means the problem can be reduced to checking
    // subtyping of the types just above `false` against the argument types.
    if (&DomainType1 == &DomainType2)
    {
        return false;
    }
    if (IsDomainTop(DomainType1) || IsDomainTop(DomainType2))
    {
        return false;
    }
    if (DomainType1.GetKind() == ETypeKind::Comparable)
    {
        return DomainType2.GetComparability() == EComparability::Incomparable;
    }
    if (DomainType2.GetKind() == ETypeKind::Comparable)
    {
        return DomainType1.GetComparability() == EComparability::Incomparable;
    }
    if (DomainType1.GetKind() == ETypeKind::Persistable)
    {
        return !DomainType2.IsPersistable();
    }
    if (DomainType2.GetKind() == ETypeKind::Persistable)
    {
        return !DomainType1.IsPersistable();
    }
    if (DomainType1.GetKind() == ETypeKind::Variable || DomainType2.GetKind() == ETypeKind::Variable)
    {
        return false;
    }
    if (IsBottom(DomainType1) || IsBottom(DomainType2))
    {
        return false;
    }
    // Types for which `true` is a subtype (`true` being just above `false`)
    if (!IsDomainTrueDistinct(DomainType1) && !IsDomainTrueDistinct(DomainType2))
    {
        return false;
    }
    // Names types.
    // We put this before tuples so that at the top level, named type
    // comparison has special handling when compared against a tuple.
    // No need to implement the same logic both in named type comparison
    // and tuple comparison, so we do named types first.
    if (const CNamedType* NamedType1 = DomainType1.AsNullable<CNamedType>())
    {
        return IsNamedTypeDistinct(*NamedType1, DomainType2, UploadedAtFnVersion);
    }
    if (const CNamedType* NamedType2 = DomainType2.AsNullable<CNamedType>())
    {
        return IsNamedTypeDistinct(*NamedType2, DomainType1, UploadedAtFnVersion);
    }
    // Tuples for which `true` is not a subtype, i.e. non-empty tuples
    //
    // Note, only non-empty tuples are compared with other types.  Types above
    // non-empty tuples are not compared to one another, as all such types are
    // also above `true` and are handled by `IsTrueDistinct`.
    if (const CTupleType* DomainTupleType1 = DomainType1.AsNullable<CTupleType>(); DomainTupleType1 && DomainTupleType1->Num() != 0)
    {
        return IsDomainNonEmptyTupleDistinct(DomainType2, *DomainTupleType1, UploadedAtFnVersion);
    }
    if (const CTupleType* DomainTupleType2 = DomainType2.AsNullable<CTupleType>(); DomainTupleType2 && DomainTupleType2->Num() != 0)
    {
        return IsDomainNonEmptyTupleDistinct(DomainType1, *DomainTupleType2, UploadedAtFnVersion);
    }
    // Types strictly above classes
    if (!IsAnyClassDistinct(DomainType1) && !IsAnyClassDistinct(DomainType2))
    {
        return false;
    }
    // Classes and structs
    if (const CClass* DomainClass1 = DomainType1.AsNullable<CClass>())
    {
        return DomainClass1->IsStruct() ? IsStructDistinct(DomainType2, *DomainClass1) : IsClassDistinct(DomainType2, *DomainClass1, UploadedAtFnVersion);
    }
    if (const CClass* DomainClass2 = DomainType2.AsNullable<CClass>())
    {
        return DomainClass2->IsStruct() ? IsStructDistinct(DomainType1, *DomainClass2) : IsClassDistinct(DomainType1, *DomainClass2, UploadedAtFnVersion);
    }
    // Subtype types
    if (const CTypeType* DomainTypeType1 = DomainType1.AsNullable<CTypeType>())
    {
        return IsDomainTypeDistinct(DomainType2, *DomainTypeType1, UploadedAtFnVersion);
    }
    if (const CTypeType* DomainTypeType2 = DomainType2.AsNullable<CTypeType>())
    {
        return IsDomainTypeDistinct(DomainType1, *DomainTypeType2, UploadedAtFnVersion);
    }
    // Pointer types
    if (const CPointerType* DomainPointerType1 = DomainType1.AsNullable<CPointerType>())
    {
        return IsPointerDistinct(DomainType2, *DomainPointerType1, UploadedAtFnVersion);
    }
    if (const CPointerType* DomainPointerType2 = DomainType2.AsNullable<CPointerType>())
    {
        return IsPointerDistinct(DomainType1, *DomainPointerType2, UploadedAtFnVersion);
    }
    // Reference types
    if (const CReferenceType* DomainReferenceType1 = DomainType1.AsNullable<CReferenceType>())
    {
        return IsReferenceDistinct(DomainType2, *DomainReferenceType1, UploadedAtFnVersion);
    }
    if (const CReferenceType* DomainReferenceType2 = DomainType2.AsNullable<CReferenceType>())
    {
        return IsReferenceDistinct(DomainType1, *DomainReferenceType2, UploadedAtFnVersion);
    }
    // Enumerations
    if (const CEnumeration* DomainEnumeration1 = DomainType1.AsNullable<CEnumeration>())
    {
        return IsEnumerationDistinct(DomainType2, *DomainEnumeration1);
    }
    if (const CEnumeration* DomainEnumeration2 = DomainType2.AsNullable<CEnumeration>())
    {
        return IsEnumerationDistinct(DomainType1, *DomainEnumeration2);
    }
    // Ints
    if (const CIntType* DomainInt1 = DomainType1.AsNullable<CIntType>())
    {
        return IsIntDistinct(DomainType2, *DomainInt1);
    }
    if (const CIntType* DomainInt2 = DomainType2.AsNullable<CIntType>())
    {
        return IsIntDistinct(DomainType1, *DomainInt2);
    }
    // Floats
    if (const CFloatType* DomainFloat1 = DomainType1.AsNullable<CFloatType>())
    {
        return IsFloatDistinct(DomainType2, *DomainFloat1);
    }
    if (const CFloatType* DomainFloat2 = DomainType2.AsNullable<CFloatType>())
    {
        return IsFloatDistinct(DomainType1 , *DomainFloat2);
    }

    return true;
}

bool AreDomainsDistinct(const CTypeBase* DomainType1, const CTypeBase* DomainType2, const uint32_t UploadedAtFnVersion)
{
    ULANG_ASSERTF(DomainType1 && DomainType2, "Expected non-null arguments to AreTypesDistinct");
    return AreDomainsDistinct(DomainType1->GetNormalType(), DomainType2->GetNormalType(), UploadedAtFnVersion);
}
}

bool SemanticTypeUtils::AreDomainsDistinct(const CTypeBase* DomainType1, const CTypeBase* DomainType2, const uint32_t UploadedAtFnVersion)
{
    ULANG_ASSERTF(DomainType1 && DomainType2, "Expected non-null arguments to AreTypesDistinct");
    ULANG_ASSERTF(
        &DomainType1->GetProgram() == &DomainType2->GetProgram(),
        "Types '%s' and '%s' are from different programs",
        DomainType1->AsCode().AsCString(),
        DomainType2->AsCode().AsCString());
    return uLang::AreDomainsDistinct(DomainType1, DomainType2, UploadedAtFnVersion);
}

namespace
{
bool IsUnknownTypeImpl(const CTypeBase* Type, TSet<const CFlowType*>& VisitedFlowTypes)
{
    ULANG_ASSERTF(Type, "Queried for types should never be null -- we should be using CUnknownType instead.");
    if (const CFlowType* FlowType = Type->AsFlowType())
    {
        if (VisitedFlowTypes.Contains(FlowType))
        {
            return false;
        }
        VisitedFlowTypes.Insert(FlowType);
        if (IsUnknownTypeImpl(FlowType->GetChild(), VisitedFlowTypes))
        {
            return true;
        }
        return false;
    }
    const CNormalType& NormalType = Type->GetNormalType();
    if (const CPointerType* PointerType = NormalType.AsNullable<CPointerType>())
    {
        // A pointer type is an unknown type if its value type is an unknown type.
        return IsUnknownTypeImpl(PointerType->NegativeValueType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(PointerType->PositiveValueType(), VisitedFlowTypes);
    }
    else if (const CReferenceType* ReferenceType = NormalType.AsNullable<CReferenceType>())
    {
        // A reference type is an unknown type if its value type is an unknown type.
        return IsUnknownTypeImpl(ReferenceType->NegativeValueType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(ReferenceType->PositiveValueType(), VisitedFlowTypes);
    }
    else if (const CArrayType* ArrayType = NormalType.AsNullable<CArrayType>())
    {
        // An array type is an unknown type if its element type is an unknown type.
        return IsUnknownTypeImpl(ArrayType->GetElementType(), VisitedFlowTypes);
    }
    else if (const CMapType* MapType = NormalType.AsNullable<CMapType>())
    {
        // An map type is an unknown type if either its key type or element type is an unknown type.
        return IsUnknownTypeImpl(MapType->GetKeyType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(MapType->GetValueType(), VisitedFlowTypes);
    }
    else if (const COptionType* OptionType = NormalType.AsNullable<COptionType>())
    {
        // An option type is an unknown type if its value type is an unknown type.
        return IsUnknownTypeImpl(OptionType->GetValueType(), VisitedFlowTypes);
    }
    else if (const CTupleType* TupleType = NormalType.AsNullable<CTupleType>())
    {
        // A tuple type is an unknown type if any of its elements is an unknown type.
        for (int32_t ParamIndex = 0; ParamIndex < TupleType->Num(); ++ParamIndex)
        {
            if (IsUnknownTypeImpl((*TupleType)[ParamIndex], VisitedFlowTypes))
            {
                return true;
            }
        }
        return false;
    }
    else if (const CFunctionType* FunctionType = NormalType.AsNullable<CFunctionType>())
    {
        // A function type is an unknown type if either its return or parameter type is an unknown type.
        return IsUnknownTypeImpl(&FunctionType->GetParamsType(), VisitedFlowTypes)
            || IsUnknownTypeImpl(&FunctionType->GetReturnType(), VisitedFlowTypes);
    }
    else
    {
        return NormalType.IsA<CUnknownType>();
    }
}
}

bool SemanticTypeUtils::IsUnknownType(const CTypeBase* Type)
{
    TSet<const CFlowType*> VisitedFlowTypes;
    return IsUnknownTypeImpl(Type, VisitedFlowTypes);
}

bool SemanticTypeUtils::IsAttributeType(const CTypeBase* Type)
{
    if (const CClass* Class = Type->GetNormalType().AsNullable<CClass>())
    {
        return Class->IsClass(*Type->GetProgram()._attributeClass);
    }
    else
    {
        return false;
    }
}

void SemanticTypeUtils::VisitAllDefinitions(const CTypeBase* Type, const CAstPackage* VisitorPackage, const TFunction<void(const CDefinition&,const CSymbol&)>& Functor)
{
    const CNormalType& NormalType = Type->GetNormalType();
    switch (NormalType.GetKind())
    {
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Persistable:
        return;

    case ETypeKind::Interface:
    {
        const CInterface& Interface = NormalType.AsChecked<CInterface>();
        Functor(*Interface._GeneralizedInterface, Interface.GetName());
        if (&Interface == Interface._GeneralizedInterface)
        {
            return;
        }
        if (VisitorPackage && !VerseFN::UploadedAtFNVersion::DetectInaccessibleTypeArguments(VisitorPackage->_UploadedAtFNVersion))
        {
            return;
        }
        for (auto [TypeVariable, NegativeType, PositiveType] : Interface._TypeVariableSubstitutions)
        {
            VisitAllDefinitions(NegativeType, VisitorPackage, Functor);
            VisitAllDefinitions(PositiveType, VisitorPackage, Functor);
        }
        return;
    }

    case ETypeKind::Class:
    {
        const CClass& Class = NormalType.AsChecked<CClass>();
        Functor(*Class.Definition(), Class.Definition()->GetName());
        if (&Class == Class._GeneralizedClass)
        {
            return;
        }
        if (VisitorPackage && !VerseFN::UploadedAtFNVersion::DetectInaccessibleTypeArguments(VisitorPackage->_UploadedAtFNVersion))
        {
            return;
        }
        for (auto [TypeVariable, NegativeType, PositiveType] : Class._TypeVariableSubstitutions)
        {
            VisitAllDefinitions(NegativeType, VisitorPackage, Functor);
            VisitAllDefinitions(PositiveType, VisitorPackage, Functor);
        }
        return;
    }

    case ETypeKind::Variable:
    {
        const CTypeVariable& TypeVariable = NormalType.AsChecked<CTypeVariable>();
        if (VisitorPackage && !VerseFN::UploadedAtFNVersion::DetectInaccessibleTypeArguments(VisitorPackage->_UploadedAtFNVersion))
        {
            return;
        }
        if (!TypeVariable._NegativeTypeVariable)
        {
            return;
        }
        VisitAllDefinitions(TypeVariable.GetPositiveType(), VisitorPackage, Functor);
        return;
    }

    case ETypeKind::Module:
    case ETypeKind::Enumeration:
    {
        const CNominalType& NominalType = *NormalType.AsNominalType();
        Functor(*NominalType.Definition(), NominalType.Definition()->GetName());
        return;
    }

    case ETypeKind::Array:
        VisitAllDefinitions(NormalType.AsChecked<CArrayType>().GetElementType(), VisitorPackage, Functor);
        return;

    case ETypeKind::Generator:
        VisitAllDefinitions(NormalType.AsChecked<CGeneratorType>().GetElementType(), VisitorPackage, Functor);
        return;

    case ETypeKind::Map:
    {
        const CMapType& MapType = NormalType.AsChecked<CMapType>();
        VisitAllDefinitions(MapType.GetKeyType(), VisitorPackage, Functor);
        VisitAllDefinitions(MapType.GetValueType(), VisitorPackage, Functor);
        return;
    }

    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        VisitAllDefinitions(PointerType.NegativeValueType(), VisitorPackage, Functor);
        VisitAllDefinitions(PointerType.PositiveValueType(), VisitorPackage, Functor);
        return;
    }

    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        VisitAllDefinitions(ReferenceType.NegativeValueType(), VisitorPackage, Functor);
        VisitAllDefinitions(ReferenceType.PositiveValueType(), VisitorPackage, Functor);
        return;
    }

    case ETypeKind::Option:
        VisitAllDefinitions(NormalType.AsChecked<COptionType>().GetValueType(), VisitorPackage, Functor);
        return;

    case ETypeKind::Type:
    {
        const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
        if (TypeType.PositiveType() == &TypeType.GetProgram()._anyType)
        {
            // If `supertype`, visit the negative type.
            VisitAllDefinitions(TypeType.NegativeType(), VisitorPackage, Functor);
        }
        else
        {
            // Otherwise, assume either the negative type is `false` or the
            // negative equivalent of `PositiveType`.
            VisitAllDefinitions(TypeType.PositiveType(), VisitorPackage, Functor);
        }
        return;
    }

    case ETypeKind::Castable:
        VisitAllDefinitions(&NormalType.AsChecked<CCastableType>().SuperType(), VisitorPackage, Functor);
        return;

    case ETypeKind::Concrete:
        VisitAllDefinitions(&NormalType.AsChecked<CConcreteType>().SuperType(), VisitorPackage, Functor);
        return;

    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        for (const CTypeBase* ElementType : TupleType.GetElements())
        {
            VisitAllDefinitions(ElementType, VisitorPackage, Functor);
        }
        return;
    }

    case ETypeKind::Function:
    {
        const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
        VisitAllDefinitions(&FunctionType.GetParamsType(), VisitorPackage, Functor);
        VisitAllDefinitions(&FunctionType.GetReturnType(), VisitorPackage, Functor);
        for (const CTypeVariable* TypeVariable : FunctionType.GetTypeVariables())
        {
            if (VisitorPackage && !VerseFN::UploadedAtFNVersion::DetectInaccessibleTypeArguments(VisitorPackage->_UploadedAtFNVersion))
            {
                continue;
            }
            VisitAllDefinitions(TypeVariable, VisitorPackage, Functor);
        }
        return;
    }

    case ETypeKind::Named:
    {
        const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
        VisitAllDefinitions(NamedType.GetValueType(), VisitorPackage, Functor);
        return;
    }

    default:
        ULANG_UNREACHABLE();
    }
}

void SemanticTypeUtils::ForEachDataType(const CTypeBase* Type, const TFunction<void(const CTypeBase*)>& F)
{
    const CNormalType& NormalType = Type->GetNormalType();
    switch (NormalType.GetKind())
    {
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Type:
    case ETypeKind::Enumeration:
    case ETypeKind::Function:
    case ETypeKind::Variable:
    case ETypeKind::Persistable:
        return;

    case ETypeKind::Class:
    case ETypeKind::Module:
    case ETypeKind::Interface:
    {
        const CNominalType* NominalType = NormalType.AsNominalType();
        ULANG_ASSERTF(NominalType, "Failed to cast to NominalType.");
        const CLogicalScope* LogicalScope = NominalType->Definition()->DefinitionAsLogicalScopeNullable();
        ULANG_ASSERTF(LogicalScope, "Failed to cast to LogicalScope");
        for (const TSRef<CDefinition>& Definition : LogicalScope->GetDefinitions())
        {
            if (const CDataDefinition* DataDefinition = Definition->AsNullable<CDataDefinition>())
            {
                F(DataDefinition->GetType());
            }
        }
        return;
    }

    case ETypeKind::Array:
        F(NormalType.AsChecked<CArrayType>().GetElementType());
        return;

    case ETypeKind::Generator:
        F(NormalType.AsChecked<CGeneratorType>().GetElementType());
        return;

    case ETypeKind::Map:
    {
        const CMapType& MapType = NormalType.AsChecked<CMapType>();
        F(MapType.GetKeyType());
        F(MapType.GetValueType());
        return;
    }

    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        F(PointerType.NegativeValueType());
        F(PointerType.PositiveValueType());
        return;
    }

    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        F(ReferenceType.NegativeValueType());
        F(ReferenceType.PositiveValueType());
        return;
    }

    case ETypeKind::Option:
        F(NormalType.AsChecked<COptionType>().GetValueType());
        return;

    case ETypeKind::Castable:
        F(&NormalType.AsChecked<CCastableType>().SuperType());
        return;

    case ETypeKind::Concrete:
        F(&NormalType.AsChecked<CConcreteType>().SuperType());
        return;

    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        for (const CTypeBase* ElementType : TupleType.GetElements())
        {
            F(ElementType);
        }
        return;
    }

    case ETypeKind::Named:
    {
        const CNamedType& NamedType = NormalType.AsChecked<CNamedType>();
        F(NamedType.GetValueType());
        return;
    }

    default:
        ULANG_UNREACHABLE();
    }
}

static void ForEachDataTypeRecursiveImpl(const CTypeBase* Type, const TFunction<void(const CTypeBase*)>& F, TArray<const CTypeBase*>& Visited)
{
    if (Visited.Contains(Type))
    {
        return;
    }
    Visited.Add(Type);
    F(Type);
    SemanticTypeUtils::ForEachDataType(Type, [&](const CTypeBase* DataType)
    {
        ForEachDataTypeRecursiveImpl(DataType, F, Visited);
    });
}

void SemanticTypeUtils::ForEachDataTypeRecursive(const CTypeBase* Type, const TFunction<void(const CTypeBase*)>& F)
{
    TArray<const CTypeBase*> Visited;
    ForEachDataTypeRecursiveImpl(Type, F, Visited);
}

namespace
{
SemanticTypeUtils::EIsEditable Combine(SemanticTypeUtils::EIsEditable lhs, SemanticTypeUtils::EIsEditable rhs)
{
    return (lhs != SemanticTypeUtils::EIsEditable::Yes ? lhs : rhs);
}
}

const char* SemanticTypeUtils::IsEditableToCMessage(EIsEditable IsEditable)
{
    switch (IsEditable)
    {
    case SemanticTypeUtils::EIsEditable::CastableTypesNotEditable:
        return "The editable attribute is not supported for types that require the castable attribute.";
    case SemanticTypeUtils::EIsEditable::NotEditableType:
        return "The editable attribute is not supported for data definitions of this type.";
    case SemanticTypeUtils::EIsEditable::MissingConcrete:
        return "The editable attribute is not supported for structs that aren't concrete.";
    case SemanticTypeUtils::EIsEditable::Yes:
        return "The editable attribute can be used here.";
    // @HACK: corresponds to hack in SemanticTypeUtils::IsEditableClassType
    case SemanticTypeUtils::EIsEditable::ClassifiableSubsetParametricArgumentInvalid:
        return "The `classifiable_subset` class currently only supports `tag` as parametric argument if marked as @editable.";
    default:
        ULANG_UNREACHABLE();
    }
}

bool SemanticTypeUtils::IsMessageType(const CNormalType& NormalType)
{
    const CClassDefinition* _messageClass = NormalType.GetProgram()._message_class.Get();
    ULANG_ASSERTF(_messageClass, "Verse message type is missing!");

    return &NormalType == &_messageClass->GetNormalType();
}

bool SemanticTypeUtils::IsLeaderboardType(const CNormalType& NormalType)
{
    if (const CClassDefinition* _leaderboardClass = NormalType.GetProgram().GetLeaderboardClassDefinition())
    {
        return &NormalType == &_leaderboardClass->GetNormalType();
    }

    return false;
}

bool SemanticTypeUtils::IsAgentTypeExclusive(const CTypeBase* Type)
{
    const CClass* TypeAsClass = Type ? Type->GetNormalType().AsNullable<CClass>() : nullptr;
    const CClass* TypeAsPositiveClass = TypeAsClass ? &AsPositiveClass(*TypeAsClass) : nullptr;
    return TypeAsPositiveClass && (TypeAsPositiveClass == Type->GetNormalType().GetProgram()._agent_class.Get());
}

SemanticTypeUtils::EIsEditable SemanticTypeUtils::IsEditableType(const uLang::CTypeBase* Type, const CAstPackage* ContextPackage)
{
    const CNormalType& NormalType = Type->GetNormalType();

    // SOL-7338 - We can't support @editable for castable_subtypes until we can enforce the castability
    // constraint in either the UnrealEd chooser or the content cooker.
    if (const CTypeType* TypeType = NormalType.AsNullable<CTypeType>(); TypeType && TypeType->PositiveType()->GetNormalType().IsA<CCastableType>())
    {
        return EIsEditable::CastableTypesNotEditable;
    }
    if (NormalType.GetKind() == Cases<
        ETypeKind::Logic,
        //		ETypeKind::Char8,   Not supported since it would show up as unsigned 8-bit integer
        //		ETypeKind::Char32,  Not supported since it would show up as unsigned 32-bit integer
        ETypeKind::Int,
        ETypeKind::Float,
        ETypeKind::Enumeration>)
    {
        return EIsEditable::Yes;
    }
    else if (IsStringType(NormalType))
    {
        return EIsEditable::Yes;
    }
    else if (const CArrayType* ArrayType = NormalType.AsNullable<CArrayType>())
    {
        return IsEditableType(ArrayType->GetElementType(), ContextPackage);
    }
    else if (const CMapType* MapType = NormalType.AsNullable<CMapType>())
    {
        return Combine(IsEditableType(MapType->GetKeyType(), ContextPackage),
            IsEditableType(MapType->GetValueType(), ContextPackage));
    }
    else if (const CPointerType* PointerType = NormalType.AsNullable<CPointerType>())
    {
        return Combine(IsEditableType(PointerType->PositiveValueType(), ContextPackage),
            IsEditableType(PointerType->NegativeValueType(), ContextPackage));
    }
    else if (const CTypeType* TypeType = NormalType.AsNullable<CTypeType>())
    {
        const CNormalType* NormalPositiveType = &TypeType->PositiveType()->GetNormalType();
        if (NormalPositiveType->GetKind() == ETypeKind::Any)
        {
            // We don't allow the type of `any` as this doesn't have clear use cases as an @editable yet (ie: identifier:type is not @editable)
            return EIsEditable::NotEditableType;
        }

        // Is this a subtype?
        if (TypeType->NegativeType()->GetNormalType().IsA<CFalseType>()
            && !TypeType->PositiveType()->GetNormalType().IsA<CAnyType>())
        {
            if (VerseFN::UploadedAtFNVersion::DisallowNonClassEditableSubtypes(ContextPackage->_UploadedAtFNVersion))
            {
                // We don't allow editable subtypes other than classes
                return IsEditableClassType(TypeType->PositiveType());
            }
            else
            {
                // COMPATIBILITY - kept around for compatibility with pre-3400 versions- see SOL-7508
                // We don't allow the type of `any` as this doesn't have clear use cases as an @editable yet (ie: identifier:type is not @editable)
                return TypeType->PositiveType()->GetNormalType().GetKind() != ETypeKind::Any ? EIsEditable::Yes : EIsEditable::NotEditableType;
            }
        }

        return EIsEditable::Yes;
    }
    else if (IsMessageType(NormalType))
    {
        return EIsEditable::Yes;
    }
    else if (const CTypeVariable* TypeVariable = NormalType.AsNullable<CTypeVariable>())
    {
        return IsEditableType(TypeVariable->GetPositiveType()->GetNormalType().AsChecked<CTypeType>().PositiveType(), ContextPackage);
    }
    else if (const CTupleType* Tuple = NormalType.AsNullable<CTupleType>())
    {
        for (const CTypeBase* ElementType : Tuple->GetElements())
        {
            EIsEditable Result = IsEditableType(ElementType, ContextPackage);
            if (Result != EIsEditable::Yes)
            {
                return Result;
            }
        }
        return EIsEditable::Yes;
    }
    else if (const COptionType* OptionType = NormalType.AsNullable<COptionType>())
    {
        // Optional types are allowed-editable if their internal value type is allowed.
        if (const uLang::CTypeBase* ValueType = OptionType->GetValueType())
        {
            return IsEditableType(ValueType, ContextPackage);
        }
    }

    return IsEditableClassType(Type);
}

SemanticTypeUtils::EIsEditable SemanticTypeUtils::IsEditableClassType(const uLang::CTypeBase* Type)
{
    using namespace uLang;

    const CNormalType& NormalType = Type->GetNormalType();
    if (const CClass* Class = NormalType.AsNullable<CClass>())
    {
        // @HACK: https://jira.it.epicgames.com/browse/SOL-3577, https://jira.it.epicgames.com/browse/SOL-8017
        // @editable classifiable_subset only supports tags as the parametric type atm.
        // At some point the editor should have better knowledge about verse types, and specifically the arguments to parametric verse types.
        // When that happens this hack should be removed and classifiable_subset should support any @editable type that can be instantiated at editor time.
        if (Class->GetScopePath('/', EPathMode::Default) == "Verse.org/Verse/classifiable_subset")
        {
            for (const STypeVariableSubstitution& Substitution : Class->_TypeVariableSubstitutions)
            {
                if (Substitution._TypeVariable->_ExplicitParam && Substitution._TypeVariable->_NegativeTypeVariable && Substitution._PositiveType)
                {
                    const CNormalType& SubstitutionNormalType = Substitution._PositiveType->GetNormalType();
                    const CClass* SubstitutionNormalClass = SubstitutionNormalType.IsA<CClass>() ? &SubstitutionNormalType.AsChecked<CClass>() : nullptr;

                    if (SubstitutionNormalClass && SubstitutionNormalClass->GetScopePath('/', EPathMode::Default) == "Verse.org/Simulation/Tags/tag")
                    {
                        continue;
                    }

                    return EIsEditable::ClassifiableSubsetParametricArgumentInvalid;
                }
            }
        }

        return (Class->IsStruct() && !Class->IsExplicitlyConcrete()) ? EIsEditable::MissingConcrete : EIsEditable::Yes;
    }
    else if (NormalType.IsA<CInterface>())
    {
        return EIsEditable::Yes;
    }
    else if (const CTypeVariable* TypeVariable = NormalType.AsNullable<CTypeVariable>())
    {
        return IsEditableClassType(TypeVariable->GetPositiveType()->GetNormalType().AsChecked<CTypeType>().PositiveType());
    }
	else if (const CConcreteType* ConcreteVariable = NormalType.AsNullable<CConcreteType>())
	{
		return IsEditableClassType(&ConcreteVariable->SuperType().GetNormalType());
	}

    return EIsEditable::NotEditableType;
}

const CTypeBase* SemanticTypeUtils::RemovePointer(const CTypeBase* Type, ETypePolarity Polarity)
{
    if (!Type)
    {
        return nullptr;
    }

    if (auto PointerType = Type->GetNormalType().AsNullable<CPointerType>())
    {
        Type = Polarity == ETypePolarity::Negative
            ? PointerType->NegativeValueType() : PointerType->PositiveValueType();
    }
    return Type;
}

const CTypeBase* SemanticTypeUtils::RemoveReference(const CTypeBase* Type, ETypePolarity Polarity)
{
    if (!Type)
    {
        return nullptr;
    }

    if (auto RefType = Type->GetNormalType().AsNullable<CReferenceType>())
    {
        Type = Polarity == ETypePolarity::Negative
            ? RefType->NegativeValueType() : RefType->PositiveValueType();
    }
    return Type;
}

const CNormalType& SemanticTypeUtils::StripVariableAndConstraints(const CNormalType& Type)
{
    ETypeConstraintFlags ConstraintFlags = ETypeConstraintFlags::None;
    return StripVariableAndConstraints(Type, ConstraintFlags);
}

const CNormalType& SemanticTypeUtils::StripVariableAndConstraints(const CNormalType& Type, ETypeConstraintFlags& OutConstraintFlags)
{
    const CNormalType* I = &Type;
    TSet<const CTypeVariable*> VisitedTypeVariables;
    for (;;)
    {
        if (const CTypeVariable* TypeVariable = I->AsNullable<CTypeVariable>())
        {
            if (VisitedTypeVariables.Contains(TypeVariable))
            {
                return Type.GetProgram()._anyType;
            }
            VisitedTypeVariables.Insert(TypeVariable);
            const CTypeType& TypeType = TypeVariable->GetPositiveType()->GetNormalType().AsChecked<CTypeType>();
            I = &TypeType.PositiveType()->GetNormalType();
        }
        else if (const CCastableType* CastableType = I->AsNullable<CCastableType>())
        {
            I = &CastableType->SuperType().GetNormalType();
            OutConstraintFlags |= ETypeConstraintFlags::Castable;
        }
        else if (const CConcreteType* ConcreteType = I->AsNullable<CConcreteType>())
        {
            I = &ConcreteType->SuperType().GetNormalType();
            OutConstraintFlags |= ETypeConstraintFlags::Concrete;
        }
        else
        {
            break;
        }
    }
    return *I;
}

CClassDefinition* SemanticTypeUtils::EnclosingClassOfDataDefinition(const CDataDefinition* Def)
{
    if (!Def)
    {
        return nullptr;
    }
    if (CDefinition* MaybeClass = const_cast<CDefinition*>(Def->_EnclosingScope.ScopeAsDefinition()))
    {
        if (auto* ClassDef = MaybeClass->AsNullable<CClassDefinition>();
            ClassDef && ClassDef->_StructOrClass == EStructOrClass::Class)
        {
            return ClassDef;
        }
    }
    return nullptr;
}

const CTypeBase* SemanticTypeUtils::DecayReference(const CTypeBase* Type)
{
    if (!Type)
    {
        return nullptr;
    }
    return DecayReference(*Type);
}

const CTypeBase* SemanticTypeUtils::DecayReference(const CTypeBase& Type)
{
    const CReferenceType* ReferenceType = Type.GetNormalType().AsNullable<CReferenceType>();
    if (!ReferenceType)
    {
        return &Type;
    }
    return ReferenceType->PositiveValueType();
}

void SemanticTypeUtils::FillTypeVariablePolaritiesImpl(
    const CTypeBase* Type,
    ETypePolarity Polarity,
    STypeVariablePolarities& TypeVariablePolarities,
    TArray<SNormalTypePolarity>& Visited)
{
    const CNormalType& NormalType = Type->GetNormalType();
    if (auto Last = Visited.end(); uLang::Find(Visited.begin(), Last, SNormalTypePolarity{&NormalType, Polarity}) != Last)
    {
        return;
    }
    Visited.Add({&NormalType, Polarity});
    switch (NormalType.GetKind())
    {
    case ETypeKind::Unknown:
    case ETypeKind::False:
    case ETypeKind::True:
    case ETypeKind::Void:
    case ETypeKind::Any:
    case ETypeKind::Comparable:
    case ETypeKind::Persistable:
    case ETypeKind::Logic:
    case ETypeKind::Int:
    case ETypeKind::Rational:
    case ETypeKind::Float:
    case ETypeKind::Char8:
    case ETypeKind::Char32:
    case ETypeKind::Path:
    case ETypeKind::Range:
    case ETypeKind::Module:
    case ETypeKind::Enumeration:
        break;
    case ETypeKind::Class:
    {
        const CClass& Class = NormalType.AsChecked<CClass>();
        if (Class.GetParentScope()->GetKind() != CScope::EKind::Function)
        {
            break;
        }
        if (const CClass* Superclass = Class._Superclass)
        {
            FillTypeVariablePolaritiesImpl(Superclass, Polarity, TypeVariablePolarities, Visited);
        }
        for (const CInterface* SuperInterface : Class._SuperInterfaces)
        {
            FillTypeVariablePolaritiesImpl(SuperInterface, Polarity, TypeVariablePolarities, Visited);
        }
        for (CDataDefinition* DataMember : Class.GetDefinitionsOfKind<CDataDefinition>())
        {
            FillTypeVariablePolaritiesImpl(
                DataMember->GetType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
        }
        for (CFunction* Function : Class.GetDefinitionsOfKind<CFunction>())
        {
            FillTypeVariablePolaritiesImpl(
                Function->_Signature.GetFunctionType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
        }
        break;
    }
    case ETypeKind::Array:
        FillTypeVariablePolaritiesImpl(
            NormalType.AsChecked<CArrayType>().GetElementType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    case ETypeKind::Generator:
        FillTypeVariablePolaritiesImpl(
            NormalType.AsChecked<CGeneratorType>().GetElementType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    case ETypeKind::Map:
    {
        const CMapType& MapType = NormalType.AsChecked<CMapType>();
        FillTypeVariablePolaritiesImpl(
            MapType.GetKeyType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        FillTypeVariablePolaritiesImpl(
            MapType.GetValueType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Pointer:
    {
        const CPointerType& PointerType = NormalType.AsChecked<CPointerType>();
        FillTypeVariablePolaritiesImpl(
            PointerType.NegativeValueType(),
            FlipPolarity(Polarity),
            TypeVariablePolarities,
            Visited);
        FillTypeVariablePolaritiesImpl(
            PointerType.PositiveValueType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Reference:
    {
        const CReferenceType& ReferenceType = NormalType.AsChecked<CReferenceType>();
        FillTypeVariablePolaritiesImpl(
            ReferenceType.NegativeValueType(),
            FlipPolarity(Polarity),
            TypeVariablePolarities,
            Visited);
        FillTypeVariablePolaritiesImpl(
            ReferenceType.PositiveValueType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Option:
        FillTypeVariablePolaritiesImpl(
            NormalType.AsChecked<COptionType>().GetValueType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    case ETypeKind::Type:
    {
        const CTypeType& TypeType = NormalType.AsChecked<CTypeType>();
        FillTypeVariablePolaritiesImpl(
            TypeType.NegativeType(),
            FlipPolarity(Polarity),
            TypeVariablePolarities,
            Visited);
        FillTypeVariablePolaritiesImpl(
            TypeType.PositiveType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Castable:
    {
        const CCastableType& CastableType = NormalType.AsChecked<CCastableType>();
        FillTypeVariablePolaritiesImpl(
            &CastableType.SuperType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Concrete:
    {
        const CConcreteType& ConcreteType = NormalType.AsChecked<CConcreteType>();
        FillTypeVariablePolaritiesImpl(
            &ConcreteType.SuperType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Interface:
    {
        const CInterface& Interface = NormalType.AsChecked<CInterface>();
        if (Interface.GetParentScope()->GetKind() != CScope::EKind::Function)
        {
            break;
        }
        for (const CInterface* SuperInterface : Interface._SuperInterfaces)
        {
            FillTypeVariablePolaritiesImpl(SuperInterface, Polarity, TypeVariablePolarities, Visited);
        }
        for (const CFunction* Function : Interface.GetDefinitionsOfKind<CFunction>())
        {
            FillTypeVariablePolaritiesImpl(
                Function->_Signature.GetFunctionType(),
                Polarity,
                TypeVariablePolarities,
                Visited);
        }
        break;
    }
    case ETypeKind::Tuple:
    {
        const CTupleType& TupleType = NormalType.AsChecked<CTupleType>();
        for (const CTypeBase* ElementType : TupleType.GetElements())
        {
            FillTypeVariablePolaritiesImpl(
                ElementType,
                Polarity,
                TypeVariablePolarities,
                Visited);
        }
        break;
    }
    case ETypeKind::Function:
    {
        const CFunctionType& FunctionType = NormalType.AsChecked<CFunctionType>();
        FillTypeVariablePolaritiesImpl(
            &FunctionType.GetParamsType(),
            FlipPolarity(Polarity),
            TypeVariablePolarities,
            Visited);
        FillTypeVariablePolaritiesImpl(
            &FunctionType.GetReturnType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    }
    case ETypeKind::Variable:
    {
        const CTypeVariable& TypeVariable = NormalType.AsChecked<CTypeVariable>();
        TypeVariablePolarities.Add({ &TypeVariable, Polarity });
        // Use the same type that instantiation uses (and not `GetType()`).
        const CTypeBase* NegativeType = TypeVariable.GetNegativeType();
        if (!NegativeType)
        {
            break;
        }
        const CTypeType* NegativeTypeType = NegativeType->GetNormalType().AsNullable<CTypeType>();
        if (!NegativeTypeType)
        {
            break;
        }
        if (Polarity == ETypePolarity::Negative)
        {
            // Recurse with the negative type, i.e. the negative `type`'s
            // `PositiveType()`, if `Polarity` is negative.
            FillTypeVariablePolaritiesImpl(
                NegativeTypeType->PositiveType(),
                ETypePolarity::Negative,
                TypeVariablePolarities,
                Visited);
        }
        else
        {
            // Otherwise, recurse with the positive type, i.e. the negative
            // `type`'s `NegativeType()`.
            ULANG_ASSERT(Polarity == ETypePolarity::Positive);
            FillTypeVariablePolaritiesImpl(
                NegativeTypeType->NegativeType(),
                ETypePolarity::Positive,
                TypeVariablePolarities,
                Visited);
        }
        break;
    }
    case ETypeKind::Named:
        FillTypeVariablePolaritiesImpl(
            NormalType.AsChecked<CNamedType>().GetValueType(),
            Polarity,
            TypeVariablePolarities,
            Visited);
        break;
    default:
        ULANG_UNREACHABLE();
    }
}

void SemanticTypeUtils::FillTypeVariablePolarities(
    const CTypeBase* Type,
    ETypePolarity Polarity,
    STypeVariablePolarities& TypeVariablePolarities)
{
    TArray<SNormalTypePolarity> Visited;
    FillTypeVariablePolaritiesImpl(Type, Polarity, TypeVariablePolarities, Visited);
}
} // namespace uLang
