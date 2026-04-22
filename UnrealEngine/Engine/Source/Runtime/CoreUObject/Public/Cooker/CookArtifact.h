// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "Containers/UnrealString.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Templates/RefCounting.h"

class ITargetPlatform;
class UCookOnTheFlyServer;
namespace UE::Cook { class ICookInfo; }

namespace UE::Cook::Artifact
{

/** Argument and return value passing structure for ICookArtifact::CompareSettings. */
struct FCompareSettingsContext
{
public:
	ICookInfo& GetCookInfo() const;
	/** The TargetPlatform being tested; invalidation requests are allowed to differ between platforms. Non-null. */
	const ITargetPlatform* GetTargetPlatform() const;
	const FConfigFile& GetPrevious() const;
	const FConfigFile& GetCurrent() const;
	const FString& GetPreviousFileName() const;
	bool IsRequestInvalidate() const;
	bool IsRequestFullRecook() const;

	/**
	 * Request an invalidation of just this artifact. Initial value is false. OnInvalidate will be called, unless
	 * the cooker or some artifact requests OnFullRecook, in which case OnFullRecook will be called and OnInvalidate
	 * will not be called.
	 */
	void RequestInvalidate(bool bValue);
	/**
	 * Request a full recook; all artifacts invalidated and all packages recooked. Initial value is false.
	 * OnFullRecook will be called. OnInvalidate will not be called.
	 */
	void RequestFullRecook(bool bValue);

private:
	FCompareSettingsContext(ICookInfo& InCookInfo, const FConfigFile& InPrevious, const FConfigFile& InCurrent,
		const FString& InPreviousFileName);

	ICookInfo& CookInfo;
	const ITargetPlatform* TargetPlatform = nullptr;
	const FConfigFile& Previous;
	const FConfigFile& Current;
	const FString& PreviousFileName;
	bool bRequestInvalidate = false;
	bool bRequestFullRecook = false;

	friend UCookOnTheFlyServer;
};

} // namespace UE::Cook::Artifact

namespace UE::Cook
{

/**
 * Interface used during cooking for systems that create an artifact collected from cooked packages.
 * Provides hooks to save global settings that invalidate the artifact when changed.
 * Provides hooks to clean the artifact when invalidated.
 */
class ICookArtifact : public FRefCountBase
{
public:
	virtual ~ICookArtifact() {}

	/** The name of the artifact in log messages and its Metadata/CookSettings_<ArtifactName>.txt file. */
	virtual FString GetArtifactName() const = 0;

	/**
	 * Construct a container of config settings from the current executable, config, and global assets.
	 * If non-empty, it will be passed to CompareSettings each time the cook can cook incrementally,
	 * and will be saved to disk in the Metadata folder. If the FConfigFile is empty, it will not be saved,
	 * CompareSettings will be called with empty Previous and Current values.
	 */
	virtual FConfigFile CalculateCurrentSettings(ICookInfo& CookInfo, const ITargetPlatform* TargetPlatform)
	{
		return FConfigFile();
	}

	/**
	 * Compare the results of the previous cook's CalculateCurrentSettings with the current results, and
	 * report to the cooker if needs to invalidate the artifact constructed by the previous cook.
	 */
	virtual void CompareSettings(UE::Cook::Artifact::FCompareSettingsContext& Context)
	{
	}

	/**
	 * Called from the cooker when a full recook was not required and CompareSettings requested invalidate.
	 * This function must clean the artifact's files under Saved\Cooked\<Platform> and any other invalidated files
	 * stored elsewhere.
	 */
	virtual void OnInvalidate(const ITargetPlatform* TargetPlatform)
	{
	}

	/**
	 * Called from the cooker when a full recook was required by the cooker or by CompareSettings from any
	 * artifact. Any files under Saved\Cooked\<Platform> will be deleted by the cooker; this function is responsible
	 * for cleaning invalidated files stored elsewhere.
	 */
	virtual void OnFullRecook(const ITargetPlatform* TargetPlatform)
	{
	}
};


} // namespace UE::Cook


///////////////////////////////////////////////////////
// Inline implementations
///////////////////////////////////////////////////////


namespace UE::Cook::Artifact
{

inline FCompareSettingsContext::FCompareSettingsContext(ICookInfo& InCookInfo,
	const FConfigFile& InPrevious, const FConfigFile& InCurrent, const FString& InPreviousFileName)
	: CookInfo(InCookInfo)
	, Previous(InPrevious)
	, Current(InCurrent)
	, PreviousFileName(InPreviousFileName)
{
}

inline ICookInfo& FCompareSettingsContext::GetCookInfo() const
{
	return CookInfo;
}
inline const ITargetPlatform* FCompareSettingsContext::GetTargetPlatform() const
{
	return TargetPlatform;
}
inline const FConfigFile& FCompareSettingsContext::GetPrevious() const
{
	return Previous;
}
inline const FConfigFile& FCompareSettingsContext::GetCurrent() const
{
	return Current;
}
inline const FString& FCompareSettingsContext::GetPreviousFileName() const
{
	return PreviousFileName;
}
inline bool FCompareSettingsContext::IsRequestInvalidate() const
{
	return bRequestInvalidate;
}
inline bool FCompareSettingsContext::IsRequestFullRecook() const
{
	return bRequestFullRecook;
}
inline void FCompareSettingsContext::RequestInvalidate(bool bValue)
{
	bRequestInvalidate = bValue;
}
inline void FCompareSettingsContext::RequestFullRecook(bool bValue)
{
	bRequestFullRecook = bValue;
}

} // namespace UE::Cook::Artifact

#endif // WITH_EDITOR