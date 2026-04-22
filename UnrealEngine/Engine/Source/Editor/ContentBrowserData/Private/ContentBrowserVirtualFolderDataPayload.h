// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserItemData.h"

class FContentBrowserVirtualFolderItemDataPayload : public IContentBrowserItemDataPayload
{
public:
	explicit FContentBrowserVirtualFolderItemDataPayload (bool InIsCustomVirtual)
        : bIsCustomVirtualFolder(InIsCustomVirtual)
	{
	}

private:
    bool bIsCustomVirtualFolder;
};
