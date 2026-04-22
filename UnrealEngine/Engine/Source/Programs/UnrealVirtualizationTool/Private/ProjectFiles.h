// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/StringFwd.h"
#include "Containers/UnrealString.h"
#include "Serialization/JsonSerializerMacros.h"

namespace UE::Virtualization
{

enum EProjectType : int32
{
	Unknown = -1,	///< Error condition

	GameProject,	///< .uproject
	UEFNProject		///< .uefnproject
};

const TCHAR* LexToString(UE::Virtualization::EProjectType Type);
void LexFromString(UE::Virtualization::EProjectType& OutType, const TCHAR* InString);

struct FPlugin : public FJsonSerializable
{
public:
	FString PluginFilePath;

	TArray<FString> PackagePaths;

public:
	BEGIN_JSON_SERIALIZER
		JSON_SERIALIZE("PluginPath", PluginFilePath);
		JSON_SERIALIZE_ARRAY("PackagePaths", PackagePaths);
	END_JSON_SERIALIZER
};

class FProject : public FJsonSerializable
{
public:
	

	FProject() = default;
	FProject(FString&& ProjectFilePath);
	~FProject() = default;

	void AddFile(const FString& FilePath);
	void AddPluginFile(const FString& FilePath, FString&& PluginFilePath);

	/**
	 * Returns the path of the project file that we should use for the content within this project.
	 * This can be different from the project path that was used to create the FProject.
	 */
	const FString& GetProjectFilePath() const;

	FStringView GetProjectName() const;
	EProjectType GetProjectType() const;

	bool DoesMatchProjectPath(FStringView Path) const;

	TArray<FString> GetAllPackages() const;
	int32 GetNumPackages() const;

	void RegisterMountPoints() const;
	void UnRegisterMountPoints() const;

	bool TryLoadConfig(FConfigFile& OutConfig) const;

private:

	FString ProjectFilePath;			// The original path for the project (can be any type supported by EProjectType)

	FString OverridenProjectFilePath;	// The path of the project to use for sourcing virtualization settings. This 
										// must always be of type EProjectType::GameProject. This will be used in one
										// of two scenarios:
										// 1) The package files  being processes are currently in one project but the settings
										// to access the virtualized data for these files are found in another project. This
										// can happen if the package files have been moved from one virtualized project to
										// another project and now need to be rehydrated for example. In this scenario the
										// overridden path can be provided on the command line via '-SourceProject=???'
										// 2) The ProjectFilePath is of the type EProjectType::UEFNProject but has a valid
										// .uproject further up in the directory hierarchy. In this case the virtualization
										// settings will be contained in this .uproject and the overriden path will be 
										// calculated automatically.

	TArray<FPlugin> Plugins;
	TArray<FString> PackagePaths;

	EProjectType ProjectType = EProjectType::Unknown;

	void ProcessProjectPath();

public:
	BEGIN_JSON_SERIALIZER
		JSON_SERIALIZE("ProjectPath", ProjectFilePath);
		JSON_SERIALIZE("OverridenProjectPath", OverridenProjectFilePath);
		JSON_SERIALIZE_ARRAY_SERIALIZABLE("Plugins", Plugins, FPlugin);
		JSON_SERIALIZE_ARRAY("PackagePaths", PackagePaths);
		JSON_SERIALIZE_ENUM("ProjectType", ProjectType);
	END_JSON_SERIALIZER
};

} //namespace UE::Virtualization
