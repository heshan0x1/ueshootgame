// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#if USE_ANDROID_JNI
#include "Android/AndroidJavaEnv.h"

namespace UE::Jni::WorkManager
{
	struct FUEWorker: Java::Lang::FObject
	{
		static constexpr FAnsiStringView ClassName = "com/epicgames/unreal/workmanager/UEWorker";

		static void JNICALL nativeAndroidBackgroundServicesOnWorkerStart(JNIEnv* env, jobject thiz, jstring WorkID);
		static void JNICALL nativeAndroidBackgroundServicesOnWorkerStop(JNIEnv* env, jobject thiz, jstring WorkID);

		static constexpr FNativeMethod NativeMethods[]
		{
			UE_JNI_NATIVE_METHOD(nativeAndroidBackgroundServicesOnWorkerStart),
			UE_JNI_NATIVE_METHOD(nativeAndroidBackgroundServicesOnWorkerStop)
		};
	};
}

namespace UE::Jni
{
	template struct TInitialize<WorkManager::FUEWorker>;
}
#endif
