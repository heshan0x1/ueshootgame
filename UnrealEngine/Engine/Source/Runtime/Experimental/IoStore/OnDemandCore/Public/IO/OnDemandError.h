// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Experimental/UnifiedError/UnifiedError.h"
#include "Experimental/UnifiedError/CoreErrorTypes.h"
#include "IO/IoChunkId.h"
#include "IO/IoStatus.h"
#include "Templates/ValueOrError.h"

#define UE_API IOSTOREONDEMANDCORE_API

////////////////////////////////////////////////////////////////////////////////
UE_DECLARE_ERROR_MODULE(UE_API, IoStoreOnDemand);

UE_DECLARE_ERROR_ONEPARAM(UE_API,	HttpError,							1,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "HttpError", "HTTP error ({StatusCode})"), uint32, StatusCode, 0);
UE_DECLARE_ERROR(UE_API,			ChunkMissingError,					2,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "ChunkMissingError", "Chunk missing error"));
UE_DECLARE_ERROR(UE_API,			ChunkHashError,						3,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "ChunkHashError", "Chunk hash mismatch error"));
UE_DECLARE_ERROR(UE_API,			InstallCacheFlushError,				4,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "InstallCacheFlushError", "Failed to flush pending data to install cache."));
UE_DECLARE_ERROR(UE_API,			InstallCacheFlushLastAccessError,	5,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "InstallCacheFlushLastAccessError", "Failed to flush last access timestamp(s) to journal."));
UE_DECLARE_ERROR(UE_API,			InstallCachePurgeError,				6,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "InstallCachePurgeError", "Failed to purge unreferenced cache block(s) from the install cache."));
UE_DECLARE_ERROR(UE_API,			InstallCacheDefragError,			7,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "InstallCacheDefragError", "Failed to defrag the install cache."));
UE_DECLARE_ERROR(UE_API,			InstallCacheVerificationError,		8,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "InstallCacheVerificationError", "Verification of installed install cache data failed."));
UE_DECLARE_ERROR(UE_API,			CasError,							9,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "CasError", "Cas error"));
UE_DECLARE_ERROR(UE_API,			CasJournalError,					10,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "CasJournalError", "Cas journal error"));
UE_DECLARE_ERROR(UE_API,			CasSnapshotError,					11,	IoStoreOnDemand, NSLOCTEXT("IoStoreOnDemand", "CasSnapshotError", "Cas snapshot error"));

////////////////////////////////////////////////////////////////////////////////
namespace UE::IoStore
{

using FResult = TValueOrError<void, UE::UnifiedError::FError>;

template <typename ResultType>
using TResult = TValueOrError<ResultType, UE::UnifiedError::FError>;

} // namespace UE::IoStore

////////////////////////////////////////////////////////////////////////////////
namespace UE::UnifiedError::IoStoreOnDemand
{

enum class ECasErrorCode : uint32
{
	None,
	InitializeFailed,
	VerifyFailed,
	ReadBlockFailed,
	WriteBlockFailed,
	DeleteBlockFailed,
	CreateJournalFailed,
	ReplayJournalFailed,
	CommitJournalFailed,
	CreateSnapshotFailed,
	LoadSnapshotFailed,
	SaveSnapshotFailed
};

inline const TCHAR* ToString(ECasErrorCode Code)
{
	switch (Code)
	{
		case ECasErrorCode::None:
			return TEXT("None");
		case ECasErrorCode::InitializeFailed:
			return TEXT("InitializeFailed");
		case ECasErrorCode::VerifyFailed:
			return TEXT("VerifyFailed");
		case ECasErrorCode::ReadBlockFailed:
			return TEXT("ReadBlockFailed");
		case ECasErrorCode::WriteBlockFailed:
			return TEXT("WriteBlockFailed");
		case ECasErrorCode::DeleteBlockFailed:
			return TEXT("DeleteBlockFailed");
		case ECasErrorCode::CreateJournalFailed:
			return TEXT("CreateJournalFailed");
		case ECasErrorCode::ReplayJournalFailed:
			return TEXT("ReplayJournalFailed");
		case ECasErrorCode::CommitJournalFailed:
			return TEXT("CommitJournalFailed");
		case ECasErrorCode::CreateSnapshotFailed:
			return TEXT("CreateSnapshotFailed");
		case ECasErrorCode::LoadSnapshotFailed:
			return TEXT("LoadSnapshotFailed");
		case ECasErrorCode::SaveSnapshotFailed:
			return TEXT("SaveSnapshotFailed");
		default:
			return TEXT("<InvalidErrorCode>");
	};
}

struct FCasErrorContext
{
	ECasErrorCode	ErrorCode = ECasErrorCode::None;
	EIoErrorCode	IoErrorCode = EIoErrorCode::Unknown;
	uint32			SystemErrorCode = 0;
	FString			ErrorMessage;
};

template <typename ResultType>
inline UE::IoStore::TResult<ResultType> MakeCasError(
	ECasErrorCode ErrorCode,
	EIoErrorCode IoErrorCode,
	FString&& ErrorMessage,
	uint32 SystemErrorCode = FPlatformMisc::GetLastError())
{
	return MakeError(CasError::MakeError(FCasErrorContext
	{
		.ErrorCode			= ErrorCode,
		.IoErrorCode		= IoErrorCode,
		.SystemErrorCode	= SystemErrorCode,
		.ErrorMessage		= MoveTemp(ErrorMessage)
	}, UE::UnifiedError::EDetailFilter::All));
}

inline UE::IoStore::FResult MakeJournalError(
	ECasErrorCode ErrorCode,
	EIoErrorCode IoErrorCode,
	FString&& ErrorMessage,
	uint32 SystemErrorCode = FPlatformMisc::GetLastError())
{
	return MakeError(CasJournalError::MakeError(FCasErrorContext
	{
		.ErrorCode			= ErrorCode,
		.IoErrorCode		= IoErrorCode,
		.SystemErrorCode	= SystemErrorCode,
		.ErrorMessage		= MoveTemp(ErrorMessage)
	}));
}

template <typename ResultType>
inline UE::IoStore::TResult<ResultType> MakeSnapshotError(
	ECasErrorCode ErrorCode,
	EIoErrorCode IoErrorCode,
	FString&& ErrorMessage,
	uint32 SystemErrorCode = FPlatformMisc::GetLastError())
{
	using namespace UE::UnifiedError;
	using namespace UE::UnifiedError::IoStoreOnDemand;

	return MakeError(CasSnapshotError::MakeError(FCasErrorContext
	{
		.ErrorCode			= ErrorCode,
		.IoErrorCode		= IoErrorCode,
		.SystemErrorCode	= SystemErrorCode,
		.ErrorMessage		= MoveTemp(ErrorMessage)
	}));
}

struct FChunkMissingErrorContext
{
	TArray<FIoChunkId> ChunkIds;
};

struct FChunkHashMismatchErrorContext
{
	FIoChunkId	ChunkId;
	FIoHash		ExpectedHash;
	FIoHash		ActualHash;
};

struct FInstallCacheErrorContext
{
	FIoStatus		IoError = FIoStatus(EIoErrorCode::Unknown);
	uint64			MaxCacheSize = 0;
	uint64			CacheSize = 0;
	uint64			DiskTotalBytes = 0;
	uint64			DiskFreeBytes = 0;
	uint32			LineNo = uint32(__builtin_LINE());
	bool			bDiskQuerySucceeded = false;
};

struct FVerificationErrorContext
{
	uint32	CorruptChunkCount = 0;
	uint32	MissingChunkCount = 0;
	uint32	ReadErrorCount = 0;
};

} // namespace UE::UnifiedError::IoStoreOnDemand

////////////////////////////////////////////////////////////////////////////////
UE_DECLARE_ERRORSTRUCT_FEATURES(IoStoreOnDemand, FCasErrorContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::IoStoreOnDemand::FCasErrorContext& Ctx)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::IoStoreOnDemand::FCasErrorContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("({ErrorCodeText}: {ErrorMessage})"));
	Writer.AddInteger(ANSITEXTVIEW("ErrorCode"), uint32(Ctx.ErrorCode));
	Writer.AddString(ANSITEXTVIEW("ErrorCodeText"), ToString(Ctx.ErrorCode));
	Writer.AddInteger(ANSITEXTVIEW("IoErrorCode"), uint32(Ctx.IoErrorCode));
	Writer.AddString(ANSITEXTVIEW("IoErrorCodeText"), GetIoErrorText(Ctx.IoErrorCode));
	Writer.AddString(ANSITEXTVIEW("ErrorMessage"), Ctx.ErrorMessage); 
	Writer.EndObject();
}
////////////////////////////////////////////////////////////////////////////////
UE_DECLARE_ERRORSTRUCT_FEATURES(IoStoreOnDemand, FChunkMissingErrorContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::IoStoreOnDemand::FChunkMissingErrorContext& Ctx)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::IoStoreOnDemand::FChunkMissingErrorContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("(MissingCount: {MissingCount}, ChunkIds: [{ChunkIds}])"));

	Writer.AddInteger(ANSITEXTVIEW("MissingCount"), Ctx.ChunkIds.Num());

	TUtf8StringBuilder<1024> Builder;
	Builder.Join(Ctx.ChunkIds, UTF8TEXTVIEW(", "));
	Writer.AddString(ANSITEXTVIEW("ChunkIds"), Builder);
	
	Writer.EndObject();
}
////////////////////////////////////////////////////////////////////////////////
UE_DECLARE_ERRORSTRUCT_FEATURES(IoStoreOnDemand, FChunkHashMismatchErrorContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::IoStoreOnDemand::FChunkHashMismatchErrorContext& Ctx)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::IoStoreOnDemand::FChunkHashMismatchErrorContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("(ChunkId: {ChunkId}, ExpectedHash: {ExpectedHash}, ActualHash: {ActualHash})"));
	Writer.AddString(ANSITEXTVIEW("ChunkId"), LexToString(Ctx.ChunkId));
	Writer.AddString(ANSITEXTVIEW("ExpectedHash"), LexToString(Ctx.ExpectedHash));
	Writer.AddString(ANSITEXTVIEW("ActualHash"), LexToString(Ctx.ActualHash));
	Writer.EndObject();
}
////////////////////////////////////////////////////////////////////////////////
UE_DECLARE_ERRORSTRUCT_FEATURES(IoStoreOnDemand, FInstallCacheErrorContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::IoStoreOnDemand::FInstallCacheErrorContext& Ctx)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::IoStoreOnDemand::FInstallCacheErrorContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("({ErrorMessage})"));
	Writer.AddInteger(ANSITEXTVIEW("ErrorCode"), int32(Ctx.IoError.GetErrorCode()));
	Writer.AddString(ANSITEXTVIEW("ErrorMessage"), Ctx.IoError.ToString());
	Writer.AddInteger(ANSITEXTVIEW("SystemErrorCode"), int32(Ctx.IoError.GetSystemErrorCode()));
	Writer.AddInteger(ANSITEXTVIEW("MaxCacheSize"), Ctx.MaxCacheSize);
	Writer.AddInteger(ANSITEXTVIEW("CacheSize"), Ctx.CacheSize);
	Writer.AddInteger(ANSITEXTVIEW("DiskTotalBytes"), Ctx.DiskTotalBytes);
	Writer.AddInteger(ANSITEXTVIEW("DiskFreeBytes"), Ctx.DiskFreeBytes);
	Writer.AddInteger(ANSITEXTVIEW("LineNo"), uint32(Ctx.LineNo));
	Writer.EndObject();
}
////////////////////////////////////////////////////////////////////////////////
UE_DECLARE_ERRORSTRUCT_FEATURES(IoStoreOnDemand, FVerificationErrorContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::IoStoreOnDemand::FVerificationErrorContext& Ctx)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::IoStoreOnDemand::FVerificationErrorContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("(CorruptChunkCount: {CorruptChunkCount}, MissingChunkCount: {MissingChunkCount}, ReadErrorCount: {ReadErrorCount})"));
	Writer.AddInteger(ANSITEXTVIEW("CorruptChunkCount"), Ctx.CorruptChunkCount);
	Writer.AddInteger(ANSITEXTVIEW("MissingChunkCount"), Ctx.MissingChunkCount);
	Writer.AddInteger(ANSITEXTVIEW("ReadErrorCount"), Ctx.ReadErrorCount);
	Writer.EndObject();
}

#undef UE_API
