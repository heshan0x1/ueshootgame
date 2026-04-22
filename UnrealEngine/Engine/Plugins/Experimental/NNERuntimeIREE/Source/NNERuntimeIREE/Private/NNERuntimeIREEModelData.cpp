// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREEModelData.h"

#include "Containers/Array.h"
#include "NNE.h"
#include "NNERuntimeIREELog.h"
#include "Serialization/CustomVersion.h"

namespace UE::NNERuntimeIREE::CPU::ModelData::Private
{

enum Version : uint32
{
	V0 = 0, // Initial
	// New versions can be added above this line
	VersionPlusOne,
	Latest = VersionPlusOne - 1
};
const FGuid GUID(0x6dcb835d, 0x9ac64a1d, 0x8165d871, 0x6122dab7);
FCustomVersionRegistration Version(GUID, Version::Latest, TEXT("NNERuntimeIREEModelDataVersionCPU"));// Always save with the latest version

} // UE::NNERuntimeIREE::CPU::ModelData::Private

void UNNERuntimeIREEModelDataCPU::Serialize(FArchive& Ar)
{
	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UE::NNERuntimeIREE::CPU::ModelData::Private::GUID);

	if (Ar.IsSaving() || Ar.IsCountingMemory()) 
	{
		Ar << GUID;
		Ar << Version;
		Ar << FileId;
		Ar << ModuleMetaData;
		Ar << CompilerResult;
	}
	else
	{
		int32 NumArchitectures = 0;

		switch (Ar.CustomVer(UE::NNERuntimeIREE::CPU::ModelData::Private::GUID))
		{
		case UE::NNERuntimeIREE::CPU::ModelData::Private::Version::V0:
			Ar << GUID;
			Ar << Version;
			Ar << FileId;
			Ar << ModuleMetaData;
			Ar << CompilerResult;
			break;
		default:
			UE_LOG(LogNNERuntimeIREE, Error, TEXT("UNNERuntimeIREEModelDataCPU: Unknown asset version %d: Deserialisation failed, please reimport the original model."), Ar.CustomVer(UE::NNERuntimeIREE::CPU::ModelData::Private::GUID));
			break;
		}
	}
}

bool UNNERuntimeIREEModelDataCPU::IsSameGuidAndVersion(TConstArrayView64<uint8> Data, FGuid Guid, int32 Version)
{
	int32 GuidSize = sizeof(UNNERuntimeIREEModelDataCPU::GUID);
	int32 VersionSize = sizeof(UNNERuntimeIREEModelDataCPU::Version);
	if (Data.Num() <= GuidSize + VersionSize)
	{
		return false;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &Guid, GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &Version, VersionSize) == 0;

	return bResult;
}

namespace UE::NNERuntimeIREE::RDG::ModelData::Private
{

enum Version : uint32
{
	V0 = 0, // Initial
	// New versions can be added above this line
	VersionPlusOne,
	Latest = VersionPlusOne - 1
};
const FGuid GUID(0x29663d20, 0x1dbd43c5, 0xa1ecc03b, 0x6005c845);
FCustomVersionRegistration Version(GUID, Version::Latest, TEXT("NNERuntimeIREEModelDataVersionRDG"));// Always save with the latest version

} // UE::NNERuntimeIREE::RDG::ModelData::Private

void UNNERuntimeIREEModelDataRDG::Serialize(FArchive& Ar)
{
	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UE::NNERuntimeIREE::RDG::ModelData::Private::GUID);

	if (Ar.IsSaving() || Ar.IsCountingMemory()) 
	{
		FNNERuntimeIREEModelDataHeaderRDG::StaticStruct()->SerializeBin(Ar, &Header);
		Ar << ModuleMetaData;
		Ar << CompilerResult;
	}
	else
	{
		switch (Ar.CustomVer(UE::NNERuntimeIREE::RDG::ModelData::Private::GUID))
		{
		case UE::NNERuntimeIREE::RDG::ModelData::Private::Version::V0:
		FNNERuntimeIREEModelDataHeaderRDG::StaticStruct()->SerializeBin(Ar, &Header);
			Ar << ModuleMetaData;
			Ar << CompilerResult;
			break;
		default:
			UE_LOG(LogNNERuntimeIREE, Error, TEXT("UNNERuntimeIREEModelDataRDG: Unknown asset version %d: Deserialisation failed, please reimport the original model."), Ar.CustomVer(UE::NNERuntimeIREE::RDG::ModelData::Private::GUID));
			break;
		}
	}
}

FNNERuntimeIREEModelDataHeaderRDG UNNERuntimeIREEModelDataRDG::ReadHeader(FArchive& Ar)
{
	FNNERuntimeIREEModelDataHeaderRDG Header{};
	switch (Ar.CustomVer(UE::NNERuntimeIREE::RDG::ModelData::Private::GUID))
	{
	case UE::NNERuntimeIREE::RDG::ModelData::Private::Version::V0:
	FNNERuntimeIREEModelDataHeaderRDG::StaticStruct()->SerializeBin(Ar, &Header);
		break;
	default:
		UE_LOG(LogNNERuntimeIREE, Error, TEXT("UNNERuntimeIREEModelDataRDG: Unknown asset version %d: Deserialisation failed, please reimport the original model."), Ar.CustomVer(UE::NNERuntimeIREE::RDG::ModelData::Private::GUID));
		break;
	}

	return Header;
}