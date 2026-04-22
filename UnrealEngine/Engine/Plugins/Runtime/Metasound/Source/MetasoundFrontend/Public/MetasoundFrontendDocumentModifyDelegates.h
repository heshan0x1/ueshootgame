// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Delegates/DelegateCombinations.h"
#include "MetasoundFrontendDocument.h"
#include "Templates/SharedPointer.h"

#define UE_API METASOUNDFRONTEND_API


// TODO: Move these to namespace
DECLARE_MULTICAST_DELEGATE(FOnMetaSoundFrontendDocumentMutateMetadata);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateArray, int32 /* Index */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateInterfaceArray, const FMetasoundFrontendInterface& /* Interface */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetaSoundFrontendDocumentRemoveSwappingArray, int32 /* Index */, int32 /* LastIndex */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetaSoundFrontendDocumentRenameClass, const int32 /* Index */, const FMetasoundFrontendClassName& /* NewName */);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray, int32 /* NodeIndex */, int32 /* VertexIndex */, int32 /* LiteralIndex */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetaSoundFrontendDocumentRenameVertex, FName /* OldName */, FName /* NewName */);

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetaSoundFrontendDocumentMutateMember, FGuid /* Member ID*/);
#endif // WITH_EDITOR

namespace Metasound::Frontend
{
	struct FDocumentMutatePageArgs
	{
		FGuid PageID;
	};

	struct FDocumentPresetStateChangedArgs
	{
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentPresetStateChanged, const FDocumentPresetStateChangedArgs& /* Args */);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentPageAdded, const FDocumentMutatePageArgs& /* Args */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentRemovingPage, const FDocumentMutatePageArgs& /* Args */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentPageSet, const FDocumentMutatePageArgs& /* Args */);

	struct FPageModifyDelegates
	{
		FOnDocumentPageAdded OnPageAdded;
		FOnDocumentRemovingPage OnRemovingPage;
		FOnDocumentPageSet OnPageSet;
	};

	struct FInterfaceModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateInterfaceArray OnInterfaceAdded;
		FOnMetaSoundFrontendDocumentMutateInterfaceArray OnRemovingInterface;

		FOnMetaSoundFrontendDocumentMutateArray OnInputAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingInput;
		FOnMetaSoundFrontendDocumentRenameVertex OnInputNameChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputDataTypeChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputDefaultChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputIsConstructorPinChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputInheritsDefaultChanged;

#if WITH_EDITOR
		FOnMetaSoundFrontendDocumentMutateArray OnInputDisplayNameChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputDescriptionChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputSortOrderIndexChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnInputIsAdvancedDisplayChanged;
#endif // WITH_EDITOR

		FOnMetaSoundFrontendDocumentMutateArray OnOutputAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnRemovingOutput;
		FOnMetaSoundFrontendDocumentRenameVertex OnOutputNameChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnOutputDataTypeChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnOutputIsConstructorPinChanged;
#if WITH_EDITOR
		FOnMetaSoundFrontendDocumentMutateArray OnOutputDisplayNameChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnOutputDescriptionChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnOutputSortOrderIndexChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnOutputIsAdvancedDisplayChanged;

		FOnMetaSoundFrontendDocumentMutateMember OnMemberMetadataSet;
		FOnMetaSoundFrontendDocumentMutateMember OnRemovingMemberMetadata;
#endif // WITH_EDITOR
	};

	struct FNodeModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateArray OnNodeAdded;
		FOnMetaSoundFrontendDocumentMutateArray OnNodeConfigurationUpdated;
		FOnMetaSoundFrontendDocumentRemoveSwappingArray OnRemoveSwappingNode;

		FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray OnNodeInputLiteralSet;
		FOnMetaSoundFrontendDocumentMutateNodeInputLiteralArray OnRemovingNodeInputLiteral;
	};

	struct FEdgeModifyDelegates
	{
		FOnMetaSoundFrontendDocumentMutateArray OnEdgeAdded;
		FOnMetaSoundFrontendDocumentRemoveSwappingArray OnRemoveSwappingEdge;
	};

	struct FGraphModifyDelegates
	{
		FEdgeModifyDelegates EdgeDelegates;
		FNodeModifyDelegates NodeDelegates;
	};

	struct FDocumentModifyDelegates : TSharedFromThis<FDocumentModifyDelegates>
	{
		UE_API FDocumentModifyDelegates();
		UE_API FDocumentModifyDelegates(const FMetasoundFrontendDocument& Document);

		~FDocumentModifyDelegates() = default;

		FOnMetaSoundFrontendDocumentMutateMetadata OnDocumentMetadataChanged;
		FOnMetaSoundFrontendDocumentMutateArray OnDependencyAdded;
		FOnMetaSoundFrontendDocumentRemoveSwappingArray OnRemoveSwappingDependency;
		FOnMetaSoundFrontendDocumentRenameClass OnRenamingDependencyClass;

		FOnDocumentPresetStateChanged OnPresetStateChanged;

		FPageModifyDelegates PageDelegates;
		FInterfaceModifyDelegates InterfaceDelegates;

	private:
		TSortedMap<FGuid, FGraphModifyDelegates> GraphDelegates;

	public:
		UE_API void AddPageDelegates(const FGuid& InPageID);
		UE_API void RemovePageDelegates(const FGuid& InPageID, bool bBroadcastNotify = true);

		UE_DEPRECATED(5.7, "Use FindGraphDelegatesChecked instead")
		UE_API FNodeModifyDelegates& FindNodeDelegatesChecked(const FGuid& InPageID);

		UE_DEPRECATED(5.7, "Use FindGraphDelegatesChecked instead")
		UE_API FEdgeModifyDelegates& FindEdgeDelegatesChecked(const FGuid& InPageID);

		UE_DEPRECATED(5.7, "Use IterateGraphDelegates instead")
		UE_API void IterateGraphEdgeDelegates(TFunctionRef<void(FEdgeModifyDelegates&)> Func);

		UE_DEPRECATED(5.7, "Use IterateGraphDelegates instead")
		UE_API void IterateGraphNodeDelegates(TFunctionRef<void(FNodeModifyDelegates&)> Func);

		UE_API FGraphModifyDelegates& FindGraphDelegatesChecked(const FGuid& InPageID);

		UE_API void IterateGraphDelegates(TFunctionRef<void(FGraphModifyDelegates&)> Func);
	};

	class IDocumentBuilderTransactionListener : public TSharedFromThis<IDocumentBuilderTransactionListener>
	{
	public:
		virtual ~IDocumentBuilderTransactionListener() = default;

		// Called when the builder is reloaded, at which point the document cache and delegates are refreshed
		virtual void OnBuilderReloaded(FDocumentModifyDelegates& OutDelegates) = 0;
	};
} // namespace Metasound::Frontend

#undef UE_API
