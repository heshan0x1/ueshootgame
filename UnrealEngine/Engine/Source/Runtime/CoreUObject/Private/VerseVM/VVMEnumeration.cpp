// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMEnumeration.h"
#include "UObject/CoreRedirects.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMEnumerator.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMTypeInitOrValidate.h"
#include "VerseVM/VVMValuePrinting.h"
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMVerseEnum.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VEnumeration)
TGlobalTrivialEmergentTypePtr<&VEnumeration::StaticCppClassInfo> VEnumeration::GlobalTrivialEmergentType;

VEnumerator* VEnumeration::GetEnumerator(const VUniqueString& Name) const
{
	for (auto I = Enumerators, Last = I + NumEnumerators; I != Last; ++I)
	{
		if ((*I)->GetName() == &Name)
		{
			return I->Get();
		}
	}
	return nullptr;
}

void VEnumeration::SerializeLayout(FAllocationContext Context, VEnumeration*& This, FStructuredArchiveVisitor& Visitor)
{
	int32 NumEnumerators = 0;
	if (!Visitor.IsLoading())
	{
		NumEnumerators = This->NumEnumerators;
	}

	Visitor.Visit(NumEnumerators, TEXT("NumEnumerators"));
	if (Visitor.IsLoading())
	{
		This = new (Context.AllocateFastCell(sizeof(VEnumeration) + NumEnumerators * sizeof(TWriteBarrier<VEnumerator>))) VEnumeration(Context, NumEnumerators);
	}
}

void VEnumeration::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	VNamedType::SerializeImpl(Context, Visitor);
	Visitor.Visit(Enumerators, NumEnumerators, TEXT("Enumerators"));
}

TSharedPtr<FJsonValue> VEnumeration::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(NumEnumerators);
	for (int32 Index = 0; Index < NumEnumerators; ++Index)
	{
		Values.Emplace(MakeShared<FJsonValueString>(Enumerators[Index]->GetName()->AsString()));
	}

	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetArrayField(PERSONA_FIELD(Enum), ::MoveTemp(Values));
	return MakeShared<FJsonValueObject>(Object);
}

VValue VEnumeration::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	FUtf8String Name;
	if (!JsonValue.TryGetUtf8String(Name))
	{
		return VValue();
	}

	if (Format == EValueJSONFormat::Persistence)
	{
		int32 Index = Name.Find("::");
		if (Index != INDEX_NONE)
		{
			Name.RightChopInline(Index + 2);
		}
		VUniqueString& EnumeratorName = VUniqueString::New(Context, Name);
		VEnumerator* Enumerator = GetEnumerator(EnumeratorName);
		return Enumerator ? *Enumerator : VValue();
	}

	// EValueJSONFormat::Persona
	VEnumerator* Enumerator = GetEnumerator(VUniqueString::New(Context, Name));
	return Enumerator ? *Enumerator : VValue();
}

template <typename TVisitor>
void VEnumeration::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Enumerators, NumEnumerators, TEXT("Enumerators"));
}

VEnumeration::VEnumeration(FAllocationContext Context, VPackage* InPackage, VArray* InRelativePath, VArray* InEnumName, VArray* InAttributeIndices, VArray* InAttributes, UEnum* InImportEnum, bool bInNative, const TArray<VEnumerator*>& InEnumerators)
	: VNamedType(Context, &GlobalTrivialEmergentType.Get(Context), InPackage, InRelativePath, InEnumName, InAttributeIndices, InAttributes, InImportEnum, bInNative)
	, NumEnumerators(InEnumerators.Num())
{
	for (VEnumerator* Enumerator : InEnumerators)
	{
		V_DIE_UNLESS(Enumerator->GetIntValue() >= 0 && Enumerator->GetIntValue() < int32(NumEnumerators));
		new (&Enumerators[Enumerator->GetIntValue()]) TWriteBarrier<VEnumerator>(Context, Enumerator);
	}

	if (InImportEnum != nullptr)
	{
		if (UVerseEnum* UeEnum = Cast<UVerseEnum>(InImportEnum))
		{
			if (EnumHasAnyFlags(UeEnum->VerseEnumFlags, EVerseEnumFlags::UHTNative))
			{
				UeEnum->Enumeration.Set(Context, this);
				Private::FVerseVMInitOrValidate<UVerseEnum> InitOrValidate(UeEnum);
				Prepare(InitOrValidate);
			}
		}
	}
}

VEnumeration::VEnumeration(FAllocationContext Context, int32 InNumEnumerators)
	: VNamedType(Context, &GlobalTrivialEmergentType.Get(Context), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false)
	, NumEnumerators(InNumEnumerators)
{
	for (int32 Index = 0; Index < NumEnumerators; ++Index)
	{
		new (&Enumerators[Index]) TWriteBarrier<VEnumerator>{};
	}
}

void VEnumeration::Prepare(const FInitOrValidateUVerseEnum& InitOrValidate)
{
	// -----------------------------------------------------------------------------------------------------
	// Keep the following code in sync with FSolClassGenerator::Prepare for enumerations

	UVerseEnum* VerseEnum = InitOrValidate.GetUVerseEnum();

	FString CppType = VerseEnum->GetName();
	TUtf8StringBuilder<Names::DefaultNameLength> QualifiedName;
	AppendQualifiedName(QualifiedName);

	InitOrValidate.SetValue(VerseEnum->CppType, CppType, TEXT("CppType"));
	InitOrValidate.SetValue(VerseEnum->QualifiedName, QualifiedName.ToString(), TEXT("QualifiedName"));

	FNameBuilder Name;
	Name.Append(CppType);
	Name.Append(TEXT("::"));
	int NamePrefix = Name.Len();

	TArray<TPair<FName, int64>> NameValuePairs;
	for (int32 Index = 0; Index < NumEnumerators; ++Index)
	{
		VEnumerator& Enumerator = *Enumerators[Index].Get();
		Name.RemoveSuffix(Name.Len() - NamePrefix);
		Name.Append(Enumerator.GetName()->AsStringView());
		NameValuePairs.Emplace(FName(Name.ToView()), Enumerator.GetIntValue());
	}
	InitOrValidate.SetEnums(NameValuePairs, UEnum::ECppForm::EnumClass);
}

UEnum* VEnumeration::CreateUEType(FAllocationContext Context)
{
	ensure(!HasUEType()); // Caller must ensure that this is not already set

	// Create the new UEnumeration/UScriptStruct object

	IEngineEnvironment* Environment = VerseVM::GetEngineEnvironment();
	check(Environment);

	// Create package for the enumeration
	UPackage* UePackage = Package->GetOrCreateUPackage(Context);

	AddRedirect(ECoreRedirectFlags::Type_Enum);

	EVersePackageType PackageType;
	Names::GetUPackagePath(Package->GetName().AsStringView(), &PackageType);
	UTF8CHAR Separator = PackageType == EVersePackageType::VNI ? UTF8CHAR('_') : UTF8CHAR('-');

	TUtf8StringBuilder<Names::DefaultNameLength> UeName;
	AppendMangledName(UeName, Separator);

	// Create the UE enum
	UVerseEnum* UeEnum = NewObject<UVerseEnum>(UePackage, FName(UeName), RF_Public /* | RF_Transient*/);
	NativeType.Set(Context, UeEnum);
	UeEnum->Enumeration.Set(Context, this);
	if (IsNativeBound())
	{
		UeEnum->SetNativeBound();
	}

	Private::FVerseVMInitOrValidate<UVerseEnum> InitOrValidate(UeEnum);
	Prepare(InitOrValidate);
	return UeEnum;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
