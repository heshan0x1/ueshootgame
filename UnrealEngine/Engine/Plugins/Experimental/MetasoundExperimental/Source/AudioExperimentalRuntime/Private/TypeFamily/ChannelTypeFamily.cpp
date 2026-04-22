// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypeFamily/ChannelTypeFamily.h"

#include "Algo/Transform.h"
#include "CoreGlobals.h"
#include "ChannelAgnostic/ChannelAgnosticTranscoding.h"
#include "Containers/Map.h"
#include "DSP/SphericalHarmonicCalculator.h"

namespace Audio
{
	namespace ChannelTypeFamilyPrviate
	{
		static FString MakePrettyString(const TArray<FDiscreteChannelTypeFamily::FSpeaker>& InSpeakers)
		{
			return FString::JoinBy(InSpeakers,TEXT(", "), [](FDiscreteChannelTypeFamily::FSpeaker InSpeaker) -> FString
			{
				return FString::Printf(TEXT("[Name=%s,Az=%2.2f, El=%2.2f]"), LexToString(InSpeaker.Speaker), InSpeaker.AzimuthDegrees, InSpeaker.ElevationDegrees);
			});
		}
	}
	
	class FChannelRegistryImpl final : public IChannelTypeRegistry 
	{
		TMap<FName, const FChannelTypeFamily*> Types;
	public:
		UE_NONCOPYABLE(FChannelRegistryImpl);
		FChannelRegistryImpl() = default;
		virtual ~FChannelRegistryImpl() override = default;
	protected:
		virtual bool RegisterType(const FName& InUniqueName, TRetainedRef<FTypeFamily> InType) override
		{
			static const FName ChannelTypeName = TEXT("Cat");
			const FTypeFamily* Type = FindTypeInternal(ChannelTypeName);
			if (Type && !InType.Get().IsA(Type))
			{
				// Not a cat. (maybe a dog?)
				return false;	
			}
			
			// TODO: thread safety here.
			if (Types.Find(InUniqueName) != nullptr)
			{
				return false;
			}
			
			// Safe to cast it and add.
			const FChannelTypeFamily* ChannelType = static_cast<const FChannelTypeFamily*>(&InType.Get());
			
			// Add to master list of registered types.
			check(Types.Find(InUniqueName) == nullptr);
			Types.Add(InUniqueName,ChannelType);
	
			return true;
		}	
		virtual const FTypeFamily* FindTypeInternal(const FName InUniqueName) const override
		{
			if (const FChannelTypeFamily* const * Found = Types.Find(InUniqueName))
			{
				return *Found;
			}
			return nullptr;
		}

		virtual TArray<const FChannelTypeFamily*> GetAllChannelFormats() const override
		{
			TArray<const FChannelTypeFamily*> AllFormats;
			Types.GenerateValueArray(AllFormats);
			return AllFormats;
		};
	};

	int32 FAmbisonicsChannelTypeFamily::OrderToNumChannels(const int32 InOrder)
	{
		return FSphericalHarmonicCalculator::OrderToNumChannels(InOrder);
	}

	IChannelTypeRegistry& GetChannelRegistry()
	{
		static FChannelRegistryImpl Registry;
		return Registry;
	}

   FChannelTypeFamily::FChannelTypeFamily(
	   const FName& InUniqueName,
	   const FName& InFamilyTypeName,
	   const int32 InNumChannels,
	   FChannelTypeFamily* InParentType,
	   const FString& InFriendlyName,
	   const bool InbIsParentsDefault,
	   const bool InbIsAbstract)
	   : Super(InUniqueName, InParentType, InFriendlyName)
	   , bIsAbstract(InbIsAbstract)
	   , bIsParentsDefault(InbIsParentsDefault)
	   , NumChannelsPrivate(InNumChannels)
	   , FamilyType(InFamilyTypeName)
	{
		check(InNumChannels >= 0);
		check(!InUniqueName.IsNone());
		if (bIsParentsDefault)
		{
			check(InParentType);
			check(InParentType->DefaultChild == nullptr); 
			InParentType->DefaultChild = this;
		}
	}

   FChannelTypeFamily::FTranscoder FChannelTypeFamily::GetTranscoder(const FGetTranscoderParams& InParam) const
   {
		FTranscoderResolver Resolver(InParam);
		Accept(Resolver);
		return Resolver.MoveResult();
   }

   FDiscreteChannelTypeFamily::FDiscreteChannelTypeFamily(const FName& InUniqueName, FChannelTypeFamily* InParentType, const FString& InFriendlyName, const TArray<FSpeaker>& InOrder, const bool bIsParentsDefault, const bool bIsAbstract)
		: Super(InUniqueName, GetFamilyTypeName(), InOrder.Num(), InParentType, InFriendlyName, bIsParentsDefault, bIsAbstract)
		, Order(InOrder)
	{
		checkf(InParentType, TEXT("Type=%s, Has a Null Parent"), *GetName().ToString());
		auto GetParentNameSafe = [](FChannelTypeFamily* i) -> FString { return i ? i->GetName().ToString() : FString(); };
		UE_LOG(LogTemp, Display, TEXT("Unique=%s\tNumChannels=%d\tParent=%s\tFriendlyName=%s\tDefault=%s\tOrder=[%s]\tAbstract=%s"),
			*InUniqueName.ToString(),  InOrder.Num(), *GetParentNameSafe(InParentType), *InFriendlyName, ToCStr(LexToString(bIsParentsDefault)), *ChannelTypeFamilyPrviate::MakePrettyString(Order), ToCStr(LexToString(bIsAbstract)));
	}

	TOptional<FChannelTypeFamily::FChannelName> FDiscreteChannelTypeFamily::GetChannelName(const int32 InChannelIndex) const
	{
		check(InChannelIndex >= 0 && InChannelIndex < NumChannels());
		if (Order.IsValidIndex(InChannelIndex))
		{
			const ESpeakerShortNames Speaker = Order[InChannelIndex].Speaker;
			const FName SpeakerName = LexToString(Speaker);
			return
			{ // Optional.
					{ 
						.Name = SpeakerName,
						.FriendlyName = SpeakerName.ToString() 
					}
			};
		}
		return {};
	}

	void RegisterChannelLayouts(IChannelTypeRegistry& Registry)
	{
		/**
		 * Standard layouts.
		 * This should be defined in .ini ultimately, allowing new, custom formats to be added simply.
		 **/
		
		// Register root type.
		static FChannelTypeFamily BaseCat(TEXT("Cat"), TEXT("Cat"),  0, nullptr, TEXT("Base Cat"), false, true);
		Registry.RegisterType(BaseCat.GetName(), BaseCat);
		
#define REGISTER_CAT(NAME, PARENT_NAME, FRIENDLY_NAME, LAYOUT, DEFAULT,ABSTRACT)\
static FDiscreteChannelTypeFamily UE_JOIN(__Type,NAME)(UE_STRINGIZE(NAME), Registry.FindChannel(PARENT_NAME), FRIENDLY_NAME, LAYOUT, DEFAULT, ABSTRACT);\
Registry.RegisterType(TEXT(UE_STRINGIZE(NAME)),UE_JOIN(__Type,NAME))

		using enum ESpeakerShortNames;
		using FS = FDiscreteChannelTypeFamily::FSpeaker;

		// Top level abstraction. "Discrete".
		REGISTER_CAT(Discrete,			TEXT("Cat"),		TEXT("Discrete"),		{},										false,					true);		

		// Mono
		REGISTER_CAT(Mono, TEXT("Discrete"), TEXT("Mono"), {}, false, true);
		REGISTER_CAT(Mono1Dot0, TEXT("Mono"), TEXT("Mono (1.0)"),
		             (TArray<FS>
			             {
			             { FC, 0.f, 0.f }, // Az:   0°, El:  0°
			             }), true, false);

		REGISTER_CAT(Mono1Dot1, TEXT("Mono"), TEXT("Mono (1.1)"), (TArray<FS>{
			             { FC, 0.f, 0.f }, // Az:   0°, El:  0°
			             { LFE, 0.f, 0.f } // LFE (omni)
			             }), false, false);

// Stereo
REGISTER_CAT(Stereo, TEXT("Discrete"), TEXT("Stereo"), {}, false, true);
REGISTER_CAT(Stereo2Dot0, TEXT("Stereo"), TEXT("Stereo (2.0)"),
	(TArray<FS>{
		{ FL, -30.f,   0.f },  // Az: -30°, El:  0°
		{ FR,  30.f,   0.f }   // Az: +30°, El:  0°
		}), true, false);
		
REGISTER_CAT(Stereo2Dot1, TEXT("Stereo"), TEXT("Stereo (2.1)"),
	(TArray<FS>{
		{ FL, -30.f, 0.f },		// Az: -30°, El:  0°
		{ FR, 30.f, 0.f },		// Az: +30°, El:  0°
		{ LFE, 0.f, 0.f }		// LFE (omni)
	}),
	false, false);
REGISTER_CAT(Stereo3Dot0, TEXT("Stereo"), TEXT("Stereo (3.0)"),
	(TArray<FS>{
		{ FL, -30.f,   0.f },  // Az: -30°, El:  0°
		{ FC,   0.f,   0.f },  // Az:   0°, El:  0°
		{ FR,  30.f,   0.f }   // Az: +30°, El:  0°
	}),
	false, false);
REGISTER_CAT(Stereo3DOt1, TEXT("Stereo"), TEXT("Stereo (3.1)"),
	(TArray<FS>{
		{ FL, -30.f,   0.f },  // Az: -30°, El:  0°
		{ FC,   0.f,   0.f },  // Az:   0°, El:  0°
		{ FR,  30.f,   0.f },  // Az: +30°, El:  0°
		{ LFE,  0.f,   0.f }   // LFE (omni)
	}),
	false, false);

// Quad
REGISTER_CAT(Quad, TEXT("Discrete"), TEXT("Quad"), {}, false, true);
REGISTER_CAT(Quad4Dot0Back, TEXT("Quad"), TEXT("Quad Back Speakers (4.0)"),
	(TArray<FS>{
		{ FL, -30.f,   0.f },  // Front Left
		{ FR,  30.f,   0.f },  // Front Right
		{ BL, -150.f,  0.f },  // Rear  Left
		{ BR, 150.f,   0.f }   // Rear  Right
		}),
	true, false);
REGISTER_CAT(Quad4Dot0Side, TEXT("Quad"), TEXT("Quad Side Speakers (4.0)"),
	(TArray<FS>{
		{ FL, -30.f,   0.f },  // Front Left
		{ FR,  30.f,   0.f },  // Front Right
		{ SL, -90.f,   0.f },  // Side  Left
		{ SR,  90.f,   0.f }   // Side  Right
		}),
		false, false);
REGISTER_CAT(Quad4Dot1, TEXT("Quad"), TEXT("Quad Back Centre LFE (4.1)"),
	(TArray<FS>{
		{ FL, -30.f,   0.f },  // Front Left
		{ FR,  30.f,   0.f },  // Front Right
		{ BL, -150.f,  0.f },  // Rear  Left
		{ BR, 150.f,   0.f },  // Rear  Right
		{ LFE, 0.f,    0.f }   // LFE (omni)
		}),
	false, false);

// Surround
REGISTER_CAT(Surround, TEXT("Discrete"), TEXT("Surround"), {}, false, true);
REGISTER_CAT(Surround5, TEXT("Surround"), TEXT("Surround (5.X)"), {}, true, true);
REGISTER_CAT(Surround5Dot0, TEXT("Surround5"), TEXT("Surround (5.0)"),
    (TArray<FS>{
        { FL,  -30,  0 },
        { FR,   30,  0 },
        { SL,  -90,  0 },
        { SR,   90,  0 },
        { FC,    0,  0 },
        { BL, -135,  0 },
        { BR,  135,  0 },
    }),
    false, false);

REGISTER_CAT(Surround5_1, TEXT("Surround5"), TEXT("Surround (5.1)"),
    (TArray<FS>{
        { FL,  -30,  0 },
        { FR,   30,  0 },
        { SL,  -90,  0 },
        { SR,   90,  0 },
        { FC,    0,  0 },
        { BL, -135,  0 },
        { BR,  135,  0 },
        { LFE,   0,  0 },
    }),
    true, false);

REGISTER_CAT(Surround7, TEXT("Surround"), TEXT("Surround (7.X)"),
    {}, false, true);

REGISTER_CAT(Surround7Dot0, TEXT("Surround7"), TEXT("Surround (7.0)"),
    (TArray<FS>{
        { FL,  -30,  0 },
        { FR,   30,  0 },
        { SL,  -90,  0 },
        { SR,   90,  0 },
        { FC,    0,  0 },
        { BL, -135,  0 },
        { BR,  135,  0 },
    }),
    false, false);

REGISTER_CAT(Surround7Dot1, TEXT("Surround7"), TEXT("Surround (7.1)"),
    (TArray<FS>{
        { FL,  -30,  0 },
        { FR,   30,  0 },
        { SL,  -90,  0 },
        { SR,   90,  0 },
        { FC,    0,  0 },
        { LFE,   0,  0 },
        { BL, -135,  0 },
        { BR,  135,  0 },
    }),
    true, false);

// Atmos beds & heights
REGISTER_CAT(Atmos, TEXT("Surround7"), TEXT("Dolby Atmos Bed"),
    {}, false, true);

REGISTER_CAT(Atmos7Dot0Dot2, TEXT("Atmos"), TEXT("Dolby Atmos (7.0.2)"),
    (TArray<FS>{
        { FL,  -30.f,   0.f },
        { FR,   30.f,   0.f },
        { SL,  -90.f,   0.f },
        { SR,   90.f,   0.f },
        { FC,    0.f,   0.f },
        { BL, -135.f,   0.f },
        { BR,  135.f,   0.f },
        // heights
        { TFL, -35.3f,  30.0f },
        { TFR,  35.3f,  30.0f },
    }),
    false, false);

REGISTER_CAT(Atmos7Dot0Dot4, TEXT("Atmos"), TEXT("Dolby Atmos (7.0.4)"),
    (TArray<FS>{
        { FL,  -30.f,   0.f },
        { FR,   30.f,   0.f },
        { SL,  -90.f,   0.f },
        { SR,   90.f,   0.f },
        { FC,    0.f,   0.f },
        { BL, -135.f,   0.f },
        { BR,  135.f,   0.f },
        // heights
        { TFL, -35.3f,  30.0f },
        { TFR,  35.3f,  30.0f },
        { TBL, -135.0f, 45.0f },
        { TBR,  135.0f, 45.0f },
    }),
    false, false);

REGISTER_CAT(Atmos7Dot1Dot2, TEXT("Atmos"), TEXT("Dolby Atmos (7.1.2)"),
    (TArray<FS>{
        { FL,  -30.f,   0.f },
        { FR,   30.f,   0.f },
        { SL,  -90.f,   0.f },
        { SR,   90.f,   0.f },
        { FC,    0.f,   0.f },
        { LFE,   0.f,   0.f },
        { BL, -135.f,   0.f },
        { BR,  135.f,   0.f },
        // heights
        { TFL, -35.3f,  30.0f },
        { TFR,  35.3f,  30.0f },
    }),
    false, false);

REGISTER_CAT(Atmos7Dot1Dot4, TEXT("Atmos"), TEXT("Dolby Atmos (7.1.4)"),
    (TArray<FS>{
        { FL,  -30.f,    0.f },
        { FR,   30.f,    0.f },
        { SL,  -90.f,    0.f },
        { SR,   90.f,    0.f },
        { FC,    0.f,    0.f },
        { LFE,   0.f,    0.f },
        { BL, -135.f,    0.f },
        { BR,  135.f,    0.f },
        // heights
        { TFL, -35.3f,   30.0f },
        { TFR,  35.3f,   30.0f },
        { TBL, -135.0f,  45.0f },
        { TBR,  135.0f,  45.0f },
    }),
    true, false);

					
#define REGISTER_AMBISONICS(NAME, PARENT_NAME, FRIENDLY_NAME, ORDER, DEFAULT,ABSTRACT)\
static FAmbisonicsChannelTypeFamily UE_JOIN(__Type,NAME)(UE_STRINGIZE(NAME), ORDER, Registry.FindChannel(PARENT_NAME), FRIENDLY_NAME, DEFAULT, ABSTRACT);\
ensure(Registry.RegisterType(UE_STRINGIZE(NAME),UE_JOIN(__Type,NAME)))

		// Ambisonics 
		REGISTER_AMBISONICS(Ambisonics,					TEXT("Cat"), 	   TEXT("Ambisonics"), 				0, false, true);
		REGISTER_AMBISONICS(Ambisonics1stOrder,			TEXT("Ambisonics"), TEXT("1st Order Ambisonics"), 		1, false, false); 
		REGISTER_AMBISONICS(Ambisonics2ndOrder,			TEXT("Ambisonics"), TEXT("2nd Order Ambisonics"), 		2, false, false); 
		REGISTER_AMBISONICS(Ambisonics3rdOrder,			TEXT("Ambisonics"), TEXT("3rd Order Ambisonics"), 		3, false, false);
		REGISTER_AMBISONICS(Ambisonics4thOrder,			TEXT("Ambisonics"), TEXT("4th Order Ambisonics"),		4, false, false); 
		REGISTER_AMBISONICS(Ambisonics5thOrder,			TEXT("Ambisonics"), TEXT("5th Order Ambisonics"),		5, false, false);  
		REGISTER_AMBISONICS(Ambisonics6thOrder,			TEXT("Ambisonics"), TEXT("6th Order Ambisonics"),		6, false, false);  
		REGISTER_AMBISONICS(Ambisonics7thOrder,			TEXT("Ambisonics"), TEXT("7th Order Ambisonics"),		7, false, false);  

		// Channel packs.
		// FPackedChannelTypeFamily MonoPack( TEXT("PackedChannel")
		
#undef REGISTER_CAT
#undef REGISTER_AMBISONICS
	}
}


const TCHAR* LexToString(const ESpeakerShortNames InSpeaker)
{
	// No code gen support here, so role this manually.
#define CASE_AND_STRING(X) case ESpeakerShortNames::X: return TEXT(#X)

	using enum ESpeakerShortNames;
	switch (InSpeaker)
	{
	CASE_AND_STRING(FL);   // Front Left
	CASE_AND_STRING(FR);   // Front Right
	CASE_AND_STRING(FC);   // Front Center
	CASE_AND_STRING(LFE);  // Low-Frequency Effects (Subwoofer)
	CASE_AND_STRING(FLC);  // Front Left Center
	CASE_AND_STRING(FRC);  // Front Right Center
	CASE_AND_STRING(SL);   // Side Left
	CASE_AND_STRING(SR);   // Side Right
	CASE_AND_STRING(BL);   // Back Left
	CASE_AND_STRING(BR);   // Back Right
	CASE_AND_STRING(BC);   // Back Center
	CASE_AND_STRING(TFL);  // Top Front Left
	CASE_AND_STRING(TFR);  // Top Front Right
	CASE_AND_STRING(TBL);  // Top Back Left
	CASE_AND_STRING(TBR);  // Top Back Right
	default:
		break;
	}
	return nullptr;
}
