// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataTypeGetTraits.h"

namespace Metasound
{
	template<typename T>
	struct TPolymorphicTraits
	{
		static constexpr bool bIsPolymorphic = false;
		static constexpr bool bIsAbstract = false;
		using BaseType = void;
	};

	struct FPolyTypeInfo
	{
		FName TypeName = NAME_None;
		FName BaseTypeName = NAME_None;
		bool bIsPolymorphic = false;
	};

	class FPolyRegistry
	{
	public:
		UE_EXPERIMENTAL(5.7, "Metasound Polymorphism is still under development")
		METASOUNDGRAPHCORE_API static FPolyRegistry& Get();

		UE_EXPERIMENTAL(5.7, "Metasound Polymorphism is still under development")
		METASOUNDGRAPHCORE_API void Register(const FPolyTypeInfo& Info);

		UE_EXPERIMENTAL(5.7, "Metasound Polymorphism is still under development")
		METASOUNDGRAPHCORE_API const FPolyTypeInfo* Find(const FName Type) const;

		UE_EXPERIMENTAL(5.7, "Metasound Polymorphism is still under development")
		void Unregister(const FPolyTypeInfo& Info);

		METASOUNDGRAPHCORE_API void GetAllRegisteredTypes(TArray<FPolyTypeInfo>& Out) const;

	private:
		TMap<FName, FPolyTypeInfo> Map;
	};

	template <typename T>
	struct TPolyRegistrar
	{
		TPolyRegistrar()
		{
			static const FName ThisName = GetMetasoundDataTypeName<T>();
			FPolyTypeInfo Info;
			Info.TypeName = ThisName;
			Info.bIsPolymorphic = TPolymorphicTraits<T>::bIsPolymorphic;

			if constexpr (TPolymorphicTraits<T>::bIsPolymorphic)
			{
				using Base = TPolymorphicTraits<T>::BaseType;
				static const FName BaseName = GetMetasoundDataTypeName<Base>();
				static_assert(!std::is_same_v<Base, T>, "Polymorphic BaseType must not equal T");
				Info.BaseTypeName = BaseName;
			}
			else
			{
				Info.BaseTypeName = NAME_None;
			}
			PRAGMA_DISABLE_INTERNAL_WARNINGS			
			FPolyRegistry::Get().Register(Info);
			PRAGMA_ENABLE_INTERNAL_WARNINGS			
		}
	};

	UE_EXPERIMENTAL(5.7, "Metasound Polymorphism is still under development")
	METASOUNDGRAPHCORE_API bool IsChildOfByName(const FName InDataType, const FName InPotentialBase);

	UE_EXPERIMENTAL(5.7, "Metasound Polymorphism is still under development")
	METASOUNDGRAPHCORE_API bool IsCastable(const FName InType, const FName InPotentialBase);
	
	
}

#define DECLARE_METASOUND_POLY_TYPE(TYPE, BASE, IS_ABSTRACT)\
	template<>\
	struct Metasound::TPolymorphicTraits<TYPE>\
	{\
		static constexpr bool bIsPolymorphic = true;\
		static constexpr bool bIsAbstract = IS_ABSTRACT;\
		using BaseType = BASE;\
	};

#define REGISTER_METASOUND_POLY_TYPE(T) static Metasound::TPolyRegistrar<T> UE_JOIN(GPolyReg,__COUNTER__);