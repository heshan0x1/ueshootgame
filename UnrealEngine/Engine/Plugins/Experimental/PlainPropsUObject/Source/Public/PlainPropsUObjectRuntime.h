// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsBind.h"
#include "PlainPropsDeclare.h"
#include "PlainPropsIndex.h"
#include "PlainPropsTypes.h"
#include "PlainPropsUeCoreBindings.h"
#include "Containers/Map.h"
#include "UObject/DynamicallyTypedValue.h"
#include "UObject/NameTypes.h"
#include "UObject/ScriptDelegateFwd.h"

struct FFieldPath;
struct FReferencePropertyValue;
struct FVerseFunction;

namespace PlainProps::UE
{

struct FDefaultInstance { uint64 Ptr; };

class FDefaultStructs final : public IDefaultStructs
{
public:
	~FDefaultStructs();
	void									Bind(FBindId Id, const UScriptStruct* Struct);
	void									BindZeroes(FBindId Id, SIZE_T Size, uint32 Alignment);
	void									BindStatic(FBindId Id, const void* Struct);
	void									Drop(FBindId Id);
	virtual const void*						Get(FBindId Id) override;

private:
	FMutableMemoryView						Zeroes;
	TBitArray<>								Instanced;
	TMap<FBindId, FDefaultInstance>			Instances;
#if DO_CHECK
	TBitArray<>								Bound;
#endif

	void									ReserveFlags(uint32 Idx);
};

struct FCommonScopeIds
{
	explicit FCommonScopeIds(TIdIndexer<FSensitiveName>& Names);
	
	FScopeId					Core;
	FScopeId					CoreUObject;
};

struct FCommonTypenameIds
{
	explicit FCommonTypenameIds(TIdIndexer<FSensitiveName>& Names);
	
	FConcreteTypenameId			Optional;
	FConcreteTypenameId			Map;
	FConcreteTypenameId			Set;
	FConcreteTypenameId			Pair;
	FConcreteTypenameId			LeafArray;
	FConcreteTypenameId			TrivialArray;
	FConcreteTypenameId			NonTrivialArray;
	FConcreteTypenameId			StaticArray;
	FConcreteTypenameId			TrivialOptional;
	FConcreteTypenameId			IntrusiveOptional;
	FConcreteTypenameId			NonIntrusiveOptional;
	FConcreteTypenameId			String;
	FConcreteTypenameId			Utf8String;
	FConcreteTypenameId			AnsiString;
	FConcreteTypenameId			VerseString;
};

struct FCommonStructIds
{
	explicit FCommonStructIds(const FCommonScopeIds& Scopes, TIdIndexer<FSensitiveName>& Names);
	
	FDualStructId			Name;
	FDualStructId			Text;
	FDualStructId			Guid;
	FDualStructId			FieldPath;
	FDualStructId			SoftObjectPath;
	FDualStructId			ClassPtr;
	FDualStructId			ObjectPtr;
	FDualStructId			WeakObjectPtr;
	FDualStructId			LazyObjectPtr;
	FDualStructId			SoftObjectPtr;
	FDualStructId			ScriptInterface;
	FDualStructId			Delegate;
	FDeclId					MulticastDelegate;
	FBindId					MulticastInlineDelegate;
	FDualStructId			MulticastSparseDelegate;
	FDualStructId			VerseFunction;
	FDualStructId			DynamicallyTypedValue;
	FDualStructId			ReferencePropertyValue;
};

struct FCommonMemberIds
{
	explicit FCommonMemberIds(TIdIndexer<FSensitiveName>& Names);
	
	FMemberId					Key;
	FMemberId					Value;
	FMemberId					Assign;
	FMemberId					Remove;
	FMemberId					Insert;
	FMemberId					Id;
	FMemberId					Object;
	FMemberId					Function;
	FMemberId					Invocations;
	FMemberId					Path;
	FMemberId					Owner;
};

struct FGlobals
{
	FGlobals();

	TIdIndexer<FSensitiveName>	Names;
	FEnumDeclarations			Enums;
	FSchemaBindings				Schemas;
	FCustomBindingsBottom		Customs;
	FDefaultStructs				Defaults;
	FCommonScopeIds				Scopes;
	FCommonStructIds			Structs;
	FCommonTypenameIds			Typenames;
	FCommonMemberIds			Members;
	FNumeralGenerator			Numerals;
	//Upgrade::FHistory			Upgrades;
	FDebugIds					Debug;
};
PLAINPROPSUOBJECT_API extern FGlobals GUE;

struct FRuntimeIds
{
	static FNameId				IndexName(FAnsiStringView Name)			{ return GUE.Names.MakeName(FName(Name)); }
	static FMemberId			IndexMember(FAnsiStringView Name)		{ return GUE.Names.NameMember(FName(Name)); }
	static FConcreteTypenameId	IndexTypename(FAnsiStringView Name)		{ return GUE.Names.NameType(FName(Name)); }
	static FFlatScopeId			IndexScope(FAnsiStringView Name)		{ return GUE.Names.NameScope(FName(Name)); }
	static FEnumId				IndexEnum(FType Type)					{ return GUE.Names.IndexEnum(Type); }
	static FStructId			IndexStruct(FType Type)					{ return GUE.Names.IndexStruct(Type); }
	static FIdIndexerBase&		GetIndexer()							{ return GUE.Names; }
};

struct FDefaultRuntime
{
	using Ids = FRuntimeIds;
	template<class T> using CustomBindings = TCustomBind<T>;

	static FEnumDeclarations&	GetEnums()			{ return GUE.Enums; }
	static FSchemaBindings&		GetSchemas()		{ return GUE.Schemas; }
	static FCustomBindings&		GetCustoms()		{ return GUE.Customs; }
	static IDefaultStructs*		GetDefaults()		{ return nullptr; }
};

struct FDeltaRuntime : FDefaultRuntime
{
	template<class T> using CustomBindings = TCustomDeltaBind<T>;

	static IDefaultStructs*		GetDefaults()		{ return &GUE.Defaults; }
};

template<uint32 N>
using TPropertySpecifier = TCustomSpecifier<FRuntimeIds, N>;

//////////////////////////////////////////////////////////////////////////

struct FFieldPathBinding : ICustomBinding
{
	using Type = FFieldPath;
	const FMemberId MemberIds[2];

	FFieldPathBinding(TPropertySpecifier<2>& Spec);

	void			Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext&) const;
	void			Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const;
	static bool		Diff(const Type& A, const Type& B, const FBindContext&);
};

struct FDelegateBinding : ICustomBinding
{
	using Type = FScriptDelegate;
	const FMemberId MemberIds[2];

	FDelegateBinding(TPropertySpecifier<2>& Spec);
	
	void			Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext&) const;
	void			Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const;
	static bool		Diff(const Type& A, const Type& B, const FBindContext&);
};

struct FMulticastInlineDelegateBinding : ICustomBinding
{
	using Type = FMulticastScriptDelegate;
	const FMemberId MemberIds[1];

	FMulticastInlineDelegateBinding(TPropertySpecifier<1>& Spec);
	
	void			Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext&) const;
	void			Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const;
	static bool		Diff(const Type& A, const Type& B, const FBindContext&);
};

//////////////////////////////////////////////////////////////////////////

struct FVerseFunctionBinding : ICustomBinding
{
	using Type =  FVerseFunction;
	const FMemberId MemberIds[1];

	FVerseFunctionBinding(TPropertySpecifier<1>& Spec);
	
	void			Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext&) const;
	void			Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const;
	static bool		Diff(const Type& A, const Type& B, const FBindContext&);
};

struct FDynamicallyTypedValueBinding : ICustomBinding
{
	using Type = ::UE::FDynamicallyTypedValue;
	const FMemberId MemberIds[1];

	FDynamicallyTypedValueBinding(TPropertySpecifier<1>& Spec);
	
	void			Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext&) const;
	void			Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const;
	static bool		Diff(const Type& A, const Type& B, const FBindContext&);
};

struct FReferencePropertyBinding : ICustomBinding
{
	using Type = FReferencePropertyValue;
	const FMemberId MemberIds[1];

	FReferencePropertyBinding(TPropertySpecifier<1>& Spec);
	
	void			Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext&) const;
	void			Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const;
	static bool		Diff(const Type& A, const Type& B, const FBindContext&);
};

} // namespace PlainProps::UE

namespace PlainProps
{
template<> struct TCustomBind<FFieldPath> { using Type = UE::FFieldPathBinding; };
template<> struct TCustomBind<FScriptDelegate> { using Type = UE::FDelegateBinding; };
template<> struct TCustomBind<FMulticastScriptDelegate> { using Type = UE::FMulticastInlineDelegateBinding; };
template<> struct TCustomBind<::UE::FDynamicallyTypedValue> { using Type = UE::FDynamicallyTypedValueBinding; };
template<> struct TCustomBind<FReferencePropertyValue> { using Type = UE::FReferencePropertyBinding; };
template<> struct TCustomBind<FVerseFunction> { using Type = UE::FVerseFunctionBinding; };

// Temporary way to tie certain types to /Script/Core scope
template<> inline FScopeId IndexNamespaceId<UE::FRuntimeIds, TTypename<FTransform>>()		{ return UE::GUE.Scopes.Core; }
template<> inline FScopeId IndexNamespaceId<UE::FRuntimeIds, TTypename<FGuid>>()			{ return UE::GUE.Scopes.Core; }
template<> inline FScopeId IndexNamespaceId<UE::FRuntimeIds, TTypename<FColor>>()			{ return UE::GUE.Scopes.Core; }
template<> inline FScopeId IndexNamespaceId<UE::FRuntimeIds, TTypename<FLinearColor>>()		{ return UE::GUE.Scopes.Core; }

}
