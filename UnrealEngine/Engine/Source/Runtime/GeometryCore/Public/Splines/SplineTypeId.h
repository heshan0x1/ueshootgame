// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE
{
namespace Geometry
{
namespace Spline
{
    
DEFINE_LOG_CATEGORY_STATIC(LogSpline, Log, All);
/** Helper for static_assert that depends on template parameter */
template<typename T>
struct TDependentFalse : std::false_type {};
    
template<typename T>
struct TSplineValueTypeTraits
{
    // Static assert ensures custom types define their name
    static_assert(TDependentFalse<T>::value, 
        "Splines must specialize TSplineValueTypeTraits<T> with a static 'Name' member.");
};
/**
 * Type identification system for splines with compile-time IDs
 */
class FSplineTypeId
{
public:
    // Type ID type (32-bit for all types)
    using IdType = uint32;
    
    /**
     * Generates a type ID for a specific implementation/value type pair
     * Uses compile-time hash of names to create deterministic IDs
     * 
     * @param ImplName Implementation name (e.g., "BSpline3", "PolyBezier")
     * @param ValueTypeName Value type name (e.g., "Vector", "Float")
     * @return A compile-time generated type ID
     */
    static constexpr IdType GenerateTypeId(
        const TCHAR* ImplName, 
        const TCHAR* ValueTypeName)
    {
        // Compute hash values from the strings (16 bits each)
        uint16 ImplHash = CompileTimeHash(ImplName) & 0xFFFF; 
        uint16 ValueHash = CompileTimeHash(ValueTypeName) & 0xFFFF;
        
        // Combine: [implHash(16) | valueHash(16)]
        return ((IdType)ImplHash << 16) | ValueHash;
    }
    
    /**
     * Compute a compile-time hash from a string
     * Simple implementation that's sufficient for our needs
     */
    static constexpr uint32 CompileTimeHash(const TCHAR* Str)
    {
        uint32 Result = 2166136261u; // FNV offset basis
        
        while (*Str)
        {
            // Process one TCHAR at a time
            TCHAR C = *Str++;
            
            // FNV-1a hash
            Result ^= (uint32)C;
            Result *= 16777619u; // FNV prime
        }
        
        return Result;
    }
    
    /**
     * Extracts the implementation hash portion from a type ID
     */
    static constexpr uint16 GetImplHash(IdType TypeId)
    {
        return (TypeId >> 16) & 0xFFFF; // Upper 16 bits
    }
    
    /**
     * Extracts the value type hash portion from a type ID
     */
    static constexpr uint16 GetValueHash(IdType TypeId)
    {
        return TypeId & 0xFFFF; // Lower 16 bits
    }
    
    /**
     * Checks if two type IDs might be in conflict
     * Used during registration to detect potential hash collisions
     */
    static bool CheckTypeIdConflict(IdType TypeId, const FString& ImplName, const FString& ValueTypeName, 
                                  const FString& ExistingImplName, const FString& ExistingValueTypeName)
    {
        // If the names don't match but the TypeId does, we have a hash collision
        bool bImplNameMismatch = (ImplName != ExistingImplName);
        bool bValueNameMismatch = (ValueTypeName != ExistingValueTypeName);
        
        if (bImplNameMismatch || bValueNameMismatch)
        {
            UE_LOG(LogSpline, Error, TEXT(" HASH COLLISION in SplineTypeId!! ️"));
            UE_LOG(LogSpline, Error, TEXT("  TypeId: 0x%08X"), TypeId);
            UE_LOG(LogSpline, Error, TEXT("  Attempted registration: Impl=%s, Value=%s"), 
                   *ImplName, *ValueTypeName);
            UE_LOG(LogSpline, Error, TEXT("  Existing registration: Impl=%s, Value=%s"),
                   *ExistingImplName, *ExistingValueTypeName);
            return true;
        }
        
        return false;
    }
};

// Simplified macro to declare a static type ID in a spline class
#define DECLARE_SPLINE_TYPE_ID(ImplName, ValueTypeName) \
    static const FSplineTypeId::IdType& GetStaticTypeId() \
    { \
        static const FSplineTypeId::IdType CachedTypeId = \
        FSplineTypeId::GenerateTypeId(ImplName, ValueTypeName); \
        return CachedTypeId; \
    } \
    virtual FSplineTypeId::IdType GetTypeId() const override \
    { \
        return GetStaticTypeId(); \
    } \
    static FString GetSplineTypeName() \
    { \
        return FString(ImplName); \
    } \
    virtual FString GetImplementationName() const override \
    { \
        return FString(ImplName); \
    }

} // namespace Spline
} // namespace Geometry
} // namespace UE