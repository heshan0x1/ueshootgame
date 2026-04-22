// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "TypeFamily.h"
#include "DSP/ChannelMap.h"
#include "Templates/Function.h"
#include "Misc/Optional.h"


/**
 * A Comprehensive Short form Speaker enumeration
 */
enum class ESpeakerShortNames : uint32
{
	FL = EAudioMixerChannel::FrontLeft,				// Front Left
	FR = EAudioMixerChannel::FrontRight,			// Front Right
	FC = EAudioMixerChannel::FrontCenter,			// Front Center
	LFE = EAudioMixerChannel::LowFrequency,			// Low-Frequency Effects (Subwoofer)
	FLC = EAudioMixerChannel::FrontLeftOfCenter,	// Front Left Center
	FRC = EAudioMixerChannel::FrontRightOfCenter,	// Front Right Center
	SL = EAudioMixerChannel::SideLeft,				// Side Left
	SR = EAudioMixerChannel::SideRight,				// Side Right
	BL = EAudioMixerChannel::BackLeft,				// Back Left
	BR = EAudioMixerChannel::BackRight,				// Back Right
	BC = EAudioMixerChannel::BackCenter,			// Back Center
	TFL = EAudioMixerChannel::TopFrontLeft,			// Top Front Left
	TFR = EAudioMixerChannel::TopFrontRight,		// Top Front Right
	TBL = EAudioMixerChannel::TopBackLeft,			// Top Back Left
	TBR = EAudioMixerChannel::TopBackRight,			// Top Back Right

	NumChannels = EAudioMixerChannel::ChannelTypeCount,
};

// Pasted from UHT generated.h, but we can't use because of AudioUnitTests.
#define FOREACH_ENUM_ESPEAKERSHORTNAMES(op) \
	op(ESpeakerShortNames::FL) \
	op(ESpeakerShortNames::FR) \
	op(ESpeakerShortNames::FC) \
	op(ESpeakerShortNames::LFE) \
	op(ESpeakerShortNames::FLC) \
	op(ESpeakerShortNames::FRC) \
	op(ESpeakerShortNames::SL) \
	op(ESpeakerShortNames::SR) \
	op(ESpeakerShortNames::BL) \
	op(ESpeakerShortNames::BR) \
	op(ESpeakerShortNames::BC) \
	op(ESpeakerShortNames::TFL) \
	op(ESpeakerShortNames::TFR) \
	op(ESpeakerShortNames::TBL) \
	op(ESpeakerShortNames::TBR)

const TCHAR* LexToString(const ESpeakerShortNames InSpeaker);

namespace Audio
{
	enum class EChannelTranscodeMethod : uint8
	{
		ChannelDrop,
		MixUpOrDown,
	};

	// Forward declare these for visitor.
	class FDiscreteChannelTypeFamily;
	class FAmbisonicsChannelTypeFamily;
	class FPackedChannelTypeFamily;
	
	class IChannelTypeVisitor
	{
	public:
		virtual ~IChannelTypeVisitor() = default;
		virtual void Visit(const FDiscreteChannelTypeFamily&) = 0;
		virtual void Visit(const FAmbisonicsChannelTypeFamily&) = 0;
	};
	
	// Base type for all "channel" based types.
	class FChannelTypeFamily : public FTypeFamily
	{
	public:
		UE_NONCOPYABLE(FChannelTypeFamily);
		using Super = FTypeFamily;

		/**
		 * Constructor. 
		 * @param InUniqueName The unique name that will be used for look up in registry.
		 * @param InFamilyTypeName The name of the concrete type (c++) that's defining this. (for safe casting). 
		 * @param InNumChannels Num of channels in this type. (pure categorical, organisational entries will be 0).
		 * @param InParentType Pointer to this parents type in the tree. Can be null.
		 * @param InFriendlyName Friendly name to display to the user e.g. "Dolby Stereo (2.0)"
		 * @param InbIsParentsDefault Marks if this entry is the default child of its parent. 
		 */
		explicit FChannelTypeFamily(
			const FName& InUniqueName,
			const FName& InFamilyTypeName,
			const int32 InNumChannels,
			FChannelTypeFamily* InParentType,
			const FString& InFriendlyName,
			const bool InbIsParentsDefault,
			const bool InbIsAbstract);

		virtual ~FChannelTypeFamily() = default;

		/**
		 * If this type can be instantiated or is just organisational.
		 * @return true if abstract, false otherwise
		 */
		bool IsAbstract() const
		{
			return bIsAbstract;
		}

		/**
		 * If this is marked as being the default on its parent
		 * @return true if true, false otherwise.
		 */
		bool IsParentsDefault() const
		{
			return bIsParentsDefault;
		}

		/**
		 * Returns the default child if one exists. Example. "Stereo" would return "Stereo_2_0"
		 * @return Pointer to type, null otherwise.
		 */
		const FChannelTypeFamily* GetDefaultChild() const { return DefaultChild; }

		/**
		 * Num of channels  
		 * @return >= 0
		 */
		int32 NumChannels() const { return NumChannelsPrivate; }

		/**
		 * Transcoder function object.
		 * Arrays of pointers necessary to make more flexible than just multi-mono and compat with our exisiting libs.
		 */
		using FTranscoder = TFunction<void(TArrayView<const float*>, TArrayView<float*>, const int32 NumFrames)>;
		
		/**
		* Parameters to pass to GetTranscoder 
		* To be expanded.
		*/
		struct FGetTranscoderParams
		{
			const FChannelTypeFamily& ToType;
			EChannelTranscodeMethod TranscodeMethod = EChannelTranscodeMethod::ChannelDrop;
			EChannelMapMonoUpmixMethod MixMethod = EChannelMapMonoUpmixMethod::EqualPower;
		};
		/**
		 * Get the function used for translating/transcoding between this and the "To" type.
		 * @return The Function Object. (this will be not be set if a function wasn't returned)
		 */
		AUDIOEXPERIMENTALRUNTIME_API FTranscoder GetTranscoder(const FGetTranscoderParams& InParam) const;

		/**
		 * Returns the family name (C++ type) of this ChannelType. This allows us to safely downcast (if necessary).
		 * @return The name of this type Family. (i.e. Discrete/Ambisoncs). 
		 */
		FName GetFamilyName() const { return FamilyType; }

		/**
		 * Returns the name of the channel. For discrete this would be what speaker its mapped to etc.
		 * @param InChannelIndex Index of channel >= 0 and < NumChannels.
		 * @return Optional containing the Channel name and friendly name.
		 */
		struct FChannelName
		{
			FName Name;
			FString FriendlyName;
		};
		virtual TOptional<FChannelName> GetChannelName(const int32 InChannelIndex) const { return {}; }

		template<typename T>
		T* Cast()
		{
			if (T::GetFamilyTypeName() == GetFamilyName())
			{
				return static_cast<T*>(this);
			}
			return nullptr;
		}

		template<typename T>
		const T* Cast() const { return const_cast<FChannelTypeFamily*>(this)->Cast<T>(); }

		virtual void Accept(IChannelTypeVisitor& InVisitor) const
		{
			checkNoEntry();
		};

	protected:
		// Both are friends so we can hide the api.
		friend class FDiscreteChannelTypeFamily;
		friend class FAmbisonicsChannelTypeFamily;
		
	private:
		bool bIsAbstract = false;
		bool bIsParentsDefault = false;
		const FChannelTypeFamily* DefaultChild = nullptr;
		int32 NumChannelsPrivate = 0;
		FName FamilyType; 
	};
	
	class FDiscreteChannelTypeFamily final: public FChannelTypeFamily 
	{
	public:
		UE_NONCOPYABLE(FDiscreteChannelTypeFamily);
		using Super = FChannelTypeFamily;

		struct FSpeaker
		{
			ESpeakerShortNames Speaker;		// Enum of speaker types.
			float AzimuthDegrees = 0.f;
			float ElevationDegrees = 0.f;
		};
		
		static FName GetFamilyTypeName()
		{
			static const FName Name = TEXT("Discrete");
			return Name;
		}
			
		FDiscreteChannelTypeFamily(const FName& InUniqueName, FChannelTypeFamily* InParentType, const FString& InFriendlyName,
			const TArray<FSpeaker>& InOrder, const bool bIsParentsDefault, const bool bIsAbstract);

		/**
		 * Find the index in the list of Channels for speaker.
		 * @param InSpeaker 
		 * @return >= 0 (index) INDEX_NONE (failed to find).
		 */
		int32 FindSpeakerIndex(const ESpeakerShortNames InSpeaker) const
		{
			return Order.IndexOfByPredicate([InSpeaker](const FSpeaker& i) { return InSpeaker == i.Speaker; });
		}

		/**
		 * Checks if a paticular speaker is present in the Channels for this Format.
		 * @param InSpeaker 
		 * @return true (exists), false otherwise.
		 */
		bool HasSpeaker(const ESpeakerShortNames InSpeaker) const
		{
			return FindSpeakerIndex(InSpeaker) != INDEX_NONE;
		}

		/**
		 * Get the function used for translating/transcoding between this and the "To" type.
		 * @param InToType The Type we are transcoding into.
		 * @return The Function Object. (this will be not be set if a function wasn't returned)
		 */	

		virtual TOptional<FChannelName> GetChannelName(const int32 InChannelIndex) const override;

		/**
		 * Get the order of the speakers defined on this discrete format.
		 * @return Array of speakers in order.
		 */
		//const TArray<ESpeakerShortNames>& GetSpeakerOrder() const { return Order; }

		const TArray<FSpeaker>& GetSpeakerOrder() const { return Order; }

		virtual void Accept(IChannelTypeVisitor& InVisitor) const override
		{
			InVisitor.Visit(*this);
		}

	protected:
		TArray<FSpeaker> Order;
	};

	// Ambisonics.
	class FAmbisonicsChannelTypeFamily final : public FChannelTypeFamily 
	{
	public:
		UE_NONCOPYABLE(FAmbisonicsChannelTypeFamily);
		using Super = FChannelTypeFamily;
		
		static FName GetFamilyTypeName()
		{
			static const FName Name = TEXT("Ambisonics");
			return Name;
		}

		static int32 OrderToNumChannels(const int32 InOrder);

		explicit FAmbisonicsChannelTypeFamily(const FName& InUniqueName, const int32 InOrder, FChannelTypeFamily* InParentType, const FString& InFriendlyName, const bool bIsParentsDefault, const bool InbIsAbstract)
			: Super(InUniqueName, GetFamilyTypeName(), OrderToNumChannels(InOrder), InParentType, InFriendlyName, bIsParentsDefault, InbIsAbstract)
			, Order(InOrder)
		{}

		int32 GetAmbisonicsOrder() const { return Order; }

		virtual void Accept(IChannelTypeVisitor& InVisitor) const override
		{
			InVisitor.Visit(*this);
		}
	private:
		int32 Order = 0;
	};

	/*
	class FPackedChannelTypeFamily final : public FChannelTypeFamily
	{
	public:
		UE_NONCOPYABLE(FPackedChannelTypeFamily);

		using Super = FChannelTypeFamily;

		static FName GetFamilyTypeName()
		{
			static const FName Name = TEXT("Packed");
			return Name;
		}
		explicit FPackedChannelTypeFamily(const FName& InUniqueName, FChannelTypeFamily* InParentType, const FString& InFriendlyName, const bool bIsParentsDefault)
			: Super(InUniqueName, GetFamilyTypeName(), 0, InParentType, InFriendlyName, bIsParentsDefault, false)
		{}
	};
	*/
	
	// Channel type registry.
	class IChannelTypeRegistry : public ITypeFamilyRegistry
	{
	public:
		const FChannelTypeFamily* FindChannel(const FName InName) const
		{
			return Find<FChannelTypeFamily>(InName);
		}
		FChannelTypeFamily* FindChannel(const FName InName)
		{
			return const_cast<FChannelTypeFamily*>(const_cast<const IChannelTypeRegistry*>(this)->FindChannel(InName));
		}

		/**
		 * Find the first concrete (non-abstract, > 0 channels) channel. (e.g. Stereo -> 'Stereo_2_0')
		 * Each Abstract type should have a designated default child.
		 *
		 * @param InName Name of format.
		 * @return Pointer to type (on success), nullptr on failure
		 */
		const FChannelTypeFamily* FindConcreteChannel(const FName InName) const
		{
			// Walk looking for non-abstract default child. (e.g. Surround->Surround5X->Surround 5.1)
			const FChannelTypeFamily* Channel = FindChannel(InName);
			while (Channel && Channel->IsAbstract())
			{
				Channel = Channel->GetDefaultChild();
			}
			return Channel;
		}

		/**
		 * Returns every registered format as an array.
		 * @return Array of format types.
		 */
		virtual TArray<const FChannelTypeFamily*> GetAllChannelFormats() const = 0;
	};

	// Loose functions for now.
	AUDIOEXPERIMENTALRUNTIME_API IChannelTypeRegistry& GetChannelRegistry();
	void RegisterChannelLayouts(IChannelTypeRegistry& Registry = GetChannelRegistry());

}//namespace Audio
