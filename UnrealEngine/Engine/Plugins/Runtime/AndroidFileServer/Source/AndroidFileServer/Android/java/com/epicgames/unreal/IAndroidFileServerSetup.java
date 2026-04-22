// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

public interface IAndroidFileServerSetup
{
	public boolean AndroidFileServer_Init(String filename);
	public boolean AndroidFileServer_Verify(String Token);
}
