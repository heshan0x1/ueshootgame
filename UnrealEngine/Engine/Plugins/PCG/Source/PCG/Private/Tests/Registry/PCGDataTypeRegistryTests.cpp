// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Tests/PCGTestsCommon.h"

#include "PCGData.h"
#include "PCGModule.h"
#include "PCGParamData.h"
#include "Compute/Data/PCGProxyForGPUData.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGLandscapeData.h"
#include "Data/PCGLandscapeSplineData.h"
#include "Data/PCGPolygon2DData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGRenderTargetData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGStaticMeshResourceData.h"
#include "Data/PCGTextureData.h"
#include "Data/PCGVirtualTextureData.h"
#include "Data/PCGVolumeData.h"

class FPCGDataTypeRegistryTestBase : public FPCGTestBaseClass
{
	using FPCGTestBaseClass::FPCGTestBaseClass;

protected:
	static bool IsRegistered(const FPCGDataTypeBaseId& ID)
	{
		return FPCGModule::GetConstDataTypeRegistry().IsRegistered(ID);
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGByClassTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.ByClass", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGByDefaultTypeTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.ByDefaultType", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGCompatibilityTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Compatibility", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAggregationTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Aggregation", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Intersection", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGCompositionTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Composition", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGUnicityTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Unicity", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGWiderTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Wider", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGUnionTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.Union", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIsChildOfTest, FPCGDataTypeRegistryTestBase, "Plugins.PCG.DataTypeRegistry.IsChildOf", PCGTestsCommon::TestFlags)

bool FPCGByClassTest::RunTest(const FString& Parameters)
{
	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();

	TArray<FPCGDataTypeIdentifier> Identifiers;

	TTuple<const TCHAR*, FPCGDataTypeIdentifier> TestValues[] =
	{
		{TEXT("Any"), FPCGDataTypeIdentifier::Construct<UPCGData>()},
		{TEXT("Spatial"), FPCGDataTypeIdentifier::Construct<UPCGSpatialData>()},
		{TEXT("Point"), FPCGDataTypeIdentifier::Construct<UPCGBasePointData>()},
		{TEXT("Volume"), FPCGDataTypeIdentifier::Construct<UPCGVolumeData>()},
		{TEXT("Primitive"), FPCGDataTypeIdentifier::Construct<UPCGPrimitiveData>()},
		{TEXT("Polyline"), FPCGDataTypeIdentifier::Construct<UPCGPolyLineData>()},
		{TEXT("Spline"), FPCGDataTypeIdentifier::Construct<UPCGSplineData>()},
		{TEXT("Polygon2D"), FPCGDataTypeIdentifier::Construct<UPCGPolygon2DData>()},
		{TEXT("Landscape spline"), FPCGDataTypeIdentifier::Construct<UPCGLandscapeSplineData>()},
		{TEXT("Landscape"), FPCGDataTypeIdentifier::Construct<UPCGLandscapeData>()},
		{TEXT("Base texture"), FPCGDataTypeIdentifier::Construct<UPCGBaseTextureData>()},
		{TEXT("Texture"), FPCGDataTypeIdentifier::Construct<UPCGTextureData>()},
		{TEXT("Proxy for GPU"), FPCGDataTypeIdentifier::Construct<UPCGProxyForGPUData>()},
		{TEXT("Static mesh resource"), FPCGDataTypeIdentifier::Construct<UPCGStaticMeshResourceData>()},
		{TEXT("Virtual Texture"), FPCGDataTypeIdentifier::Construct<UPCGVirtualTextureData>()},
		{TEXT("Render target"), FPCGDataTypeIdentifier::Construct<UPCGRenderTargetData>()},
		{TEXT("Settings"), FPCGDataTypeIdentifier::Construct<UPCGSettings>()},
		{TEXT("Param"), FPCGDataTypeIdentifier::Construct<UPCGParamData>()},
		{TEXT("DynamicMesh"), FPCGDataTypeIdentifier::Construct<UPCGDynamicMeshData>()},
	};

	for (const auto& [TypeName, Identifier] : TestValues)
	{
		UTEST_TRUE(FString::Printf(TEXT("%s type is registered"), TypeName), Identifier.IsValid());
		
		UTEST_FALSE(
			FString::Printf(TEXT("%s type identifier is unique"), TypeName),
			Identifiers.ContainsByPredicate([&Identifier](const FPCGDataTypeIdentifier& Other) { return Identifier.IsSameType(Other); })
		);
		
		Identifiers.Add(Identifier);
	}
	
	return true;
}

bool FPCGByDefaultTypeTest::RunTest(const FString& Parameters)
{
	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();

	// Test invalid first
	UTEST_FALSE(FString::Printf(TEXT("%s type is invalid"), TEXT("None")), FPCGDataTypeIdentifier{EPCGDataType::None}.IsValid());

	TArray<FPCGDataTypeIdentifier> Identifiers;

	TTuple<const TCHAR*, FPCGDataTypeIdentifier> TestValues[] =
	{
		{TEXT("Any"), FPCGDataTypeIdentifier(EPCGDataType::Any)},
		{TEXT("Spatial"), FPCGDataTypeIdentifier(EPCGDataType::Spatial)},
		{TEXT("Point"), FPCGDataTypeIdentifier(EPCGDataType::Point)},
		{TEXT("Volume"), FPCGDataTypeIdentifier(EPCGDataType::Volume)},
		{TEXT("Primitive"), FPCGDataTypeIdentifier(EPCGDataType::Primitive)},
		{TEXT("Polyline"), FPCGDataTypeIdentifier(EPCGDataType::PolyLine)},
		{TEXT("Spline"), FPCGDataTypeIdentifier(EPCGDataType::Spline)},
		{TEXT("Polygon2D"), FPCGDataTypeIdentifier(EPCGDataType::Polygon2D)},
		{TEXT("Landscape spline"), FPCGDataTypeIdentifier(EPCGDataType::LandscapeSpline)},
		{TEXT("Landscape"), FPCGDataTypeIdentifier(EPCGDataType::Landscape)},
		{TEXT("Base texture"), FPCGDataTypeIdentifier(EPCGDataType::BaseTexture)},
		{TEXT("Texture"), FPCGDataTypeIdentifier(EPCGDataType::Texture)},
		{TEXT("Proxy for GPU"), FPCGDataTypeIdentifier(EPCGDataType::ProxyForGPU)},
		{TEXT("Static mesh resource"), FPCGDataTypeIdentifier(EPCGDataType::StaticMeshResource)},
		{TEXT("Virtual Texture"), FPCGDataTypeIdentifier(EPCGDataType::VirtualTexture)},
		{TEXT("Render target"),FPCGDataTypeIdentifier(EPCGDataType::RenderTarget)},
		{TEXT("Settings"), FPCGDataTypeIdentifier(EPCGDataType::Settings)},
		{TEXT("Param"), FPCGDataTypeIdentifier(EPCGDataType::Param)},
		{TEXT("PointOrParam"), FPCGDataTypeIdentifier(EPCGDataType::PointOrParam)},
		{TEXT("DynamicMesh"), FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh)}
	};

	for (const auto& [TypeName, Identifier] : TestValues)
	{
		UTEST_TRUE(FString::Printf(TEXT("%s type is valid"), TypeName), Identifier.IsValid());
		bool bRegistered = Algo::AllOf(Identifier.GetIds(), [](const FPCGDataTypeBaseId ID) { return IsRegistered(ID); });
		
		UTEST_TRUE(FString::Printf(TEXT("%s type is registered"), TypeName), bRegistered);
		
		UTEST_FALSE(
			FString::Printf(TEXT("%s type identifier is unique"), TypeName),
			Identifiers.ContainsByPredicate([&Identifier](const FPCGDataTypeIdentifier& Other) { return Identifier.IsSameType(Other); })
		);
		
		Identifiers.Add(Identifier);
	}
	
	return true;
}

bool FPCGCompatibilityTest::RunTest(const FString& Parameters)
{
	TTuple<FPCGDataTypeIdentifier, FPCGDataTypeIdentifier, EPCGDataTypeCompatibilityResult> TestValues[] =
	{
		{EPCGDataType::Any, EPCGDataType::None, EPCGDataTypeCompatibilityResult::UnknownType},
		{EPCGDataType::None, EPCGDataType::None, EPCGDataTypeCompatibilityResult::UnknownType},
		{EPCGDataType::None, EPCGDataType::Any, EPCGDataTypeCompatibilityResult::UnknownType},
		
		{EPCGDataType::Any, EPCGDataType::Spatial, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Any, EPCGDataType::Concrete, EPCGDataTypeCompatibilityResult::RequireConversion},
		{EPCGDataType::Any, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireFilter},
		
		{EPCGDataType::None, EPCGDataType::None, EPCGDataTypeCompatibilityResult::UnknownType},
		
		{EPCGDataType::Spatial, EPCGDataType::Any, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Point, EPCGDataType::Any, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Any, EPCGDataType::Any, EPCGDataTypeCompatibilityResult::Compatible},
		
		{EPCGDataType::Point, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::Compatible},
		
		{EPCGDataType::Spatial, EPCGDataType::Concrete, EPCGDataTypeCompatibilityResult::RequireConversion},
		{EPCGDataType::Spatial, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireConversion},
		{EPCGDataType::Spatial, EPCGDataType::Volume, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Spatial, EPCGDataType::Surface, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Spatial, EPCGDataType::Spline, EPCGDataTypeCompatibilityResult::RequireFilter},
		
		{EPCGDataType::Composite, EPCGDataType::Concrete, EPCGDataTypeCompatibilityResult::RequireConversion},
		
		{EPCGDataType::Spline, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireConversion},
		{EPCGDataType::Volume, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireConversion},
		
		{EPCGDataType::Spline, EPCGDataType::Surface, EPCGDataTypeCompatibilityResult::RequireConversion},
		{EPCGDataType::Polygon2D, EPCGDataType::Surface, EPCGDataTypeCompatibilityResult::RequireConversion},
		
		{EPCGDataType::Spline, EPCGDataType::LandscapeSpline, EPCGDataTypeCompatibilityResult::NotCompatible},
		{EPCGDataType::Spline, EPCGDataType::PolyLine, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::LandscapeSpline, EPCGDataType::PolyLine, EPCGDataTypeCompatibilityResult::Compatible},
		
		{EPCGDataType::Landscape, EPCGDataType::Texture, EPCGDataTypeCompatibilityResult::NotCompatible},
		{EPCGDataType::Landscape, EPCGDataType::Surface, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Surface, EPCGDataType::Landscape, EPCGDataTypeCompatibilityResult::RequireFilter},

		// Explicit construction of overlapping types
		{EPCGDataType::Surface, FPCGDataTypeIdentifier::Construct(EPCGDataType::Surface, EPCGDataType::Landscape), EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Landscape, FPCGDataTypeIdentifier::Construct(EPCGDataType::Surface, EPCGDataType::Landscape), EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Surface, FPCGDataTypeIdentifier::Construct(EPCGDataType::Landscape, EPCGDataType::Surface), EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Landscape, FPCGDataTypeIdentifier::Construct(EPCGDataType::Landscape, EPCGDataType::Surface), EPCGDataTypeCompatibilityResult::Compatible},
		
		{EPCGDataType::RenderTarget, EPCGDataType::Texture, EPCGDataTypeCompatibilityResult::NotCompatible},
		{EPCGDataType::BaseTexture, EPCGDataType::Texture, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Texture, EPCGDataType::BaseTexture, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::BaseTexture, EPCGDataType::RenderTarget, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::RenderTarget, EPCGDataType::BaseTexture, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::VirtualTexture, EPCGDataType::Texture, EPCGDataTypeCompatibilityResult::NotCompatible},
		{EPCGDataType::VirtualTexture, EPCGDataType::Surface, EPCGDataTypeCompatibilityResult::Compatible},
		
		{EPCGDataType::Point, EPCGDataType::Param, EPCGDataTypeCompatibilityResult::RequireConversion},
		{EPCGDataType::Param, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireConversion},
		
		{EPCGDataType::PointOrParam, EPCGDataType::Param, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::PointOrParam, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Point, EPCGDataType::PointOrParam, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Param, EPCGDataType::Param, EPCGDataTypeCompatibilityResult::Compatible},


		{EPCGDataType::Spline | EPCGDataType::Point, EPCGDataType::Spline, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Spline | EPCGDataType::Point, EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireFilter},
		{EPCGDataType::Spline | EPCGDataType::Point, EPCGDataType::Param, EPCGDataTypeCompatibilityResult::NotCompatible},
		{EPCGDataType::Spline | EPCGDataType::Point, EPCGDataType::Spline | EPCGDataType::Landscape, EPCGDataTypeCompatibilityResult::RequireFilter},
		
		{EPCGDataType::Param, EPCGDataType::Spline | EPCGDataType::Point, EPCGDataTypeCompatibilityResult::NotCompatible},
		{EPCGDataType::Spline, EPCGDataType::Spline | EPCGDataType::Point, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Point, EPCGDataType::Spline | EPCGDataType::Point, EPCGDataTypeCompatibilityResult::Compatible},
		{EPCGDataType::Spline | EPCGDataType::Landscape, EPCGDataType::Spline | EPCGDataType::Point, EPCGDataTypeCompatibilityResult::RequireFilter},
	};

	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();

	for (const auto& [TypeA, TypeB, ExpectedCompatibility] : TestValues)
	{
		const FString TypeAName = TypeA.ToString();
		const FString TypeBName = TypeB.ToString();

		
		UTEST_EQUAL(
			FString::Printf(TEXT("Compatiblity between %s and %s is expected."),
				*TypeAName,
				*TypeBName),
			static_cast<int64>(Registry.IsCompatible(TypeA, TypeB)),
			static_cast<int64>(ExpectedCompatibility)
		);
	}

	return true;
}

/**
 * Aggregation should return all the base types.
 */
bool FPCGAggregationTest::RunTest(const FString& Parameters)
{
	TTuple<FPCGDataTypeIdentifier, FPCGDataTypeIdentifier> TestValues[] =
	{
		{
			FPCGDataTypeIdentifier{EPCGDataType::Point} | EPCGDataType::None, 
			FPCGDataTypeIdentifier::Construct<UPCGPointData>()
		},
		{ 
			EPCGDataType::Point | EPCGDataType::Param, 
			FPCGDataTypeIdentifier::Construct<UPCGPointData, UPCGParamData>()
		},
		{
			EPCGDataType::Point | EPCGDataType::Param,
			FPCGDataTypeIdentifier::Construct<UPCGParamData, UPCGPointData>()
		},
		{
			EPCGDataType::PointOrParam,
			FPCGDataTypeIdentifier::Construct<UPCGPointArrayData, UPCGParamData>()
		},
		{
			EPCGDataType::PointOrParam,
			FPCGDataTypeIdentifier::Construct<UPCGParamData, UPCGBasePointData>()
		},
		{
			EPCGDataType::Spline | EPCGDataType::Volume,
			FPCGDataTypeIdentifier::Construct<UPCGSplineData, UPCGVolumeData>()
		},
		{
			EPCGDataType::Texture | EPCGDataType::Point | EPCGDataType::Landscape,
			FPCGDataTypeIdentifier::Construct<UPCGTextureData, UPCGBasePointData, UPCGLandscapeData>()
		},
	};

	const UEnum* Enum = StaticEnum<EPCGDataType>();
	check(Enum);

	for (const auto& [Type, ExpectedIdentifier] : TestValues)
	{
		const FPCGDataTypeIdentifier TypeIdentifier{Type};
		
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const FString TypeName = Enum->GetValueOrBitfieldAsString(static_cast<int64>(Type));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		
		UTEST_TRUE(FString::Printf(TEXT("Composition for %s is valid."), *TypeName), TypeIdentifier.IsSameType(ExpectedIdentifier))
	}

	return true;
}

/**
 * Intersection should return the common type, or none if there is no common type.
 */
bool FPCGIntersectionTest::RunTest(const FString& Parameters)
{
	TTuple<TArray<EPCGDataType>, FPCGDataTypeIdentifier> TestValues[] =
	{
		{ 
			TArray{EPCGDataType::Point, EPCGDataType::None}, 
			FPCGDataTypeIdentifier{}
		},
		{ 
			TArray{EPCGDataType::Point, EPCGDataType::Param}, 
			FPCGDataTypeIdentifier{}
		},
		{
			TArray{EPCGDataType::Spline, EPCGDataType::Volume},
			FPCGDataTypeIdentifier{}
		},
		{
			TArray{EPCGDataType::Surface, EPCGDataType::Point, EPCGDataType::Landscape},
			FPCGDataTypeIdentifier{}
		},
		{
			TArray{EPCGDataType::Texture,  EPCGDataType::Texture},
			FPCGDataTypeIdentifier{EPCGDataType::Texture}
		},
		{
			TArray{EPCGDataType::Point,  EPCGDataType::PointOrParam},
			FPCGDataTypeIdentifier{EPCGDataType::Point}
		},
		{
			TArray{EPCGDataType::Param,  EPCGDataType::PointOrParam},
			FPCGDataTypeIdentifier{EPCGDataType::Param}
		},
		{
			TArray{EPCGDataType::Surface,  EPCGDataType::Landscape},
			FPCGDataTypeIdentifier{EPCGDataType::Landscape}
		},
		{
			TArray{EPCGDataType::Surface,  EPCGDataType::Texture},
			FPCGDataTypeIdentifier{EPCGDataType::Texture}
		},
		{
			TArray{EPCGDataType::Any,  EPCGDataType::Spatial},
			FPCGDataTypeIdentifier{EPCGDataType::Spatial}
		},
	};

	const UEnum* Enum = StaticEnum<EPCGDataType>();
	check(Enum);

	for (const auto& [Types, ExpectedIdentifier] : TestValues)
	{
		FPCGDataTypeIdentifier TypeIdentifier{Types[0]};
		FString TypesNames = TypeIdentifier.ToString();
		for (int32 i = 1; i < Types.Num(); ++i)
		{
			const FPCGDataTypeIdentifier OtherType{Types[i]};
			TypeIdentifier &= OtherType;
			TypesNames += TEXT(" & ") + OtherType.ToString();
		}

		UTEST_TRUE(FString::Printf(TEXT("Intersection for %s is %s."), *TypesNames, *ExpectedIdentifier.ToString()), TypeIdentifier.IsSameType(ExpectedIdentifier))
	}

	return true;
}

/**
 * Composition: Return the aggregation with other types, with reduction. (ie. if Data B and C are child of Data A, Composition of B and C gives A)
 */
bool FPCGCompositionTest::RunTest(const FString& Parameters)
{
	TTuple<TArray<FPCGDataTypeIdentifier>, FPCGDataTypeIdentifier> TestValues[] =
	{
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Point}, FPCGDataTypeIdentifier{EPCGDataType::None}},
			FPCGDataTypeIdentifier{EPCGDataType::Point}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Any}, FPCGDataTypeIdentifier{EPCGDataType::Spatial}},
			FPCGDataTypeIdentifier{EPCGDataType::Any}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Spatial}, FPCGDataTypeIdentifier{EPCGDataType::Concrete}},
			FPCGDataTypeIdentifier{EPCGDataType::Spatial}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Spatial}, (FPCGDataTypeIdentifier{EPCGDataType::Composite})},
			FPCGDataTypeIdentifier{EPCGDataType::Spatial}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Concrete}, FPCGDataTypeIdentifier{EPCGDataType::Composite}},
			FPCGDataTypeIdentifier{EPCGDataType::Spatial}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Texture}, FPCGDataTypeIdentifier{EPCGDataType::Composite}},
			FPCGDataTypeIdentifier{EPCGDataType::Texture | EPCGDataType::Composite}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Concrete}, FPCGDataTypeIdentifier{EPCGDataType::Point}},
			FPCGDataTypeIdentifier{EPCGDataType::Concrete}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Surface}, FPCGDataTypeIdentifier{EPCGDataType::Texture}},
			FPCGDataTypeIdentifier{EPCGDataType::Surface}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Spline}, FPCGDataTypeIdentifier{EPCGDataType::LandscapeSpline}, FPCGDataTypeIdentifier{EPCGDataType::Polygon2D}},
			FPCGDataTypeIdentifier{EPCGDataType::PolyLine}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Spline}, FPCGDataTypeIdentifier{EPCGDataType::LandscapeSpline}, FPCGDataTypeIdentifier{EPCGDataType::Polygon2D}, FPCGDataTypeIdentifier{EPCGDataType::Point} },
			FPCGDataTypeIdentifier::Construct<UPCGPolyLineData, UPCGPointData>()
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Texture}, FPCGDataTypeIdentifier{EPCGDataType::RenderTarget}},
			FPCGDataTypeIdentifier{EPCGDataType::BaseTexture}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Texture}, FPCGDataTypeIdentifier{EPCGDataType::RenderTarget}, FPCGDataTypeIdentifier{EPCGDataType::VirtualTexture}, FPCGDataTypeIdentifier{EPCGDataType::Landscape}},
			FPCGDataTypeIdentifier{EPCGDataType::Surface}
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Point}, FPCGDataTypeIdentifier{EPCGDataType::Param}},
			FPCGDataTypeIdentifier::Construct<UPCGPointData, UPCGParamData>()
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Point}, FPCGDataTypeIdentifier{EPCGDataType::Param}},
			FPCGDataTypeIdentifier::Construct<UPCGParamData, UPCGPointData>()
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Spline}, FPCGDataTypeIdentifier{EPCGDataType::Volume}},
			FPCGDataTypeIdentifier::Construct<UPCGSplineData, UPCGVolumeData>()
		},
		{
			TArray{FPCGDataTypeIdentifier{EPCGDataType::Texture}, FPCGDataTypeIdentifier{EPCGDataType::Point}, FPCGDataTypeIdentifier{EPCGDataType::Landscape}},
			FPCGDataTypeIdentifier::Construct<UPCGTextureData, UPCGPointData, UPCGLandscapeData>()
		},
	};

	for (const auto& [TypeIdentifiers, ExpectedIdentifier] : TestValues)
	{
		const FPCGDataTypeIdentifier CompositionType = FPCGDataTypeIdentifier::Compose(TypeIdentifiers);
		
		const FString TypeName = FPCGDataTypeIdentifier::Construct(TypeIdentifiers).ToString();
		const FString ExpectedTypeName = ExpectedIdentifier.ToString();

		UTEST_TRUE(FString::Printf(TEXT("Composition of %s gives %s."), *TypeName, *ExpectedTypeName), CompositionType.IsSameType(ExpectedIdentifier))
	}

	return true;
}

/**
 * We should never have duplicates in the IDs when we construct/combine/compose the same types.
 */
bool FPCGUnicityTest::RunTest(const FString& Parameters)
{
	const FPCGDataTypeIdentifier SameTypeWithClassTpl = FPCGDataTypeIdentifier::Construct<UPCGData, UPCGData>();
	UTEST_EQUAL("Construct with same class in template gives just 1 ID", SameTypeWithClassTpl.GetIds().Num(), 1)

	const FPCGDataTypeIdentifier ChildTypesWithClassTpl = FPCGDataTypeIdentifier::Construct<UPCGData, UPCGSpatialData, UPCGPointData>();
	UTEST_EQUAL("Construct with child classes in template gives just 1 ID", SameTypeWithClassTpl.GetIds().Num(), 1)
	UTEST_EQUAL("Construct with child classes in template gives the wider one", SameTypeWithClassTpl.GetId(), FPCGDataTypeInfo::AsId());

	const FPCGDataTypeIdentifier SameTypeWithClass = FPCGDataTypeIdentifier::Construct(UPCGPointData::StaticClass(), UPCGPointData::StaticClass());
	UTEST_EQUAL("Construct with same class gives just 1 ID", SameTypeWithClass.GetIds().Num(), 1)

	const FPCGDataTypeIdentifier SameTypeWithBaseIds = FPCGDataTypeIdentifier::Construct(FPCGDataTypeInfoLandscape::AsId(), FPCGDataTypeInfoLandscape::AsId());
	UTEST_EQUAL("Construct with same base ids gives just 1 ID", SameTypeWithBaseIds.GetIds().Num(), 1)
	
	const FPCGDataTypeIdentifier ChildTypeWithBaseIds = FPCGDataTypeIdentifier::Construct(FPCGDataTypeInfoLandscape::AsId(), FPCGDataTypeInfoSurface::AsId(), FPCGDataTypeInfoLandscape::AsId());
	UTEST_EQUAL("Construct with child base ids gives just 1 ID", ChildTypeWithBaseIds.GetIds().Num(), 1)
	UTEST_EQUAL("Construct with child base ids gives the wider one", ChildTypeWithBaseIds.GetId(), FPCGDataTypeInfoSurface::AsId())

	const FPCGDataTypeIdentifier SameTypeWithIds = FPCGDataTypeIdentifier::Construct(SameTypeWithClassTpl, SameTypeWithClassTpl, SameTypeWithClassTpl);
	UTEST_EQUAL("Construct with same ids gives just 1 ID", SameTypeWithIds.GetIds().Num(), 1)

	const FPCGDataTypeIdentifier SameTypeWithDefaultTypeOr = SameTypeWithClass | EPCGDataType::Point;
	UTEST_EQUAL("Bitwise Or with same default type gives just 1 ID", SameTypeWithDefaultTypeOr.GetIds().Num(), 1)

	const FPCGDataTypeIdentifier SameTypeWithOr = SameTypeWithClass | SameTypeWithClass;
	UTEST_EQUAL("Bitwise Or with same ids gives just 1 ID", SameTypeWithOr.GetIds().Num(), 1)

	FPCGDataTypeIdentifier SameTypeOr = SameTypeWithClass;
	SameTypeOr |= SameTypeWithClass;
	UTEST_EQUAL("Bitwise Or with self gives just 1 ID", SameTypeOr.GetIds().Num(), 1)

	SameTypeOr |= EPCGDataType::Point;
	UTEST_EQUAL("Bitwise Or on self with default type gives just 1 ID", SameTypeOr.GetIds().Num(), 1)

	const FPCGDataTypeIdentifier SameTypeCompose = SameTypeWithClass.Compose({SameTypeWithClass, SameTypeWithClass});
	UTEST_EQUAL("Compose with same type gives just 1 ID", SameTypeCompose.GetIds().Num(), 1)

	return true;
}

bool FPCGWiderTest::RunTest(const FString& Parameters)
{
	// Test: Left is wider than right. Result in the bool
	TTuple<FPCGDataTypeIdentifier, FPCGDataTypeIdentifier, bool> TestValues[] =
	{
		{EPCGDataType::Any, EPCGDataType::Concrete, true},
		{EPCGDataType::Concrete, EPCGDataType::Any, false},
		{EPCGDataType::PointOrParam, EPCGDataType::Point, true},
		{EPCGDataType::PointOrParam, EPCGDataType::Param, true},
		{EPCGDataType::Param, EPCGDataType::PointOrParam, false},
		{EPCGDataType::Point, EPCGDataType::PointOrParam, false},
		{EPCGDataType::PolyLine, EPCGDataType::Spline, true},
		{EPCGDataType::Spline, EPCGDataType::PolyLine, false},
		{EPCGDataType::Spline, EPCGDataType::LandscapeSpline, true},
		{EPCGDataType::LandscapeSpline, EPCGDataType::Spline, true},
		{EPCGDataType::Spline | EPCGDataType::Point, EPCGDataType::Point | EPCGDataType::LandscapeSpline, true}
	};

	for (const auto& [LeftIdentifier, RightIdentifier, ExpectedResult] : TestValues)
	{
		if (ExpectedResult)
		{
			UTEST_TRUE(FString::Printf(TEXT("%s is wider than %s."), *LeftIdentifier.ToString(), *RightIdentifier.ToString()), LeftIdentifier.IsWider(RightIdentifier))
		}
		else
		{
			UTEST_FALSE(FString::Printf(TEXT("%s is not wider than %s."), *LeftIdentifier.ToString(), *RightIdentifier.ToString()), LeftIdentifier.IsWider(RightIdentifier))
		}
	}

	return true;
}

bool FPCGUnionTest::RunTest(const FString& Parameters)
{
	TTuple<FPCGDataTypeIdentifier, FPCGDataTypeIdentifier> TestValues[] =
	{
		{FPCGDataTypeIdentifier{EPCGDataType::Point} | EPCGDataType::None, EPCGDataType::Point},
		{FPCGDataTypeIdentifier{EPCGDataType::Any} | EPCGDataType::Concrete, EPCGDataType::Any},
		{FPCGDataTypeIdentifier{EPCGDataType::Param} | EPCGDataType::Spatial, EPCGDataType::Any},
		{FPCGDataTypeIdentifier{EPCGDataType::Param} | EPCGDataType::Point, EPCGDataType::Any},
		{FPCGDataTypeIdentifier{EPCGDataType::Texture} | EPCGDataType::RenderTarget, EPCGDataType::BaseTexture},
		{FPCGDataTypeIdentifier{EPCGDataType::Landscape} | EPCGDataType::RenderTarget, EPCGDataType::Surface},
		{FPCGDataTypeIdentifier{EPCGDataType::PointOrParam}, EPCGDataType::Any},
		{FPCGDataTypeIdentifier{EPCGDataType::Point} | EPCGDataType::Landscape | EPCGDataType::Polygon2D, EPCGDataType::Concrete},
		{FPCGDataTypeIdentifier{EPCGDataType::Point} | EPCGDataType::Landscape | EPCGDataType::Polygon2D | EPCGDataType::Composite, EPCGDataType::Spatial},
	};
	
	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();
	
	for (const auto& [Identifiers, ExpectedIdentifier] : TestValues)
	{
		const FPCGDataTypeIdentifier Union = Registry.GetIdentifiersUnion({Identifiers});
		UTEST_TRUE(FString::Printf(TEXT("Union of %s is %s."), *Identifiers.ToString(), *ExpectedIdentifier.ToString()), Union.IsSameType(ExpectedIdentifier))
	}
	
	return true;
}

bool FPCGIsChildOfTest::RunTest(const FString& Parameters)
{
	TTuple<FPCGDataTypeIdentifier, FPCGDataTypeIdentifier, bool> TestValues[] =
	{
		{FPCGDataTypeInfo::AsId(), FPCGDataTypeInfo::AsId(), true},
		{FPCGDataTypeInfoPoint::AsId(), FPCGDataTypeInfo::AsId(), true},
		{FPCGDataTypeInfo::AsId(), FPCGDataTypeInfoPoint::AsId(), false},
		{FPCGDataTypeInfoSpline::AsId(), FPCGDataTypeInfoPolyline::AsId(), true},
		{FPCGDataTypeInfoSpline::AsId(), FPCGDataTypeInfoLandscapeSpline::AsId(), false},
		{FPCGDataTypeIdentifier::Construct(FPCGDataTypeInfoSpline::AsId(), FPCGDataTypeInfoLandscapeSpline::AsId()), FPCGDataTypeInfoPolyline::AsId(), true},
		{FPCGDataTypeIdentifier::Construct(FPCGDataTypeInfoParam::AsId(), FPCGDataTypeInfoPoint::AsId()), FPCGDataTypeIdentifier::Construct(FPCGDataTypeInfoParam::AsId(), FPCGDataTypeInfoSpatial::AsId()), true},
	};
	
	const FPCGDataTypeRegistry& Registry = FPCGModule::GetConstDataTypeRegistry();
	
	for (const auto& [LHS, RHS, ExpectedResult] : TestValues)
	{
		const bool bIsChildOf = LHS.IsChildOf(RHS);
		const FString TestWhat = FString::Printf(TEXT("%s is child of %s."), *LHS.ToString(), *RHS.ToString());
		
		if (ExpectedResult)
		{
			UTEST_TRUE(*TestWhat, bIsChildOf)
		}
		else
		{
			UTEST_FALSE(*TestWhat, bIsChildOf)
		}
	}
	
	return true;
}

#endif // WITH_EDITOR