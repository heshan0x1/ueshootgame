// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityElementTypes.h"
#include "MassExternalSubsystemTraits.h"
#include "Subsystems/Subsystem.h"


namespace UE::Mass
{
	template<typename T>
	using Clean = typename TRemoveReference<T>::Type;
	
	template<typename T>
	concept CFragment = TIsDerivedFrom<Clean<T>, FMassFragment>::Value &&
		(
			std::is_trivially_copyable_v<Clean<T>> ||
			static_cast<bool>(TMassFragmentTraits<Clean<T>>::AuthorAcceptsItsNotTriviallyCopyable)
		);
	
	template<typename T>
	concept CTag = TIsDerivedFrom<Clean<T>, FMassTag>::Value;

	template<typename T>
	concept CChunkFragment = TIsDerivedFrom<Clean<T>, FMassChunkFragment>::Value;

	template<typename T>
	concept CSharedFragment = TIsDerivedFrom<Clean<T>, FMassSharedFragment>::Value;

	template<typename T>
	concept CConstSharedFragment = TIsDerivedFrom<Clean<T>, FMassConstSharedFragment>::Value;

	template<typename T>
	concept CNonTag = CFragment<T> || CChunkFragment<T> || CSharedFragment<T> || CConstSharedFragment<T>;

	template<typename T>
	concept CElement = CNonTag<T> || CTag<T>;

	template<typename T>
	concept CSubsystem = TIsDerivedFrom<Clean<T>, USubsystem>::Value;

	namespace Private
	{
		template<CElement T>
		struct TElementTypeHelper
		{
			using Type = std::conditional_t<CFragment<T>, FMassFragment
				, std::conditional_t<CTag<T>, FMassTag
				, std::conditional_t<CChunkFragment<T>, FMassChunkFragment
				, std::conditional_t<CSharedFragment<T>, FMassSharedFragment
				, std::conditional_t<CConstSharedFragment<T>, FMassConstSharedFragment
				, void>>>>>;
		};
	}

	template<typename T>
	using TElementType = typename Private::TElementTypeHelper<T>::Type;
}
