// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMObjectPrinting.h"
#include "Containers/Set.h"
#include "Containers/Utf8String.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Misc/TransactionallySafeRWLock.h"
#include "UObject/Object.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/VVMClass.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMNames.h"
#include "VerseVM/VVMNativeString.h"
#include "VerseVM/VVMObject.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMValuePrinting.h"

namespace
{
struct FObjectPrintHandlerRegistry
{
	FTransactionallySafeRWLock RWLock;
	TArray<Verse::ObjectPrinting::FHandler*> Handlers;

	static FObjectPrintHandlerRegistry& Get()
	{
		static FObjectPrintHandlerRegistry Registry;
		return Registry;
	}
};
} // namespace

namespace Verse
{
namespace ObjectPrinting
{
void RegisterHandler(FHandler* Handler)
{
	FObjectPrintHandlerRegistry& Registry = FObjectPrintHandlerRegistry::Get();
	UE::TWriteScopeLock Lock(Registry.RWLock);
	Registry.Handlers.Add(Handler);
}
void UnregisterHandler(FHandler* Handler)
{
	FObjectPrintHandlerRegistry& Registry = FObjectPrintHandlerRegistry::Get();
	UE::TWriteScopeLock Lock(Registry.RWLock);
	Registry.Handlers.Remove(Handler);
}
} // namespace ObjectPrinting

void AppendToString(FUtf8StringBuilderBase& Builder, UObject* Object, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	if (IsCellFormat(Format))
	{
		Builder << UTF8TEXT("UObject");
		if (Format == EValueStringFormat::CellsWithAddresses)
		{
			Builder.Appendf(UTF8TEXT("@0x%p"), Object);
		}
		Builder << UTF8TEXT('(');

		if (Format == EValueStringFormat::CellsWithAddresses && VisitedObjects.Contains(Object))
		{
			Builder << UTF8TEXT(')');
			return;
		}
	}

	bool bHandled = false;
	if (Object)
	{
		// Give the registered print handlers a chance to handle this UObject first.
		FObjectPrintHandlerRegistry& Registry = FObjectPrintHandlerRegistry::Get();
		UE::TReadScopeLock Lock(Registry.RWLock);
		for (ObjectPrinting::FHandler* Handler : Registry.Handlers)
		{
			if (Handler->TryStringHandle(Object, Builder, Format, VisitedObjects, RecursionDepth))
			{
				bHandled = true;
				break;
			}
		}
	}

	if (!bHandled)
	{
		// Otherwise, just print its name.
		VisitedObjects.Add(Object);
		Builder << GetFullNameSafe(Object);
	}

	if (IsCellFormat(Format))
	{
		Builder << UTF8TEXT(')');
	}
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
void VObject::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	if (Format == EValueStringFormat::CellsWithAddresses && VisitedObjects.Contains(this))
	{
		// we already printed the address in `VCell::AppendToString`
		return;
	}
	VisitedObjects.Add(this);

	VEmergentType* EmergentType = GetEmergentType();

	if (!IsCellFormat(Format))
	{
		EmergentType->Type->StaticCast<VClass>().AppendQualifiedName(Builder);
		Builder << UTF8TEXT("{");
	}

	// VNativeStructs have no shape
	if (EmergentType->Shape)
	{
		// Print the fields of the object.
		FUtf8StringView Separator = UTF8TEXT("");
		for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
		{
			Builder << Separator;
			Separator = UTF8TEXT(", ");

			VUniqueString* FieldName = It->Key.Get();
			Builder << Verse::Names::RemoveQualifier(FieldName->AsStringView());
			Builder << UTF8TEXT(" := ");

			FOpResult FieldResult = LoadField(Context, *FieldName);
			if (FieldResult.IsReturn())
			{
				FieldResult.Value.AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth + 1);
			}
			else
			{
				Builder << "\"(error)\"";
			}
		}
	}

	if (!IsCellFormat(Format))
	{
		Builder << UTF8TEXT('}');
	}
}

TSharedPtr<FJsonValue> VObject::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	VisitedObjects.Add(this, Verse::EVisitState::Visiting);

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	if (Format == EValueJSONFormat::Persistence && !IsStruct())
	{
		VEmergentType* EmergentType = GetEmergentType();
		VNamedType& NamedType = EmergentType->Type->StaticCast<VNamedType>();
		VPackage& Package = NamedType.GetPackage();
		FUtf8String PackageName{Verse::Names::GetUPackagePath<UTF8CHAR>(Package.GetName().AsStringView())};
		FUtf8String ClassName{NamedType.GetFullName()};
		// This information is only needed by the BPVM, as the Verse VM has enough
		// context from the VClass.
		JsonObject->SetField(Persistence::PackageNameKey, MakeShared<TJsonValueString<UTF8CHAR>>(::MoveTemp(PackageName)));
		JsonObject->SetField(Persistence::ClassNameKey, MakeShared<TJsonValueString<UTF8CHAR>>(::MoveTemp(ClassName)));
	}

	VEmergentType* EmergentType = GetEmergentType();
	for (auto I = EmergentType->Shape->CreateFieldsIterator(); I; ++I)
	{
		FOpResult FieldValue = {FOpResult::Error};
		AutoRTFM::Open([&] { FieldValue = LoadField(Context, *EmergentType, &I->Value); });

		if (!FieldValue.IsReturn())
		{
			return nullptr;
		}
		TSharedPtr<FJsonValue> FieldJsonValue = FieldValue.Value.ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
		if (!FieldJsonValue)
		{
			return nullptr;
		}
		FUtf8StringView UnqualifiedName = Verse::Names::RemoveQualifier(I->Key->AsStringView());
		JsonObject->SetField(Format == EValueJSONFormat::Persistence ? ShortNameToFieldName(UnqualifiedName) : FString(UnqualifiedName), ::MoveTemp(FieldJsonValue));
	}

	VisitedObjects.Add(this, EVisitState::Visited);
	return MakeShared<FJsonValueObject>(::MoveTemp(JsonObject));
}
#endif

} // namespace Verse
