// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Common.h"
#include "uLang/Semantics/QualifierUtils.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/ModuleAlias.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/ScopedAccessLevelType.h"

namespace uLang
{
using VstSharedRef = TSRef<Verse::Vst::Node>;

bool HasDefinition(const CExprIdentifierBase& IdentifierBase)
{
    // local and unresolved are not supposed to have a definition
    // TODO: local will be qualified in the future
    return IdentifierBase.GetNodeType() == Cases<EAstNodeType::Identifier_Local, EAstNodeType::Identifier_Unresolved>
        || static_cast<const CExprIdentifierBase&>(IdentifierBase).IdentifierDefinition();
}

class CFindResolvedIdentifiersWithoutDefinitionsAstVisitor final : public SAstVisitor
{
public:
    virtual void Visit(const char* /*FieldName*/, CAstNode& AstNode) override
    {
        if (const CExprIdentifierBase* IdentifierBase = AstNode.AsIdentifierBase(); IdentifierBase && !HasDefinition(*IdentifierBase))
        {
            _InvalidIdentifiers.Add(AstNode.AsExpression());
        }
    }

    virtual void VisitElement(CAstNode& AstNode) override
    {
        Visit(AstNode);
    }

    void Visit(const CAstNode& AstNode)
    {
        AstNode.VisitImmediates(*this);
        AstNode.VisitChildren(*this);
    }

    TArray<const CExpressionBase*> GetInvalidIdentifiers() const
    {
        return _InvalidIdentifiers;
    }

private:
    TArray<const CExpressionBase*> _InvalidIdentifiers;
};

TArray<const CExpressionBase*> FindResolvedIdentifiersWithoutDefinitions(const TSRef<CSemanticProgram>& /*Program*/, const CAstNode& RootNode)
{
    CFindResolvedIdentifiersWithoutDefinitionsAstVisitor Visitor;
    Visitor.Visit(RootNode);
    return Visitor.GetInvalidIdentifiers();
}

class CFindUnresolvedIdentifiersAstVisitor final : public SAstVisitor
{
public:
    virtual void Visit(const char* /*FieldName*/, CAstNode& AstNode) override
    {
        if (AstNode.GetNodeType() == EAstNodeType::Identifier_Unresolved)
        {
            _UnresolvedIdentifiers.Add(AstNode.AsExpression());
        }
    }

    virtual void VisitElement(CAstNode& AstNode) override
    {
        Visit(AstNode);
    }

    void Visit(const CAstNode& AstNode)
    {
        AstNode.VisitImmediates(*this);
        AstNode.VisitChildren(*this);
    }

    TArray<const CExpressionBase*> GetUnresolvedIdentifiers() const
    {
        return _UnresolvedIdentifiers;
    }

private:
    TArray<const CExpressionBase*> _UnresolvedIdentifiers;
};

TArray<const CExpressionBase*> FindUnresolvedIdentifiers(const TSRef<CSemanticProgram>& Program, const CAstNode& RootNode)
{
    CFindUnresolvedIdentifiersAstVisitor Visitor;
    Visitor.Visit(RootNode);
    return Visitor.GetUnresolvedIdentifiers();
}

struct SQualifierResult
{
    enum class EStatus
    {
        Undefined, Qualified, NotRequired, Failed
    };

    static SQualifierResult Qualified(const CUTF8StringView Str)
    {
        return { EStatus::Qualified, Str };
    }

    static SQualifierResult NotRequired()
    {
        return {EStatus::NotRequired, {}};
    }

    static SQualifierResult Failed()
    {
        return {EStatus::Failed, {}};
    }

    EStatus Status = EStatus::Failed;
    CUTF8String String;
};

// Arguments can have a lot of different shapes
// X:int, Y:int  CExprMakeTuple
// X:int         CExprDefinition
// X():int       CExprInvocation

enum class EContains { Local, Path, NotYet, No };

EContains ContainsExpr(const CExpressionBase* Argument, const CExprIdentifierUnresolved* Expr)
{
    if (Argument == Expr)
    {
        return EContains::Local;
    }
    // (X:int, ...)
    else if (const CExprMakeTuple* Args = AsNullable<CExprMakeTuple>(Argument))
    {
        for (const TSPtr<CExpressionBase>& Arg : Args->GetSubExprs())
        {
            EContains Contains = ContainsExpr(Arg, Expr);
            if (Contains != EContains::No)
            {
                return Contains;
            }
        }
    } 
    // X:int
    else if (auto Definition = AsNullable<CExprDefinition>(Argument))
    {
        if (Definition->GetName() == Expr->_Symbol)
        {
            return EContains::NotYet; // Should be Path, but the compiler isn't ready
        }
        return ContainsExpr(Definition->Element(), Expr);
    } 
    // X(...):int
    else if (auto Invocation = AsNullable<CExprInvocation>(Argument))
    {
        if (Invocation->GetCallee().Get() == Expr)
        {
            return EContains::Local;
        }
        EContains Contains = ContainsExpr(Invocation->GetArgument(), Expr);
        if (Contains != EContains::No)
        {
            return Contains;
        }
    }
    return EContains::No;
}

static SQualifierResult QualifierOf(const CExpressionBase* Expr, Verse::Vst::Identifier* VstIdentifier, const CSemanticProgram& Program)
{
    static auto GetScopePath = [](const CScope& Scope)
    {
        CUTF8String Result = Scope.GetScopePath('/', CScope::EPathMode::PrefixSeparator);
        return Result;
    };
    auto GetQualifierPath = [Expr, VstIdentifier](const SQualifier& Qualifier) -> SQualifierResult
    {
        switch (Qualifier._Type)
        {
        case SQualifier::EType::Local:
            return SQualifierResult::Qualified("local");
        case SQualifier::EType::LogicalScope:
        {
            return SQualifierResult::Qualified(GetScopePath(*Qualifier.GetLogicalScope()));
        }
        case SQualifier::EType::NominalType:
            {
                auto* Scope = Qualifier.GetNominalType()->Definition()->DefinitionAsLogicalScopeNullable();
                ULANG_ASSERT(Scope);
                return SQualifierResult::Qualified(GetScopePath(*Scope));
            }
        case SQualifier::EType::Unknown:
            return SQualifierResult::Failed();
        default:
            ULANG_UNREACHABLE();
        };
    };

    auto GetDefinitionQualifier = [&](const CDefinition& Definition) -> SQualifierResult
    {
        const CDefinition* PrototypeDefinition = Definition.GetBaseOverriddenDefinition().GetPrototypeDefinition();
        if (const CDataDefinition* DataDefinition = PrototypeDefinition->AsNullable<CDataDefinition>(); DataDefinition && DataDefinition->_bNamed)
        {
            return SQualifierResult::NotRequired(); // TODO: Should use the implicit qualifier, but the compiler can't handle that.
        }
        return GetQualifierPath(PrototypeDefinition->GetImplicitQualifier());
    };

    /* Global types are special treated at the moment, they aren't qualified. This will not be true forever. */
    static auto IsGlobalType = [](const CExprIdentifierTypeAlias* IdentifierTypeAlias) -> bool
    {
        const CTypeType* TypeType = IdentifierTypeAlias->_TypeAlias.GetTypeType();
        const CTypeBase* PositiveType = TypeType->PositiveType();
        if (PositiveType == TypeType->NegativeType()) {
            if (PositiveType->GetNormalType().GetKind() == Cases<ETypeKind::False, ETypeKind::True, ETypeKind::Void, ETypeKind::Any, ETypeKind::Comparable, ETypeKind::Logic, ETypeKind::Rational, ETypeKind::Char8, ETypeKind::Char32, ETypeKind::Path, ETypeKind::Range, ETypeKind::Persistable>)
            {
                return true;
            }
        }
        return false;
    };

    // TODO This is a terrible hack, maybe (:super) should be treated the same as (:local) and not show up here. 
    if (VstIdentifier->GetSourceText() == "super") {
       return SQualifierResult::NotRequired();
    }

    switch (Expr->GetNodeType())
    {
    case EAstNodeType::Identifier_Function:
    {
        // TODO: should this be here, or in the implementation of "GetImplicitQualifier"?
        auto Fun = AsNullable<CExprIdentifierFunction>(Expr);
        if (Fun->_Function._ExtensionFieldAccessorKind == EExtensionFieldAccessorKind::ExtensionMethod)
        {
            return SQualifierResult::NotRequired();
        }
        return GetDefinitionQualifier(Fun->_Function);
    }

    case EAstNodeType::Identifier_Enum:
        return GetDefinitionQualifier(*AsNullable<CExprEnumerationType>(Expr)->GetEnumeration(Program));

    case EAstNodeType::Identifier_Module:
        return GetDefinitionQualifier(*AsNullable<CExprIdentifierModule>(Expr)->GetModule(Program));

    case EAstNodeType::Identifier_ModuleAlias:
        return GetDefinitionQualifier(AsNullable<CExprIdentifierModuleAlias>(Expr)->_ModuleAlias);

    case EAstNodeType::Identifier_Interface:
        return GetDefinitionQualifier(*AsNullable<CExprInterfaceType>(Expr)->GetInterface(Program));

    case EAstNodeType::Identifier_Class:
        return GetDefinitionQualifier(*AsNullable<CExprIdentifierClass>(Expr)->GetClass(Program)->_Definition);

    case EAstNodeType::Identifier_TypeAlias:
    {
        // TODO: Add qualification for hardcoded types also.
        if (const CExprIdentifierTypeAlias* IdentifierTypeAlias = AsNullable<CExprIdentifierTypeAlias>(Expr)) {
            if (IsGlobalType(IdentifierTypeAlias))
            {
                return SQualifierResult::NotRequired();
            }
        }
        return GetDefinitionQualifier(AsNullable<CExprIdentifierTypeAlias>(Expr)->_TypeAlias);
    }
    case EAstNodeType::Identifier_Data:
        // TODO: The compiler can't handle type{x:int where (local:)x >= 0}
        if (const CExprIdentifierData* IdentifierData = AsNullable<CExprIdentifierData>(Expr)) 
        {
            if (IdentifierData->_DataDefinition._EnclosingScope.GetKind() == CScope::EKind::Type) 
            {
                return SQualifierResult::NotRequired();
            }
        }
        return GetDefinitionQualifier(AsNullable<CExprIdentifierData>(Expr)->_DataDefinition);

    case EAstNodeType::Identifier_Self:
    case EAstNodeType::Identifier_BuiltInMacro:
    case EAstNodeType::Literal_Logic:
    case EAstNodeType::Invoke_MakeOption: 
        // TODO: sus; need to see exactly what MakeOption can do,
        //       and whether or not it's covered by "Unresolved" below.
        // TODO: these types of identifiers can't be hand-qualified at the moment,
        //       and their use as the LHS of definitions is forbidden by the semantic analyzer.
        return SQualifierResult::NotRequired();

    case EAstNodeType::Identifier_TypeVariable:
        return GetDefinitionQualifier(AsNullable<CExprIdentifierTypeVariable>(Expr)->_TypeVariable);

    case EAstNodeType::Identifier_Unresolved:
        if (VstIdentifier->GetSourceText() == "_")
        {
            return SQualifierResult::NotRequired();
        }
        for (const Verse::Vst::Node* It = VstIdentifier; It; It = It->GetParent())
        {
            auto ItAst = It->GetMappedAstNode();
            if (!ItAst)
            {
                continue;
            }
            if (ItAst->GetNodeType() == EAstNodeType::Context_Package) 
            {
                auto Package = static_cast<const CAstPackage*>(ItAst);
                return  SQualifierResult::Qualified(*Package->_VersePath);
            }
            auto ItExpr = ItAst->AsExpression();
            if (AsNullable<CExprIdentifierUnresolved>(ItExpr))
            {
                continue; // Still not enough information
            }
            if (auto MakeNamed = AsNullable<CExprMakeNamed>(ItExpr))
            {
                if (MakeNamed->GetNameIdentifier().Get() == Expr)
                {
                    // TODO: This is not true, but how to qualify named parameters is not resolved yet.
                    return SQualifierResult::NotRequired();
                }
                continue;
            }
            if (AsNullable<CExprInvocation>(ItExpr))
            {
                continue; // TODO: Is this due to processing an extension method? Write test and verify.
            }
            if (AsNullable<CExprWhere>(ItExpr))
            {
                return SQualifierResult::Qualified("local");
            }
            if (auto Def = AsNullable<CExprDefinition>(ItExpr))
            {
                if (Def->ValueDomain())
                {
                    continue;
                }
                ULANG_ASSERT(Def->Value());
                if (auto Module = AsNullable<CExprModuleDefinition>(Def->Value()))
                {
                    return GetDefinitionQualifier(*Module->_SemanticModule->GetModule());
                }
                else if (auto Class = AsNullable<CExprClassDefinition>(Def->Value()))
                {
                    return GetDefinitionQualifier(*Class->_Class._Definition);
                }
                else if (auto Interface = AsNullable<CExprInterfaceDefinition>(Def->Value()))
                {
                    return GetDefinitionQualifier(Interface->_Interface);
                }
                else if (auto Enum = AsNullable<CExprEnumDefinition>(Def->Value()))
                {
                    return GetDefinitionQualifier(Enum->_Enum);
                }
                else if (auto Scoped = AsNullable<CExprScopedAccessLevelDefinition>(Def->Value()))
                {
                    return GetDefinitionQualifier(*Scoped->_AccessLevelDefinition);
                }

                return SQualifierResult::Failed();
            }

            if (auto ClassDef = AsNullable<CExprClassDefinition>(ItExpr))
            {
                return  GetQualifierPath(ClassDef->_Class.AsQualifier());
            }

            if (auto DataDef = AsNullable<CExprDataDefinition>(ItExpr))
            {
                if (DataDef->Element() == Expr)
                {
                    // TODO: The compiler can't handle type{(local:)x:int where x >= 0}
                    if (DataDef->_DataMember->_EnclosingScope.GetKind() == CScope::EKind::Type)
                    {
                        return SQualifierResult::NotRequired();
                    }
                    return GetDefinitionQualifier(*DataDef->_DataMember);
                }
                else if (auto Var = AsNullable<CExprVar>(DataDef->Element()))
                {
                    if (Var->Operand() == Expr)
                    {
                        return GetDefinitionQualifier(*DataDef->_DataMember);
                    }
                }
                return SQualifierResult::Failed();
            }

            if (auto FuncDef = AsNullable<CExprFunctionDefinition>(ItExpr))
            {
                auto Invocation = AsNullable<CExprInvocation>(FuncDef->Element());
                if (!Invocation)
                {
                    return SQualifierResult::Failed();
                }
                if (Invocation->GetCallee() == Expr)
                {
                    return GetDefinitionQualifier(*FuncDef->_Function);
                }
                else if (EContains Contains = ContainsExpr(Invocation->GetArgument(), AsNullable<CExprIdentifierUnresolved>(Expr)); Contains != EContains::No)
                {
                    switch (Contains)
                    {
                    case EContains::Local: return SQualifierResult::Qualified("local");
                    case EContains::Path: return SQualifierResult::Failed();// Need to implement for qualified named parameters
                    case EContains::NotYet: return SQualifierResult::NotRequired();
                    case EContains::No: return SQualifierResult::Failed();
                    }
                }
                return SQualifierResult::Failed();
            }

            if (auto ModuleDef = AsNullable<CExprModuleDefinition>(ItExpr))
            {
                return GetQualifierPath(ModuleDef->_SemanticModule->GetModule()->AsQualifier());
            }
            if (auto TypeAliasDef = AsNullable<CExprTypeAliasDefinition>(ItExpr))
            {
                return  GetDefinitionQualifier(*TypeAliasDef->_TypeAlias);
            }
        }
        return SQualifierResult::Failed();

    case EAstNodeType::Invoke_Invocation:
    case EAstNodeType::Literal_Enum:
        // nb: these AST nodes either contain identifiers, or always come with context (eg. MyEnum.SomeCase)
        //     and can be ignored
        return SQualifierResult::NotRequired();

    case EAstNodeType::Invoke_ReferenceToValue:
        return QualifierOf(AsNullable<CExprReferenceToValue>(Expr)->Operand(), VstIdentifier, Program);

    case EAstNodeType::Invoke_PointerToReference:
        return QualifierOf(AsNullable<CExprPointerToReference>(Expr)->Operand(), VstIdentifier, Program);

    case EAstNodeType::Error_:
    case EAstNodeType::Placeholder_:
    case EAstNodeType::External:
    case EAstNodeType::PathPlusSymbol:
    case EAstNodeType::Literal_Number:
    case EAstNodeType::Literal_Char:
    case EAstNodeType::Literal_String:
    case EAstNodeType::Literal_Path:
    case EAstNodeType::Literal_Type:
    case EAstNodeType::Literal_Function:
    case EAstNodeType::Identifier_OverloadedFunction:
    case EAstNodeType::Identifier_Local:
    case EAstNodeType::Definition:
    case EAstNodeType::MacroCall:
    case EAstNodeType::Invoke_UnaryArithmetic:
    case EAstNodeType::Invoke_BinaryArithmetic:
    case EAstNodeType::Invoke_ShortCircuitAnd:
    case EAstNodeType::Invoke_ShortCircuitOr:
    case EAstNodeType::Invoke_LogicalNot:
    case EAstNodeType::Invoke_Comparison:
    case EAstNodeType::Invoke_QueryValue:
    case EAstNodeType::Invoke_MakeArray:
    case EAstNodeType::Invoke_MakeMap:
    case EAstNodeType::Invoke_MakeTuple:
    case EAstNodeType::Invoke_TupleElement:
    case EAstNodeType::Invoke_MakeRange:
    case EAstNodeType::Invoke_Type:
    case EAstNodeType::Invoke_Set:
    case EAstNodeType::Invoke_NewPointer:
    case EAstNodeType::Assignment:
    case EAstNodeType::Invoke_ArrayFormer:
    case EAstNodeType::Invoke_GeneratorFormer:
    case EAstNodeType::Invoke_MapFormer:
    case EAstNodeType::Invoke_Subtype:
    case EAstNodeType::Invoke_TupleType:
    case EAstNodeType::Invoke_Arrow:
    case EAstNodeType::Invoke_ArchetypeInstantiation:
    case EAstNodeType::Flow_CodeBlock:
    case EAstNodeType::Flow_Let:
    case EAstNodeType::Flow_Defer:
    case EAstNodeType::Flow_If:
    case EAstNodeType::Flow_Iteration:
    case EAstNodeType::Flow_Loop:
    case EAstNodeType::Flow_Break:
    case EAstNodeType::Flow_Return:
    case EAstNodeType::Flow_ProfileBlock:
    case EAstNodeType::Ir_For:
    case EAstNodeType::Ir_ForBody:
    case EAstNodeType::Ir_ArrayAdd:
    case EAstNodeType::Ir_MapAdd:
    case EAstNodeType::Ir_ArrayUnsafeCall:
    case EAstNodeType::Ir_ConvertToDynamic:
    case EAstNodeType::Ir_ConvertFromDynamic:
    case EAstNodeType::Concurrent_Sync:
    case EAstNodeType::Concurrent_Rush:
    case EAstNodeType::Concurrent_Race:
    case EAstNodeType::Concurrent_SyncIterated:
    case EAstNodeType::Concurrent_RushIterated:
    case EAstNodeType::Concurrent_RaceIterated:
    case EAstNodeType::Concurrent_Branch:
    case EAstNodeType::Concurrent_Spawn:
    case EAstNodeType::Concurrent_Await:
    case EAstNodeType::Concurrent_Upon:
    case EAstNodeType::Concurrent_When:
    case EAstNodeType::Definition_Module:
    case EAstNodeType::Definition_Enum:
    case EAstNodeType::Definition_Interface:
    case EAstNodeType::Definition_Class:
    case EAstNodeType::Definition_Data:
    case EAstNodeType::Definition_IterationPair:
    case EAstNodeType::Definition_Function:
    case EAstNodeType::Definition_TypeAlias:
    case EAstNodeType::Definition_Using:
    case EAstNodeType::Definition_Import:
    case EAstNodeType::Definition_Where:
    case EAstNodeType::Definition_Var:
    case EAstNodeType::Definition_Live:
    case EAstNodeType::Definition_ScopedAccessLevel:
    case EAstNodeType::Invoke_MakeNamed:
    case EAstNodeType::Context_Project:
    case EAstNodeType::Context_CompilationUnit:
    case EAstNodeType::Context_Package:
    case EAstNodeType::Context_Snippet:
    case EAstNodeType::Invoke_OptionFormer:
    default:
        return SQualifierResult::Failed();
    }
}

TArray<VstSharedRef> QualifyAllAnalyzedIdentifiers(bool bVerbose, const TSRef<CSemanticProgram>& Program, VstSharedRef& Root)
{
    TArray<VstSharedRef> IdentifiersFailed;

    uLang::TArray<VstSharedRef> Stack;
    Stack.Add(Root);
    while (!Stack.IsEmpty())
    {
        VstSharedRef CurrentNode = Stack.Pop();
        for (const VstSharedRef& Child : CurrentNode->GetChildren())
        {
            Stack.Add(Child);
        }

        auto VstIdentifier = CurrentNode->AsNullable<Verse::Vst::Identifier>();
        if (!VstIdentifier || !VstIdentifier->CanBeQualified())
        {
            continue;
        }
        if (VstIdentifier->IsQualified())
        {
            continue;
        }

        const CAstNode* AstNode = VstIdentifier->GetMappedAstNode();
        if (!AstNode)
        {
            continue;
        }

        const CExpressionBase* Expr = AstNode->AsExpression();
        const SQualifierResult& Qualifier = QualifierOf(Expr, VstIdentifier, *Program);
        switch (Qualifier.Status)
        {
        case SQualifierResult::EStatus::Qualified:
            VstIdentifier->AddQualifier(Qualifier.String);
            break;
        case SQualifierResult::EStatus::NotRequired:
            break;
        case SQualifierResult::EStatus::Failed:
            IdentifiersFailed.Add(CurrentNode);
            break;
        case SQualifierResult::EStatus::Undefined:
            // TODO Do something here?
        default:
            ULANG_UNREACHABLE();
        }
    }
    if (IdentifiersFailed.Num() && bVerbose)
    {
        ULANG_LOGF(Display, "# Identifiers left to qualify: %d", IdentifiersFailed.Num());
    }

    return IdentifiersFailed;
}

}    // Namespace uLang
