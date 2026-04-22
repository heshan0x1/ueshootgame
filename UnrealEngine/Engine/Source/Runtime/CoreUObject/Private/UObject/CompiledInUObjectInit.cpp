// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectMacros.h"

#if UE_WITH_CONSTINIT_UOBJECT

#include "Logging/StructuredLog.h"
#include "Misc/FeedbackContext.h"
#include "Misc/MessageDialog.h"
#include "Misc/TVariant.h"
#include "Serialization/AsyncLoadingEvents.h"
#include "Serialization/LoadTimeTrace.h"
#include "Templates/Overload.h"
#include "UObject/CompiledInObjectRegistry.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectPrivate.h"
#include "VerseVM/VVMVerseClass.h"
#include "VerseVM/VVMVerseEnum.h"
#include "VerseVM/VVMVerseFunction.h"
#include "VerseVM/VVMVerseStruct.h"

// Initialization functions called during boot.

void UClassRegisterAllCompiledInClasses();
void ProcessNewlyLoadedUObjects(FName InModuleName, bool bCanProcessNewlyLoadedObjects);


// Requirements
static_assert(IS_MONOLITHIC, "UE_WITH_CONSTINIT_UOBJECT can currently only be set for monolithic builds");
static_assert(!WITH_LIVE_CODING, "UE_WITH_CONSTINIT_UOBJECT and WITH_LIVE_CODING cannot currently be set simultaneously.");
static_assert(!WITH_HOT_RELOAD, "UE_WITH_CONSTINIT_UOBJECT and WITH_HOT_RELOAD cannot currently be set simultaneously.");
static_assert(!WITH_RELOAD, "UE_WITH_CONSTINIT_UOBJECT and WITH_RELOAD cannot currently be set simultaneously.");
static_assert(!UE_WITH_REMOTE_OBJECT_HANDLE, "UE_WITH_CONSTINIT_UOBJECT and UE_WITH_REMOTE_OBJECT_HANDLE cannot currently be set simultaneously.");
static_assert(!USE_PER_MODULE_UOBJECT_BOOTSTRAP, "UE_WITH_CONSTINIT_UOBJECT and USE_PER_MODULE_UOBJECT_BOOTSTRAP cannot currently be set simultaneously.");



void UClassRegisterAllCompiledInClasses()
{
	SCOPED_BOOT_TIMING("UClassRegisterAllCompiledInClasses");
	LLM_SCOPE(ELLMTag::UObject);
	UE_LOGFMT(LogInit, Verbose, "UClassRegisterAllCompiledInClasses");

	// Don't seem to actually need to do anything here - Objects are already constructed, but we don't
	// want to hash or index them yet because necessary config has not been initialized.
}

void UObjectForceRegistration(UObjectBase* Object, bool bCheckForModuleRelease)
{
	// No-op for constinit uobjects case
}

void UObjectProcessRegistrants()
{
	SCOPED_BOOT_TIMING("UObjectProcessRegistrants");
	LLM_SCOPE(ELLMTag::UObject);
	check(UObjectInitialized());

	FCompiledInObjectRegistry& Registry = FCompiledInObjectRegistry::Get();
	Registry.AddAndHashObjects();
}

void UE::CoreUObject::ConstructCompiledInObjects()
{
	SCOPED_BOOT_TIMING("UE::CoreUObject::ConstructCompiledInObjects");
	LLM_SCOPE(ELLMTag::UObject);
	check(UObjectInitialized());

	FCompiledInObjectRegistry& Registry = FCompiledInObjectRegistry::Get();
	while (Registry.HasObjectsPendingConstruction())
	{
		Registry.AddAndHashObjects();
		Registry.FinishConstructingObjects();
	}
}

void ProcessNewlyLoadedUObjects(FName InModuleName, bool bCanProcessNewlyLoadedObjects)
{
	if (!bCanProcessNewlyLoadedObjects)
	{
		FCoreUObjectDelegates::CompiledInUObjectsRegisteredDelegate.Broadcast(InModuleName, ECompiledInUObjectsRegisteredStatus::Delayed);
		return;
	}
	SCOPED_BOOT_TIMING("ProcessNewlyLoadedUObjects");
	LLM_SCOPE(ELLMTag::UObject);
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ProcessNewlyLoadedUObjects"), STAT_ProcessNewlyLoadedUObjects, STATGROUP_ObjectVerbose);

	check(UObjectInitialized());
	FCompiledInObjectRegistry& Registry = FCompiledInObjectRegistry::Get();

	while (Registry.HasPendingObjects())
	{
		Registry.AddAndHashObjects();
		Registry.FinishConstructingObjects();
		Registry.CreateClassDefaultObjects(InModuleName);
	}
	Registry.EmptyObjects();

	FCoreUObjectDelegates::CompiledInUObjectsRegisteredDelegate.Broadcast(InModuleName, ECompiledInUObjectsRegisteredStatus::PostCDO);

	if (!GIsInitialLoad)
	{
		Registry.AssembleReferenceTokenStream();
	}
}


static void InitializeConstInitProperties(UStruct* Struct);

void FCompiledInObjectRegistry::EmptyObjects()
{
	checkfSlow(ListHead == nullptr || (ListHead == AlreadyAdded && ListHead == AlreadyConstructed && ListHead == AlreadyConstructedDefaultObjects), 
		TEXT("Attempting to empty deferred registry when we don't appear to have constructed all objects"));
	// Just drop the whole list - if the structs are recompiled and call Link again we will replace the Next ptr
	ListHead = nullptr;
	AlreadyAdded = nullptr;
	AlreadyConstructed = nullptr;
	AlreadyConstructedDefaultObjects = nullptr;
}

bool FCompiledInObjectRegistry::HasObjectsPendingConstruction() const
{
	return ListHead && (ListHead != AlreadyAdded || ListHead != AlreadyConstructed);
}

bool FCompiledInObjectRegistry::HasPendingObjects() const
{
	return ListHead && (ListHead != AlreadyAdded || ListHead != AlreadyConstructed || ListHead != AlreadyConstructedDefaultObjects);
}

template<typename... FUNCTORS>
void FCompiledInObjectRegistry::Iterate(FRegisterCompiledInObjects* Stop, FUNCTORS&&... FS)
{
	for (FRegisterCompiledInObjects* It = ListHead; It != Stop; It = It->ListNext)
	{
		Visit([Dispatch=UE::Overload(Forward<FUNCTORS>(FS)...)](const auto& Registrant) 
		{
			if constexpr (std::is_invocable_v<decltype(Dispatch), decltype(Registrant)>)
			{
				Dispatch(Registrant);
			}
		}, It->Registrants);
	}
}

template<typename FUNCTOR>
void FCompiledInObjectRegistry::IterateObjects(FRegisterCompiledInObjects* Stop, FUNCTOR&& F)
{
	Iterate(Stop, 
		[&F](const FRegisterCompiledInObjects::FPackageRegistrants& PReg) 
		{ 
			F(PReg.Package);
			for (UObject* Obj : PReg.Objects)
			{
				F(Obj);
			}
		},
		[&F](const FRegisterCompiledInObjects::FClassRegistrants& CReg) 
		{ 
			for (UScriptStruct* Struct : CReg.Structs)
			{
				F(Struct);
			}
			for (UEnum* Enum : CReg.Enums)
			{
				F(Enum);
			}
			for (UClass* Class : CReg.Classes)
			{
				F(Class);
			}
		},
		[&F](const FRegisterCompiledInObjects::FIntrinsicClassRegistrant& IReg) 
		{ 
			F(IReg.IntrinsicClass);
		}
	);
}

template<typename FUNCTOR>
void FCompiledInObjectRegistry::IterateScriptStructs(FRegisterCompiledInObjects* Stop, FUNCTOR&& F)
{
	Iterate(Stop, 
		[&F](const FRegisterCompiledInObjects::FPackageRegistrants& PReg) 
		{ 
			for (UObject* Obj : PReg.Objects)
			{
				if (UScriptStruct* Struct = Cast<UScriptStruct>(Obj))
				{
					F(Struct);
				}
			}
		},
		[&F](const FRegisterCompiledInObjects::FClassRegistrants& CReg) 
		{ 
			for (UScriptStruct* Struct : CReg.Structs)
			{
				F(Struct);
			}
		}
	);
}

template<typename FUNCTOR>
void FCompiledInObjectRegistry::IterateClasses(FRegisterCompiledInObjects* Stop, FUNCTOR&& F)
{
	Iterate(Stop, 
		[&F](const FRegisterCompiledInObjects::FClassRegistrants& CReg) 
		{
		   	for (UClass* Class : CReg.Classes)
			{
				F(Class);
			}
		},
		[&F](const FRegisterCompiledInObjects::FIntrinsicClassRegistrant& IReg) 
		{
			F(IReg.IntrinsicClass);
		}
	);
}

template<typename FUNCTOR>
void FCompiledInObjectRegistry::IterateIntrinsicClasses(FRegisterCompiledInObjects* Stop, FUNCTOR&& F)
{
	Iterate(Stop, 
		[&F](const FRegisterCompiledInObjects::FIntrinsicClassRegistrant& IReg) 
		{
			F(IReg.IntrinsicClass, IReg.Constructor);
		});
}

/** Initialize the name and index of all pending objects and add them to the object hash. */
void FCompiledInObjectRegistry::AddAndHashObjects()
{
	SCOPED_BOOT_TIMING("FDeferredRegistry::AddAndHashPendingRegistrants");
	FRegisterCompiledInObjects* Stop = AlreadyAdded;
	AlreadyAdded = ListHead;
	IterateObjects(Stop, [](UObject* Obj)
	{
		Obj->AddConstInitObject();
		// Initialize UStruct and UFunction objects
		if (UStruct* Struct = Cast<UStruct>(Obj))
		{
			InitializeConstInitProperties(Struct);
			for (UField* Field = Struct->Children; Field; Field = Field ->Next)
			{
				Field->AddConstInitObject();
				if (UStruct* InnerStruct = Cast<UStruct>(Field))
				{
					InitializeConstInitProperties(InnerStruct);
				}
			}
		}

		// TODO: General purpose interface function for initializing compiled in FNames
		if (UClass* Class = Cast<UClass>(Obj))
		{
			Class->ClassConfigName = FName(Class->CompiledInClassConfigName);

			// Create TArray of native functions from compiled-in data to maintain API
			// Note: this needs to be done before native function binding
			if (Class != UObject::StaticClass())
			{
				TConstArrayView<UE::CodeGen::FClassNativeFunction> CompiledInNativeFunctions = Class->CompiledInNativeFunctions;
				new (&Class->NativeFunctionLookupTable) TArray<FNativeFunctionLookup>(CompiledInNativeFunctions);
			}
		}
		else if (USparseDelegateFunction* SparseDelegate = Cast<USparseDelegateFunction>(Obj))
		{
			const UTF8CHAR* OwningClassName = SparseDelegate->CompiledInOwningClassName;
			const UTF8CHAR* DelegateName = SparseDelegate->CompiledInDelegateName;
			new (&SparseDelegate->OwningClassName) FName(OwningClassName); // Can this not be inferred from elsewhere...?
			new (&SparseDelegate->DelegateName) FName(DelegateName);
		}
	});
}

#if WITH_METADATA
void FCompiledInObjectRegistry::AddMetaData(UObject* Object, TConstArrayView<UE::CodeGen::ConstInit::FMetaData> InMetaData)
{
	if (InMetaData.Num())
	{
		FMetaData& MetaData = Object->GetPackage()->GetMetaData();
		for (const UE::CodeGen::ConstInit::FMetaData& MetaDataParam : InMetaData)
		{
			MetaData.SetValue(Object, UTF8_TO_TCHAR(MetaDataParam.NameUTF8), UTF8_TO_TCHAR(MetaDataParam.ValueUTF8));
		}
	}
}

TConstArrayView<UE::CodeGen::ConstInit::FMetaData> FCompiledInObjectRegistry::GetCompiledInMetaData(UStruct* InStruct)
{
	TConstArrayView<UE::CodeGen::ConstInit::FMetaData> MetaData = InStruct->CompiledInMetaData;
	static_assert(STRUCT_OFFSET(UStruct, CompiledInMetaData) == STRUCT_OFFSET(UStruct, Script));
	new (&InStruct->Script) TArray<uint8>(); // Activate other union member
	return MetaData;
}

TConstArrayView<UE::CodeGen::ConstInit::FMetaData> FCompiledInObjectRegistry::GetCompiledInMetaData(UEnum* InEnum)
{
	TConstArrayView<UE::CodeGen::ConstInit::FMetaData> MetaData = InEnum->CompiledInMetaData;
	InEnum->CompiledInMetaData = {}; // This is not aliased with anything 
	return MetaData;
}
#endif // WITH_METADATA

void FCompiledInObjectRegistry::AddMetaData(UStruct* Object)
{
#if WITH_METADATA
	AddMetaData(Object, GetCompiledInMetaData(Object));
#endif
}
void FCompiledInObjectRegistry::AddMetaData(UEnum* Object)
{
#if WITH_METADATA
	AddMetaData(Object, GetCompiledInMetaData(Object));
#endif
}

/** Finalize construction of all pending objects */
void FCompiledInObjectRegistry::FinishConstructingObjects()
{
	SCOPED_BOOT_TIMING("FDeferredRegistry::FinishConstructingPendingRegistrants");
	FRegisterCompiledInObjects* Stop = AlreadyConstructed;
	AlreadyConstructed = ListHead;
	// Prepare struct cpp ops before linking to try and remove dependency between FStructProperty and UScriptStruct
	IterateScriptStructs(Stop, [](UScriptStruct* Struct){
		Struct->PrepareCppStructOps();
	});

	auto ConstructScriptStruct = [](UScriptStruct* Struct)
	{
		// Get MetaData from union struct fields before linking
		AddMetaData(Struct);
		Struct->StaticLink();
		if (UVerseStruct* VerseStruct = Cast<UVerseStruct>(Struct))
		{
			VerseStruct->QualifiedName = VerseStruct->CompiledInQualifiedName;
		}
		NotifyRegistrationEvent(
			Struct->GetOutermost()->GetFName(),
			Struct->GetFName(),
			ENotifyRegistrationType::NRT_Struct,
			ENotifyRegistrationPhase::NRP_Finished,
			nullptr,
			false,
			Struct
		);
	};
	auto ConstructDelegate = [](UDelegateFunction* Delegate)
	{
		// Get MetaData from union struct fields before linking
		AddMetaData(Delegate);
		//	ConstructUFunctionInternal
		Delegate->Bind(); // Bind should no longer be necessary as constinit functions have their native function pointer assigned by UHT
		Delegate->StaticLink();
		if (Delegate->GetOuter() == Delegate->GetPackage())
		{
			// Notify loader of new top level noexport objects like UScriptStruct, UDelegateFunction and USparseDelegateFunction
			NotifyRegistrationEvent(
				Delegate->GetPackage()->GetFName(),
				Delegate->GetFName(),
				ENotifyRegistrationType::NRT_NoExportObject,
				ENotifyRegistrationPhase::NRP_Finished,
				nullptr,
				false,
				Delegate
			);
		}
	};
	auto ConstructFunction = [](UFunction* Function)
	{
		// Get MetaData from union struct fields before linking
		AddMetaData(Function);
		if (UVerseFunction* VerseFunction = Cast<UVerseFunction>(Function))
		{
			VerseFunction->AlternateName = VerseFunction->CompiledInAlternateNameUTF8;
		}
		Function->Bind(); // Bind should no longer be necessary as constinit functions have their native function pointer assigned by UHT
		Function->StaticLink();
	};
	auto ConstructEnum = [](UEnum* Enum)
	{
		// Get MetaData from union struct fields before linking
		AddMetaData(Enum);
		Enum->InitializeNames();
		if (UVerseEnum* VerseEnum = Cast<UVerseEnum>(Enum))
		{
			VerseEnum->QualifiedName = VerseEnum->CompiledInQualifiedName;
		}
		NotifyRegistrationEvent(
			Enum->GetOutermost()->GetFName(),
			Enum->GetFName(),
			ENotifyRegistrationType::NRT_Enum,
			ENotifyRegistrationPhase::NRP_Finished,
			nullptr,
			false,
			Enum
		);
	};
	auto ConstructObject = [&](UObject* Object)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(Object))
		{
			ConstructScriptStruct(Struct);
		}
		else if (UDelegateFunction* Delegate = Cast<UDelegateFunction>(Object))
		{
			ConstructDelegate(Delegate);
		}
		else if (UFunction* Function = Cast<UFunction>(Object))
		{
			ConstructFunction(Function);
		}
		else if (UEnum* Enum = Cast<UEnum>(Object))
		{
			ConstructEnum(Enum);
		}
		else 
		{
			checkf(false, TEXT("Unknown compiled-in object type to construct: %s"), *Object->GetFullName());
		}
	};
	{
		SCOPED_BOOT_TIMING("ConstructNonClassObjects");
		IterateObjects(Stop, UE::Overload(
			[](UPackage* Package) {},
			[](UClass* Class) {},
			ConstructObject,
			ConstructEnum,
			ConstructFunction,
			ConstructDelegate,
			ConstructScriptStruct
		));
	}

	SCOPED_BOOT_TIMING("ConstructClasses");

	// Execute manual construction code for intrinsic classes, which may create properties, before linking
	IterateIntrinsicClasses(Stop, [&](UClass* Class, void (*IntrinsicClassConstructor)()){
		IntrinsicClassConstructor();
	});
	IterateClasses(Stop ,[&](UClass* Class){
		AddMetaData(Class);
		if (UClass* SuperClass = Class->GetSuperClass())
		{
			checkfSlow(EnumHasAllFlags(Class->ClassFlags, (SuperClass->ClassFlags & CLASS_Inherit)), TEXT("Inheritable flags were not all propagated from %s to %s"), *SuperClass->GetPathName(), *Class->GetPathName());
			checkfSlow(EnumHasAllFlags(Class->ClassCastFlags, SuperClass->ClassCastFlags), TEXT("Class cast flags were no tall propagated from %s to %s"), *SuperClass->GetPathName(), *Class->GetPathName());
		}
		Class->ClassFlags |= CLASS_Constructed;

		// Make sure the reference token stream is empty since it will be reconstructed later on
		// This should not apply to intrinsic classes since they emit native references before AssembleReferenceTokenStream is called.
		if ((Class->ClassFlags & CLASS_Intrinsic) != CLASS_Intrinsic)
		{
			check((Class->ClassFlags & CLASS_TokenStreamAssembled) != CLASS_TokenStreamAssembled);
			Class->ReferenceSchema.Reset();
		}

		Class->InitFuncMap();
		{
			TConstArrayView<UE::CodeGen::ConstInit::FClassImplementedInterface> Interfaces = Class->CompiledInInterfaces;
			if (UVerseClass* VerseClass = Cast<UVerseClass>(Class))
			{
				for (const UE::CodeGen::ConstInit::FClassImplementedInterface& ImplementedInterface : Interfaces)
				{
					if (ImplementedInterface.bVerseDirectInterface)
					{
						if (UVerseClass* InterfaceClass = CastChecked<UVerseClass>(ImplementedInterface.Class, ECastCheckedType::NullAllowed))
						{
							VerseClass->DirectInterfaces.Add(InterfaceClass);
						}
					}
				}
			}
			new (&Class->Interfaces) TArray<FImplementedInterface>(Interfaces);
		}

		Class->StaticLink();

		if (UVerseClass* VerseClass = Cast<UVerseClass>(Class))
		{
			VerseClass->MangledPackageVersePath = VerseClass->CompiledInMangledPackageVersePath;
			VerseClass->PackageRelativeVersePath = VerseClass->CompiledInPackageRelativeVersePath;
		}

		// Initialize functions of classes after the class is constructed
		for (UField* Field = Class->Children; Field; Field = Field->Next)
		{
			ConstructObject(Field);
		}

		UE_LOG(LogUObjectBootstrap, Verbose, TEXT("UObjectLoadAllCompiledInDefaultProperties After Registrant %s %s"), *Class->GetPackage()->GetName(), *Class->GetName());
	});

}

void FCompiledInObjectRegistry::CreateClassDefaultObjects(FName InModuleName)
{
	TRACE_LOADTIME_REQUEST_GROUP_SCOPE(TEXT("FCompiledInObjectRegistry::CreateClassDefaultObjects"));
	SCOPED_BOOT_TIMING("FCompiledInObjectRegistry::CreateClassDefaultObjects");

	FRegisterCompiledInObjects* Stop = AlreadyConstructedDefaultObjects;
	AlreadyConstructedDefaultObjects = ListHead;

	FCoreUObjectDelegates::CompiledInUObjectsRegisteredDelegate.Broadcast(InModuleName, ECompiledInUObjectsRegisteredStatus::PreCDO);

	static FName LongEnginePackageName(TEXT("/Script/Engine"));

	TArray<UClass*> NewClasses;
	TArray<UClass*> NewClassesInCoreUObject;
	TArray<UClass*> NewClassesInEngine;
	IterateClasses(Stop, [&](UClass* Class){
		FName PackageName = Class->GetOutermost()->GetFName();
		if (PackageName == GLongCoreUObjectPackageName)
		{
			NewClassesInCoreUObject.Add(Class);
		}
		else if (PackageName == LongEnginePackageName)
		{
			NewClassesInEngine.Add(Class);
		}
		else
		{
			NewClasses.Add(Class);
		}
	});

	// Sort classes by class hierarchy to avoid deep recursion in creating default objects - registration is reversed because of linked list add-at-head behavior
	auto SortClasses = [](UClass* A, UClass* B)
	{
		if (B->IsChildOf(A))
		{
			return true;
		}	
		return false;
	};
	Algo::Sort(NewClassesInCoreUObject, SortClasses);
	Algo::Sort(NewClassesInEngine, SortClasses);
	Algo::Sort(NewClasses, SortClasses);
	
	// notify async loader of all new classes before creating the class default objects
	for (const TArray<UClass*>* Array : {&NewClassesInCoreUObject, &NewClassesInEngine, &NewClasses })
	{
		SCOPED_BOOT_TIMING("NotifyClassFinishedRegistrationEvents");
		for (UClass* Class : *Array)
		{
			NotifyRegistrationEvent(
				Class->GetPackage()->GetFName(),
				Class->GetFName(),
				ENotifyRegistrationType::NRT_Class,
				ENotifyRegistrationPhase::NRP_Finished,
				nullptr,
				false,
				Class
			);
		}
	}

	if (!NewClassesInCoreUObject.IsEmpty())
	{
		SCOPED_BOOT_TIMING("CoreUObject Classes");
		for (UClass* Class : NewClassesInCoreUObject) // we do these first because we assume these never trigger loads
		{
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject Begin %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			Class->GetDefaultObject();
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject End %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
		}
	}
	if (!NewClassesInEngine.IsEmpty())
	{
		SCOPED_BOOT_TIMING("Engine Classes");
		for (UClass* Class : NewClassesInEngine) // we do these second because we want to bring the engine up before the game
		{
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject Begin %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			Class->GetDefaultObject();
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject End %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
		}
	}
	if (!NewClasses.IsEmpty())
	{
		SCOPED_BOOT_TIMING("Other Classes");
		for (UClass* Class : NewClasses)
		{
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject Begin %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			Class->GetDefaultObject();
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject End %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
		}
	}
	FFeedbackContext& ErrorsFC = UClass::GetDefaultPropertiesFeedbackContext();
	if (ErrorsFC.GetNumErrors() || ErrorsFC.GetNumWarnings())
	{
		TArray<FString> AllErrorsAndWarnings;
		ErrorsFC.GetErrorsAndWarningsAndEmpty(AllErrorsAndWarnings);

		FString AllInOne;
		UE_LOG(LogUObjectBootstrap, Warning, TEXT("-------------- Default Property warnings and errors:"));
		for (const FString& ErrorOrWarning : AllErrorsAndWarnings)
		{
			UE_LOG(LogUObjectBootstrap, Warning, TEXT("%s"), *ErrorOrWarning);
			AllInOne += ErrorOrWarning;
			AllInOne += TEXT("\n");
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format( NSLOCTEXT("Core", "DefaultPropertyWarningAndErrors", "Default Property warnings and errors:\n{0}"), FText::FromString( AllInOne ) ) );
	}
}

void FCompiledInObjectRegistry::AssembleReferenceTokenStream()
{
	IterateClasses(nullptr, [](UClass* Class)
	{
		// Assemble reference token stream for garbage collection/ RTGC.
		if (!Class->HasAnyFlags(RF_ClassDefaultObject) && !Class->HasAnyClassFlags(CLASS_TokenStreamAssembled))
		{
			Class->AssembleReferenceTokenStream();
		}
	});
}





void FRegisterCompiledInObjects::Register()
{
	FCompiledInObjectRegistry::Get().AddObjects(this);
}

void FField::InitializeConstInitField(FField* InOwner)
{
#if DO_GUARD_SLOW
	bool bFound = false;
	for (FField* ExistingOwner = Owner.ToFieldUnsafe(); ExistingOwner; ExistingOwner = ExistingOwner->Owner.ToFieldUnsafe())
	{
		if (ExistingOwner == InOwner)
		{
			bFound = true;
			break;
		}
	}
	checkSlow(bFound);
#endif
	Owner = FFieldVariant(Owner.ToFieldUnsafe());
	checkSlow(!Owner.IsUObject());
	checkSlow((void*)NameTempUTF8 != (void*)GetFieldClassPrivate());
	NamePrivate = NameTempUTF8;
	ClassPrivate = GetFieldClassPrivate();
	checkSlow(ClassPrivate != nullptr);
}

void FField::InitializeConstInitField(UObject* InOwner)
{
	checkSlow(Owner.GetRawPointer() == InOwner);
	Owner = FFieldVariant(Owner.ToUObjectUnsafe());
	checkSlow(Owner.IsUObject());
	checkSlow((void*)NameTempUTF8 != (void*)GetFieldClassPrivate());
	NamePrivate = NameTempUTF8;
	ClassPrivate = GetFieldClassPrivate();
	checkSlow(ClassPrivate != nullptr);
}

static void InitializeConstInitProperties(UStruct* Struct)
{
	checkSlow(Struct->IsA<UObject>());
	TArray<FField*> InnerFields;
	for (FProperty* Property = static_cast<FProperty*>(Struct->ChildProperties); Property; Property = static_cast<FProperty*>(Property->Next))
	{
		checkfSlow(Property->InternalGetOwnerAsUObjectUnsafe() == Struct, TEXT("ChildProperties linked list should only contain fields from a single struct in a hierarchy"));
		Property->InitializeConstInitProperty(Struct);
		InnerFields.Reset();
		Property->GetInnerFields(InnerFields);
		for (FField* Inner : InnerFields)
		{
			static_cast<FProperty*>(Inner)->InitializeConstInitProperty(Property);
		}
	}
}

void FProperty::InitializeConstInitProperty(UStruct* InStructOwner)
{
	InitializeConstInitField(InStructOwner);
	if (RepNotifyFuncNameUTF8)
	{
		RepNotifyFunc = FName(RepNotifyFuncNameUTF8);
		DestructorLinkNext = nullptr;
	}
#if WITH_METADATA
	InitializeMetaData();
#endif
}

void FProperty::InitializeConstInitProperty(FProperty* InPropertyOwner)
{
	InitializeConstInitField(InPropertyOwner);
	if (RepNotifyFuncNameUTF8)
	{
		RepNotifyFunc = FName(RepNotifyFuncNameUTF8);
		DestructorLinkNext = nullptr;
	}
#if WITH_METADATA
	InitializeMetaData();
#endif
}

#if WITH_METADATA
void FProperty::InitializeMetaData()
{
	TConstArrayView<UE::CodeGen::ConstInit::FMetaData> MetaDataArray = MakeArrayView(MetaDataParams, NumMetaDataParams);
	// Overwrite fields that were unioned with metadata params
	PropertyLinkNext = nullptr;
#if WITH_EDITORONLY_DATA
	IndexInOwner = INDEX_NONE;
#endif
	for (const UE::CodeGen::ConstInit::FMetaData& MetaDataData : MetaDataArray)
	{
		SetMetaData(FName(MetaDataData.NameUTF8), UTF8_TO_TCHAR(MetaDataData.ValueUTF8));
	}
}
#endif // WITH_METADATA

// Perform basic initialization of an object that was constructed at compile time
//	Initialize its name from a static string stored at construction
//	Add the object to the object array / object hash
// 	Clear flags that were set at construction/by AddObject
void UObjectBase::AddConstInitObject()
{
	FName Name = GetUninitializedName();
	AddObject(Name, EInternalObjectFlags::None);
	ObjectFlags &= ~RF_NeedInitialization;
	GUObjectArray.IndexToObject(InternalIndex)->ClearFlags(EInternalObjectFlags::PendingConstruction);
	check(!GUObjectArray.IsDisregardForGC(this) || GUObjectArray.IndexToObject(InternalIndex)->IsRootSet());
	checkSlow(ClassPrivate);
	checkSlow(!(OuterPrivate) != !(ClassPrivate->IsChildOf<UPackage>())); // logical xor
}

void UClass::InitFuncMap()
{
	// Called during UObject init, should be no lock contention
	{
		FUClassFuncScopeWriteLock ScopeLock(FuncMapLock);
		for (UField* Field = Children; Field; Field = Field->Next)
		{
			if (UFunction* Function = Cast<UFunction>(Field))
			{
				FuncMap.Add(Function->GetFName(), Function);
			}
		}
	}

#if DO_CHECK
	{
		FUClassFuncScopeWriteLock ScopeLock(AllFunctionsCacheLock);
		check(AllFunctionsCache.IsEmpty());
	}
#endif
}

#endif // UE_WITH_CONSTINIT_UOBJECT 