// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMEditorRemoveUnusedMembersListItem.h"

namespace UE::RigVMEditor
{
	FRigVMEditorRemoveUnusedMembersListItem::FRigVMEditorRemoveUnusedMembersListItem(
		const FName& InMemberName, 
		const bool bInInitiallySelected)
		: MemberName(InMemberName)
		, bSelected(bInInitiallySelected)
	{}
}
