// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGModule.h"

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGGraph.h"
#include "Compute/PCGComputeKernel.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGPolygon2DData.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Elements/PCGDifferenceElement.h"
#include "Hash/PCGGraphHashContext.h"
#include "Hash/PCGSettingsHashContext.h"
#include "Tests/Determinism/PCGDeterminismNativeTests.h"
#include "Tests/Determinism/PCGDifferenceDeterminismTest.h"

#include "ISettingsModule.h"
#include "ShaderCore.h"
#include "ShowFlags.h"
#include "Misc/Paths.h"
#endif

LLM_DEFINE_TAG(PCG);

#define LOCTEXT_NAMESPACE "FPCGModule"

#if WITH_EDITOR
FPCGGraphChangedDelegate FPCGModule::OnGraphChangedDelegate;
#endif

namespace PCGModule
{
	FPCGModule* PCGModulePtr = nullptr;

	static const TCHAR* DataTypeRegistryError = TEXT("Data type operations like compatibility or intersection can't run before the module is loaded."
		"The logic will not function in a CDO and must be deferred until after the load.");
}

FPCGModule& FPCGModule::GetPCGModuleChecked()
{
	check(PCGModule::PCGModulePtr);
	return *PCGModule::PCGModulePtr;
}

const FPCGDataTypeRegistry& FPCGModule::GetConstDataTypeRegistry()
{
	checkf(IsPCGModuleLoaded(), TEXT("%s"), PCGModule::DataTypeRegistryError);
	return GetPCGModuleChecked().DataTypeRegistry;
}

FPCGDataTypeRegistry& FPCGModule::GetMutableDataTypeRegistry()
{
	checkf(IsPCGModuleLoaded(), TEXT("%s"), PCGModule::DataTypeRegistryError);
	return GetPCGModuleChecked().DataTypeRegistry;
}

bool FPCGModule::IsPCGModuleLoaded()
{
	return PCGModule::PCGModulePtr != nullptr;
}

void FPCGModule::StartupModule()
{
	LLM_SCOPE_BYTAG(PCG);

	// Cache for fast access
	check(!PCGModule::PCGModulePtr);
	PCGModule::PCGModulePtr = this;
	
#if WITH_EDITOR
	PCGDeterminismTests::FNativeTestRegistry::Create();

	RegisterNativeElementDeterminismTests();

	FEngineShowFlags::RegisterCustomShowFlag(PCGEngineShowFlags::Debug, /*DefaultEnabled=*/true, EShowFlagGroup::SFG_Developer, LOCTEXT("ShowFlagDisplayName", "PCG Debug"));

	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("PCG"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/PCG"), PluginShaderDir);

	LoadAdditionalKernelSources();
#endif // WITH_EDITOR

	// Registering accessor methods
	AttributeAccessorFactory.RegisterDefaultMethods();
	AttributeAccessorFactory.RegisterMethods<UPCGBasePointData>(UPCGBasePointData::GetPointAccessorMethods());
	// @todo_pcg: Eventually remove the UPCGPointData method registration because the UPCGBasePointData accessors are compatible
	AttributeAccessorFactory.RegisterMethods<UPCGPointData>(UPCGPointData::GetPointAccessorMethods());
	AttributeAccessorFactory.RegisterMethods<UPCGPolygon2DData>(UPCGPolygon2DData::GetPolygon2DAccessorMethods());
	AttributeAccessorFactory.RegisterMethods<UPCGSplineData>(UPCGSplineData::GetSplineAccessorMethods());

#if WITH_EDITOR
	ObjectHashFactory.RegisterObjectHashContextFactory(UPCGGraphInterface::StaticClass(), FPCGOnCreateObjectHashContext::CreateStatic(&FPCGGraphHashContext::MakeInstance));
	ObjectHashFactory.RegisterObjectHashContextFactory(UPCGSettingsInterface::StaticClass(), FPCGOnCreateObjectHashContext::CreateStatic(&FPCGSettingsHashContext::MakeInstance));
#endif

	// Register onto the PreExit, because we need the class to be still valid to remove them from the mapping
	FCoreDelegates::OnPreExit.AddRaw(this, &FPCGModule::PreExit);
	// Also register on post init to gather all PCG types to register them
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPCGModule::OnPostInitEngine);

	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FPCGModule::Tick));
}

void FPCGModule::ShutdownModule()
{
	FTSTicker::RemoveTicker(TickDelegateHandle);

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);

	PCGModule::PCGModulePtr = nullptr;
}

void FPCGModule::PreExit()
{
	// Unregistering accessor methods
	AttributeAccessorFactory.UnregisterMethods<UPCGSplineData>();
	AttributeAccessorFactory.UnregisterMethods<UPCGPolygon2DData>();
	AttributeAccessorFactory.UnregisterMethods<UPCGPointData>();
	AttributeAccessorFactory.UnregisterMethods<UPCGBasePointData>();
	AttributeAccessorFactory.UnregisterDefaultMethods();

	DataTypeRegistry.Shutdown();

#if WITH_EDITOR
	DeregisterNativeElementDeterminismTests();

	PCGDeterminismTests::FNativeTestRegistry::Destroy();
#endif // WITH_EDITOR
}

void FPCGModule::OnPostInitEngine()
{
	LLM_SCOPE_BYTAG(PCG);
	DataTypeRegistry.RegisterKnownTypes();
}

void FPCGModule::ExecuteNextTick(TFunction<void()> TickFunction)
{
	FScopeLock Lock(&ExecuteNextTickLock);
	ExecuteNextTicks.Add(TickFunction);
}

bool FPCGModule::Tick(float DeltaTime)
{
	LLM_SCOPE_BYTAG(PCG);

	TArray<TFunction<void()>> LocalExecuteNextTicks;

	{
		FScopeLock Lock(&ExecuteNextTickLock);
		LocalExecuteNextTicks = MoveTemp(ExecuteNextTicks);
	}

	for (TFunction<void()>& LocalExecuteNextTick : LocalExecuteNextTicks)
	{
		LocalExecuteNextTick();
	}

	return true;
}

#if WITH_EDITOR
// todo_pcg: Could move to editor module once PCGComputeKernel.h is publicly exposed.
void FPCGModule::LoadAdditionalKernelSources()
{
	auto LoadSources = [](TConstArrayView<FSoftObjectPath> SourcePaths)
	{
		for (const FSoftObjectPath& SourcePath : SourcePaths)
		{
			if (!ensure(SourcePath.TryLoad()))
			{
				UE_LOG(LogPCG, Error, TEXT("Failed to load compute source asset '%s'."), *SourcePath.ToString());
			}
		}
	};

	// Load all additional sources needed by all kernels in PCG plugin here.
	LoadSources(PCGComputeKernel::GetDefaultAdditionalSourcePaths());
}

void FPCGModule::RegisterNativeElementDeterminismTests()
{
	PCGDeterminismTests::FNativeTestRegistry::RegisterTestFunction(UPCGDifferenceSettings::StaticClass(), PCGDeterminismTests::DifferenceElement::RunTestSuite);
	// TODO: Add other native test functions
}

void FPCGModule::DeregisterNativeElementDeterminismTests()
{
	PCGDeterminismTests::FNativeTestRegistry::DeregisterTestFunction(UPCGDifferenceSettings::StaticClass());
}
#endif // WITH_EDITOR

IMPLEMENT_MODULE(FPCGModule, PCG);

PCG_API DEFINE_LOG_CATEGORY(LogPCG);

void PCGLog::LogErrorOnGraph(const FText& InMsg, const FPCGContext* InContext)
{
	if (InContext)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, InMsg);
	}
	else
	{
		UE_LOG(LogPCG, Error, TEXT("%s"), *InMsg.ToString());
	}
}

void PCGLog::LogWarningOnGraph(const FText& InMsg, const FPCGContext* InContext)
{
	if (InContext)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, InMsg);
	}
	else
	{
		UE_LOG(LogPCG, Warning, TEXT("%s"), *InMsg.ToString());
	}
}

#undef LOCTEXT_NAMESPACE
