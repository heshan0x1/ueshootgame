// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOS/IOSPlatformWebAuth.h"
#if PLATFORM_IOS && !PLATFORM_TVOS

#import <Foundation/Foundation.h>
#import <AuthenticationServices/AuthenticationServices.h>

#import <WebKit/WebKit.h>

#include "IOS/IOSAsyncTask.h"
#include "IOS/IOSAppDelegate.h"

#include "WebAuthModule.h"

@interface PresentationContext : NSObject <ASWebAuthenticationPresentationContextProviding>
{
}
@end

@implementation PresentationContext

- (ASPresentationAnchor)presentationAnchorForWebAuthenticationSession:(ASWebAuthenticationSession *)session
{
	if ([IOSAppDelegate GetDelegate].Window == nullptr)
	{
		NSLog(@"authorizationController: presentationAnchorForAuthorizationController: error window is NULL");
	}
	return [IOSAppDelegate GetDelegate].Window;
}

@end

static NSMutableDictionary* NewSearchDictionary(NSString *EnvironmentName, bool bUseDeprecatedLocation);
static NSData* GetStoredCredentialsData(NSString *EnvironmentName, bool bUseDeprecatedLocation, OSStatus& OutStatus);
static void MigrateCredentialsData(NSString* EnvironmentName, NSData* CredentialsData);
static PresentationContext* PresentationContextProvider = nullptr;

bool FIOSWebAuth::AuthSessionWithURL(const FString &UrlStr, const FString &SchemeStr, const FWebAuthSessionCompleteDelegate& Delegate)
{
	FTCHARToUTF8 TCUrlStr(*UrlStr);
	NSString *NSUrlStr = [NSString stringWithUTF8String:TCUrlStr.Get()];
	NSURL *Url = [NSURL URLWithString:NSUrlStr];

	FTCHARToUTF8 TCSchemeStr(*SchemeStr);
	NSString *Scheme = [NSString stringWithUTF8String:TCSchemeStr.Get()];

	NSLog(@"AuthSessionWithURL Url=[%@], CallbackUrlScheme=[%@]", NSUrlStr, Scheme);

    AuthSessionCompleteDelegate = Delegate;
	FIOSCoreDelegates::OnOpenURL.Remove(OpenUrlHandle);

	dispatch_async(dispatch_get_main_queue(), ^ {
		ASWebAuthenticationSession* SavedAuthSession = [[ASWebAuthenticationSession alloc] initWithURL:Url callbackURLScheme: Scheme completionHandler:^(NSURL * _Nullable callbackURL, NSError * _Nullable error)
		{
			[FAppleAsyncTask CreateTaskWithBlock : ^ bool(void)
			 {
				FIOSCoreDelegates::OnOpenURL.Remove(OpenUrlHandle);
				// Response received
				if (callbackURL != nil)
				{
					const char *StrCallbackURL = [callbackURL.absoluteString UTF8String];
					AuthSessionCompleteDelegate.ExecuteIfBound(UTF8_TO_TCHAR(StrCallbackURL), true);
				}
				// Empty response
				else
				{
					AuthSessionCompleteDelegate.ExecuteIfBound(FString(), false);
				}
				AuthSessionCompleteDelegate = nullptr;
				return true;
			}];
		}];
		
		check(PresentationContextProvider);
		SavedAuthSession.presentationContextProvider = PresentationContextProvider;
		
		[FAppleAsyncTask CreateTaskWithBlock : ^ bool(void)
		{
			OpenUrlHandle = FIOSCoreDelegates::OnOpenURL.AddLambda([this, AppScheme = FString(Scheme), SavedAuthSession](UIApplication* application, NSURL* url, NSString* sourceApplication, id annotation)
			{
				if (FString(url.scheme) == AppScheme)
				{
					// FORT-809099 : Retain and after calling cancel release SavedAuthSession. This is to avoid a bug in [ASWebAuthenticationSession cancel]
					// SavedAuthSession will be deallocated inside of cancel because documentation says we should not keep any strong reference to it.
					// That makes the inner SFAuthenticationSession instance ASWebAuthenticationSession uses to be captured in a deallocated state deep inside the API
					// When that zombie instance is attempted to be released when the browser UI has transitioned out it may potentially crash.
					[SavedAuthSession retain];
					[SavedAuthSession cancel];
					[SavedAuthSession release];
					
					[FAppleAsyncTask CreateTaskWithBlock : ^ bool(void)
					 {
						FIOSCoreDelegates::OnOpenURL.Remove(OpenUrlHandle);
						const char* StrCallbackURL = [url.absoluteString UTF8String];
						AuthSessionCompleteDelegate.ExecuteIfBound(UTF8_TO_TCHAR(StrCallbackURL), true);
						AuthSessionCompleteDelegate = nullptr;
						return true;
					}];
				}
			});
			dispatch_async(dispatch_get_main_queue(), ^ {
				[SavedAuthSession start];
				[SavedAuthSession release];
			});
			return true;
		}];
	});

	return true;
}

NSMutableDictionary* NewSearchDictionary(NSString *EnvironmentName, bool bUseDeprecatedLocation)
{
	NSString* ServiceName = nil;
	if (bUseDeprecatedLocation)
	{
		ServiceName = [[UIDevice currentDevice] identifierForVendor].UUIDString;
	}
	else
	{
		ServiceName = [[NSBundle mainBundle] bundleIdentifier];
	}
	NSString* keyName = [NSString stringWithFormat:@"DeviceCredentials_%@", EnvironmentName];
	NSData* EncodedIdentifier = [keyName dataUsingEncoding:NSUTF8StringEncoding];

	NSMutableDictionary* SearchDictionary = [[NSMutableDictionary alloc] init];
	[SearchDictionary setObject:(id)kSecClassGenericPassword forKey:(id)kSecClass];
	[SearchDictionary setObject:EncodedIdentifier forKey:(id)kSecAttrGeneric];
	[SearchDictionary setObject:EncodedIdentifier forKey:(id)kSecAttrAccount];
	[SearchDictionary setObject:ServiceName forKey:(id)kSecAttrService];

	return SearchDictionary;
}

NSData* GetStoredCredentialsData(NSString *EnvironmentName, bool bUseDeprecatedLocation, OSStatus& OutStatus)
{
	NSMutableDictionary* SearchDictionary = NewSearchDictionary(EnvironmentName, bUseDeprecatedLocation);

	// a couple extra params for retrieval
	[SearchDictionary setObject:(id)kSecMatchLimitOne forKey:(id)kSecMatchLimit];
	// Add search return types
	[SearchDictionary setObject:(id)kCFBooleanTrue forKey:(id)kSecReturnData];

	CFTypeRef CFResult = nil;
	OutStatus = SecItemCopyMatching((__bridge CFDictionaryRef)SearchDictionary, (CFTypeRef *)&CFResult);
	[SearchDictionary release];

	NSData* Result = nil;
	if (OutStatus == errSecSuccess)
	{
		// only get the Results if we succeeded
		Result = (__bridge NSData*)CFResult;
	}
	return Result;
}

void MigrateCredentialsData(NSString* EnvironmentName, NSData* CredentialsData)
{
	NSMutableDictionary* NewLocationSearchDictionary = NewSearchDictionary(EnvironmentName, false);
	
	// Save the credentials in the new location. We do not need to clear them first as we only migrate if there are no credentials present there
	[NewLocationSearchDictionary setObject:CredentialsData forKey:(id)kSecValueData];
	OSStatus Status = SecItemAdd((CFDictionaryRef)NewLocationSearchDictionary, NULL);
	
	if (Status == errSecSuccess)
	{
		// Now that the credentials are successfully stored in the new location, clear the old location
		NSMutableDictionary* OldLocationSearchDictionary = NewSearchDictionary(EnvironmentName, true);
		SecItemDelete((CFDictionaryRef)OldLocationSearchDictionary);
		[OldLocationSearchDictionary release];
		
		UE_LOG(LogWebAuth, Verbose, TEXT("Migrated credentials to new location."));
	}
	else
	{
		UE_LOG(LogWebAuth, Warning, TEXT("Failed to migrate credentials. Status = %d"), Status);
	}
	
	[NewLocationSearchDictionary release];
}

bool FIOSWebAuth::SaveCredentials(const FString& IdStr, const FString& TokenStr, const FString& EnvironmentNameStr)
{
	FTCHARToUTF8 TCIdStr(*IdStr);
	NSString *Id = [NSString stringWithUTF8String:TCIdStr.Get()];
	FTCHARToUTF8 TCTokenStr(*TokenStr);
	NSString *Token = [NSString stringWithUTF8String:TCTokenStr.Get()];
	FTCHARToUTF8 TCEnvironmentNameStr(*EnvironmentNameStr);
	NSString *EnvironmentName = [NSString stringWithUTF8String:TCEnvironmentNameStr.Get()];

	NSMutableDictionary* SearchDictionary = NewSearchDictionary(EnvironmentName, false);

	// erase any existing one
	SecItemDelete((CFDictionaryRef)SearchDictionary);

	// if we have nil/empty params, we just delete and leave
	if (Id == nil || Id.length == 0 || Token == nil || Token.length == 0)
	{
		UE_LOG(LogWebAuth, Verbose, TEXT("Cleared existing credentials"));
		return true;
	}

	// make a data blob of array of strings
	NSData* CredentialsData = [NSKeyedArchiver archivedDataWithRootObject:@[Id, Token] requiringSecureCoding:NO error:nil];
	[SearchDictionary setObject:CredentialsData forKey:(id)kSecValueData];

	// add it
	OSStatus Status = SecItemAdd((CFDictionaryRef)SearchDictionary, NULL);
	if (Status == errSecSuccess)
	{
		UE_LOG(LogWebAuth, Verbose, TEXT("Saved credentials"));
	}
	else
	{
		UE_LOG(LogWebAuth, Warning, TEXT("Failed to save credentials. Status = %d"), Status);
	}
    
    [SearchDictionary release];
	return Status == errSecSuccess;
}

bool FIOSWebAuth::LoadCredentials(FString& OutIdStr, FString& OutTokenStr, const FString& EnvironmentNameStr)
{
	FTCHARToUTF8 TCEnvironmentNameStr(*EnvironmentNameStr);
	NSString *EnvironmentName = [NSString stringWithUTF8String:TCEnvironmentNameStr.Get()];

	OSStatus Status = errSecSuccess;
	NSData* Result = GetStoredCredentialsData(EnvironmentName, false, Status);
	if (Result == nil)
	{
		Result = GetStoredCredentialsData(EnvironmentName, true, Status);
		
		if (Result != nil)
		{
			// migrate data to new location
			MigrateCredentialsData(EnvironmentName, Result);
		}
	}

	if (Result)
	{
		// convert data blob back to an array
		NSArray* CredentialsArray = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSArray class] fromData:Result error:nil];
		if ([CredentialsArray count] == 2 && [[CredentialsArray objectAtIndex:0] isKindOfClass:[NSString class]] && [[CredentialsArray objectAtIndex:1] isKindOfClass:[NSString class]])
		{
			NSString* Id = [CredentialsArray objectAtIndex:0];
			NSString* Token = [CredentialsArray objectAtIndex:1];

			const char *CStr = [Id UTF8String];
			OutIdStr = FString(CStr);

			CStr = [Token UTF8String];
			OutTokenStr = FString(CStr);

			UE_LOG(LogWebAuth, Verbose, TEXT("Retrieved credentials successfully"));
		}
		else
		{
			UE_LOG(LogWebAuth, Warning, TEXT("Retrieved credentials, but they were poorly formatted, failing."));
			Status = errSecParam;
		}
	}
	else
	{
		if (Status == errSecItemNotFound)
		{
			UE_LOG(LogWebAuth, Log, TEXT("Failed to retrieve credentials, no credentials found"));
		}
		else
		{
			UE_LOG(LogWebAuth, Warning, TEXT("Failed to retrieve credentials, status = %d"), Status);
		}
	}

	if (Status != errSecSuccess)
	{
		OutIdStr = FString();
		OutTokenStr = FString();
	}

	return Status == errSecSuccess;
}

void FIOSWebAuth::DeleteLoginCookies(const FString& PrefixStr, const FString& SchemeStr, const FString& DomainStr, const FString& PathStr)
{
	FTCHARToUTF8 TCPrefixStrStr(*PrefixStr);
	NSString *Prefix = [NSString stringWithUTF8String:TCPrefixStrStr.Get()];

	FTCHARToUTF8 TCDomainStr(*DomainStr);
	NSString *LoginDomain = [NSString stringWithUTF8String:TCDomainStr.Get()];

    WKHTTPCookieStore* CookieStore = [[WKWebsiteDataStore defaultDataStore] httpCookieStore];

    [CookieStore getAllCookies:^(NSArray<NSHTTPCookie*>* Cookies)
        {
            NSLog(@"Clearing cookies for domain %@", LoginDomain);
            for (NSHTTPCookie* Cookie in Cookies)
            {
                if ([[Cookie domain] hasSuffix:LoginDomain] && [[Cookie name] hasPrefix:Prefix])
                {
                    [CookieStore deleteCookie:Cookie completionHandler:nil];
                }
            }
        }];
}

FIOSWebAuth::FIOSWebAuth()
{
	PresentationContextProvider = [PresentationContext new];
}

FIOSWebAuth::~FIOSWebAuth()
{
	FIOSCoreDelegates::OnOpenURL.Remove(OpenUrlHandle);
	if (PresentationContextProvider != nil)
	{
		[PresentationContextProvider release];
		PresentationContextProvider = nil;
	}
}


IWebAuth* FIOSPlatformWebAuth::CreatePlatformWebAuth()
{
	return new FIOSWebAuth();
}

#endif // PLATFORM_IOS && !PLATFORM_TVOS
