// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "ProfileTree/LaunchProfileTreeData.h"
#include "ILauncherProfile.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Delegates/Delegate.h"

#define UE_API PROJECTLAUNCHER_API

namespace ProjectLauncher
{
	class FLaunchExtension;

	typedef TMulticastDelegate<void()> FPropertyChangedDelegate;

	/**
	 * Base class for a launch extension instance.
	 * Used while editing a specific profile, and when finalizing the command line arguments during profile launch.
	 * 
	 * Created by a specialization of FLaunchExtension as follows:
	 * 
	 * TSharedPtr<ProjectLauncher:FLaunchExtensinInstance> FMyLaunchExtension::CreateInstanceForProfile( ProjectLauncher::FLaunchExtensionInstance::FArgs& InArgs )
	 * {
	 *     return MakeShared<FMyLaunchExtensionInstance>(InArgs);
	 * }
	 */
	class FLaunchExtensionInstance : public TSharedFromThis<FLaunchExtensionInstance>
	{
	public:
		struct FArgs
		{
			ILauncherProfileRef Profile;
			TSharedRef<FModel> Model;
			TSharedRef<FLaunchExtension> Extension;
		};

		UE_API FLaunchExtensionInstance( FArgs& InArgs );
		UE_API virtual ~FLaunchExtensionInstance();

		/**
		 * Returns the parameters that this extension provides. They will be added to the submenu.
		 */
		UE_API virtual bool GetExtensionParameters( TArray<FString>& OutParameters ) const;

		/**
		 * Returns the user-facing name for the given parameter. It will default to the parameter itself.
		 */
		UE_API virtual FText GetExtensionParameterDisplayName( const FString& InParameter ) const;

		/**
		 * Returns the user facing variables that this extension provides, in "$(name)" format
		 */
		UE_API virtual bool GetExtensionVariables( TArray<FString>& OutVariables ) const;

		/**
		 * Returns the current value for the given variable
		 */
		UE_API virtual bool GetExtensionVariableValue( const FString& InParameter, FString& OutValue ) const;

		/**
		 * Hook to allow the extension to extend the extension parameters menu
		 */
		virtual void CustomizeParametersSubmenu( FMenuBuilder& MenuBuilder ) {};

		/**
		 * Hook to allow the extension to add extra fields to the property editing tree, if the tree builder allows it
		 * Property tree items should be hidden until the user has selected something to make it relevent, to avoid cluttering the UI.
		 */
		virtual void CustomizeTree( FLaunchProfileTreeData& ProfileTreeData ) {};

		/**
		 * Advanced hook to allow any advanced modification of the command line when our profile is launched
		 */
		virtual void CustomizeLaunchCommandLine( FString& InOutCommandLine ) {};

		/**
		 * Advanced hook to allow any advanced modification of the command line when an automated test is launched
		 */
		virtual void CustomizeAutomatedTestCommandLine( const ILauncherProfileAutomatedTestRef& InAutomatedTest, FString& InOutCommandLine ) {};

		/**
		 * Notification callback when a property has been changed in our profile
		 */
		virtual void OnPropertyChanged() {};

		/**
		 * Notification callback when the profile is validated
		 */
		virtual void OnValidateProfile() {};

		/**
		 * Describes how this extension is represented in the extensions menu
		 */
		struct FExtensionsMenuEntry
		{
			// internal names
			FString SectionName; // if this is empty, item will be placed in the common section & SectionDisplayName will be ignored
			FString SubmenuName;

			// friendly names
			FText SectionDisplayName;
			FText SubmenuDisplayName;

			// type of menu entry
			enum EType
			{
				Type_None,		// does not appear in the extensions menu
				Type_Section,	// appears in the extensions menu in the SectionName section.
				Type_SubMenu,	// appears in the extensions menu in a submenu of the SectionName section
			};
			EType Type;
		};
		UE_API virtual void GetExtensionsMenuEntry( FExtensionsMenuEntry& MenuEntry ) const;

		/**
		 * Create the default heading for this extension
		 */
		UE_API FLaunchProfileTreeNode& AddDefaultHeading(ProjectLauncher::FLaunchProfileTreeData& ProfileTreeData) const;

		/**
		 * Create the default heading for this extension, with a bespoke display name
		 */
		UE_API FLaunchProfileTreeNode& AddDefaultHeading(ProjectLauncher::FLaunchProfileTreeData& ProfileTreeData, FText OverrideDisplayName) const;

		/*
		 * Populate the custom extension menu for this extension. This menu would typically contain
		 * items that might enable bespoke tree customization options for example.
		 */
		virtual void MakeCustomExtensionSubmenu(FMenuBuilder& MenuBuilder) {}

		/** 
		 * Get the extension that instantiated us
		 */
		inline TSharedRef<FLaunchExtension> GetExtension() const { return Extension; }

		/**
		 * Get the property changed delegate that will be called when an extension changes the profile
		 */
		inline FPropertyChangedDelegate& GetPropertyChangedDelegate() { return PropertyChangedDelegate; }

	protected:

		/**
		 * Determine if the given parameter is on the command line
 		 * 
		 * @param InParameter the parameter to test, e.g. -key=value or -param
		 * @returns whether the parameter is being used
		 */
		UE_API bool IsParameterUsed( const FString& InParameter ) const;

		/** 
		 * Add or remove the given parameter on the command line
		 * 
		 * @param InParameter the parameter to add or remove, e.g. -key=value or -param
		 * @param bUsed whether to add or remove the parameter
		 */
		UE_API void SetParameterUsed( const FString& InParameter, bool bUsed );

		/** 
		 * Add the given parameter to the command line
		 * 
		 * @param InParameter the parameter to add, e.g. -key=value or -param
		 */
		UE_API void AddParameter( const FString& InParameter );

		/** 
		 * Remove the given parameter from the command line
		 * 
		 * @param InParameter the parameter to remove, e.g. -key=value or -param
		 */
		UE_API void RemoveParameter( const FString& InParameter );

		/** 
		 * Retrieve the current value for the given command line parameter
		 * 
		 * @param InParameter the parameter to query, e.g. -key=value
		 * @returns the value of the parameter
		 */
		UE_API FString GetParameterValue( const FString& InParameter ) const;

		/** 
		 * Update the value of the given command line parameter
		 * 
		 * @param InParameter the parameter to modify, e.g. -key=value or -key=
		 * @param InNewValue the new value to use
		 * @returns true unless InParameter is not a key/value property
		 */
		UE_API bool UpdateParameterValue( const FString& InParameter, const FString& InNewValue );

		/** 
		 * Get the current state of the given command line parameter, allowing for user-changed values
		 * 
		 * @param InParameter the parameter to test, e.g. -key=value
		 * @returns The parameter as it is now, allowing for user modifications, e.g. -key=value or -key=some_new_value
		 */
		UE_API FString GetFinalParameter( const FString& InParameter ) const;

		/** 
		 * Get the current command line
		 * Extensions should generally go through this function instead of querying the Profile directlry
		 *
		 * @returns the current command line string
		 */
		UE_API FString GetCommandLine() const;

		/** 
		 * Update the current command line
		 * Extensions should generally go through this function instead of modifying the Profile directlry
		 * 
		 * @param CommandLine the new command line
		 */
		UE_API void SetCommandLine( const FString& CommandLine );

		/** 
		 * Get the profile we were instantiated for
		 */
		inline ILauncherProfileRef GetProfile() const { return Profile; }

		/** 
		 * Get the model, for general purpose helper functions
		 */
		inline TSharedRef<FModel> GetModel() const { return Model; }

		/**
		 * Get the name of the platform to launch on
		 */
		UE_API FName GetLaunchPlatformName() const;

		/** 
		* Enumeration to choose where a value should be stored
		*/
		enum class EConfig : uint8
		{
			User_Common,     ///< value is shared between all instances of this extension
			User_PerProfile, ///< value is specific to this profile & extension
			PerProfile,      ///< value is saved with the profile
		};

		/**
		 * Read a configuration string value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to read
		 * @param DefaultValue value to return if the value isn't found
		 * @returns the value of the string, or DefaultValue if it isn't found
		 */
		UE_API FString GetConfigString( EConfig Config, const TCHAR* Name, const TCHAR* DefaultValue = TEXT("") ) const;

		/**
		 * Read a configuration bool value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to read
		 * @param DefaultValue value to return if the value isn't found
		 * @returns the value of the bool, or DefaultValue if it isn't found
		 */
		UE_API bool GetConfigBool( EConfig Config, const TCHAR* Name, bool DefaultValue = false ) const;

		/**
		 * Read a configuration int value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to read
		 * @param DefaultValue value to return if the value isn't found
		 * @returns the value of the bool, or DefaultValue if it isn't found
		 */
		UE_API int32 GetConfigInteger( EConfig Config, const TCHAR* Name, int32 DefaultValue = 0 ) const;

		/**
		 * Read a configuration float value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to read
		 * @param DefaultValue value to return if the value isn't found
		 * @returns the value of the bool, or DefaultValue if it isn't found
		 */
		UE_API float GetConfigFloat( EConfig Config, const TCHAR* Name, float DefaultValue = 0.0f ) const;

		/**
		 * Write a configuration string value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to write
		 * @param Value value to write
		 */
		UE_API void SetConfigString( EConfig Config, const TCHAR* Name, const FString& Value ) const;

		/**
		 * Write a configuration bool value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to write
		 * @param Value value to write
		 */
		UE_API void SetConfigBool( EConfig Config, const TCHAR* Name, const bool& Value ) const;

		/**
		 * Write a configuration int value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to write
		 * @param Value value to write
		 */
		UE_API void SetConfigInteger( EConfig Config, const TCHAR* Name, const int32& Value ) const;

		/**
		 * Write a configuration float value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value to write
		 * @param Value value to write
		 */
		UE_API void SetConfigFloat( EConfig Config, const TCHAR* Name, const float& Value ) const;



		/**
		 * Get the final key name to use for reading & writing a configuration value
		 * 
		 * @param Config where the value is stored
		 * @param Name name of the value
		 * 
		 * @returns namespaced version of Name
		 */
		UE_API FString GetConfigKeyName( EConfig Config, const TCHAR* Name ) const;

		/**
		 * Signal to any listeners that a property in the profile has changed
		 */
		UE_API void BroadcastPropertyChanged() const;

	private:
		UE_API bool IsParameterGroup( const FString& InParameter ) const;
		UE_API bool TryRemoveParameterGroup( const FString& InParameter );


		ILauncherProfileRef Profile;
		TSharedRef<FModel> Model;
		TSharedRef<FLaunchExtension> Extension;
		FPropertyChangedDelegate PropertyChangedDelegate;

		friend class FLaunchProfileTreeNode;
		UE_API void MakeCommandLineSubmenu( FMenuBuilder& MenuBuilder );

	};


	/**
	 * Helper base class for an automated test launch extension
	 * Creates an item in the extension menu that will toggle the test on and off
	 * note: It is not necessary for an automated test extensions to use this class
	 */
	class FAutomatedTestLaunchExtensionInstance : public FLaunchExtensionInstance
	{
	public:
		UE_API FAutomatedTestLaunchExtensionInstance( FArgs& InArgs );

		/*
		 * Populate the custom extension menu for this automated test extension. This will add a menu item to toggle the test on and off
		 */
		UE_API virtual void MakeCustomExtensionSubmenu( FMenuBuilder& MenuBuilder ) override;

		/**
		 * Describes how this extension is represented in the extensions menu - a common section for all automated tests
		 */
		UE_API virtual void GetExtensionsMenuEntry( FExtensionsMenuEntry& MenuEntry ) const override;
		
		/**
		 * Determine if this automated test is currently active
		 */
		UE_API bool IsTestActive() const;

	protected:
		UE_API void CustomizeAutomatedTestCommandLine( const ILauncherProfileAutomatedTestRef& InAutomatedTest, FString& InOutCommandLine ) override;
		virtual bool HasCustomExtensionMenu() const { return true; }

		/**
		 * Advanced hook to allow any advanced modification of the command line when this extension's automated test is launched
		 */
		virtual void CustomizeAutomatedTestCommandLine( FString& InOutCommandLine ) {}

		/**
		 * Hook to allow any modification of the automated test & profile once it has been created. At least the test name should be specified
		 */
		virtual void OnTestAdded( ILauncherProfileAutomatedTestRef AutomatedTest ) = 0;

		/**
		 * Advanced hook to allow any modification of the profile just before the automated test is removed
		 */
		virtual void OnTestRemoved( ILauncherProfileAutomatedTestRef AutomatedTest ) {}

		/**
		 * Access this extension's automated test if it has been created
		 */
		UE_API ILauncherProfileAutomatedTestPtr GetTest() const;

		/**
		 * Internal name for the automated test. Must be globally unique, so recommended to include the extension name
		 */
		virtual const FString GetTestInternalName() const = 0;
	};






	/**
	 * Base class for a launch extension.
	 * 
	 * Singleton instance is registered with this plugin during initialization as follows:
	 * 
	 *   TSharedPtr<FMyLaunchExtension> MyExtension = MakeShared<FMyLaunchExtension>();
	 *   IProjectLauncherModule::Get().RegisterExtension( MyExtension.ToSharedRef() );
	 */
	class FLaunchExtension : public TSharedFromThis<FLaunchExtension>
	{
	public:
		virtual ~FLaunchExtension() = default;

		/**
		 * Returns the debug name for this extension
		 */
		virtual const TCHAR* GetInternalName() const = 0;

		/**
		 * Returns the user-facing name for this extension
		 */
		virtual FText GetDisplayName() const = 0;


		/**
		 * Instantiate all compatible extensions
		 * 
		 * @param InProfile the current profile 
		 * @param InModel helper class
		 * @returns array of all compatible extensions
		 */
		UE_API static TArray<TSharedPtr<FLaunchExtensionInstance>> CreateExtensionInstancesForProfile( ILauncherProfileRef InProfile, TSharedRef<FModel> InModel );


	private:
		/**
		 * Create an instance of the launch extension for the given profile - called internally by CreateExtensionInstancesForProfile
		 *
		 * @returns: new instance, or null if it isn't appropriate
		 */
		virtual TSharedPtr<FLaunchExtensionInstance> CreateInstanceForProfile( FLaunchExtensionInstance::FArgs& InArgs ) = 0;
	};


};

#undef UE_API
