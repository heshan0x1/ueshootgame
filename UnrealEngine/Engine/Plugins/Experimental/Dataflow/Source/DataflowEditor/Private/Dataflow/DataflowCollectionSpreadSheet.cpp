// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowCollectionSpreadSheet.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowCollectionSpreadSheetWidget.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowSelection.h"
#include "Templates/EnableIf.h"

//#include "Dataflow/DataflowEdNode.h"


FDataflowCollectionSpreadSheet::FDataflowCollectionSpreadSheet(TObjectPtr<UDataflowBaseContent> InContent)
	: FDataflowNodeView(InContent)
{

}

void FDataflowCollectionSpreadSheet::SetSupportedOutputTypes()
{
	GetSupportedOutputTypes().Empty();

	GetSupportedOutputTypes().Add("FManagedArrayCollection");
}

void FDataflowCollectionSpreadSheet::UpdateViewData()
{
	if (CollectionSpreadSheet && CollectionSpreadSheet->GetCollectionTable())
	{
		CollectionSpreadSheet->GetCollectionTable()->GetCollectionInfoMap().Empty();

		if (GetSelectedNode())
		{
			if (GetSelectedNode()->IsBound())
			{
				if (TSharedPtr<FDataflowNode> DataflowNode = GetSelectedNode()->DataflowGraph->FindBaseNode(GetSelectedNode()->DataflowNodeGuid))
				{
					TArray<FDataflowOutput*> Outputs = DataflowNode->GetOutputs();
					if (const TObjectPtr<UDataflowBaseContent> Content = GetEditorContent())
					{
						if (TSharedPtr<UE::Dataflow::FEngineContext> Context = Content->GetDataflowContext())
						{
							for (FDataflowOutput* Output : Outputs)
							{
								if (Output->GetType() == "FManagedArrayCollection")
								{
									const FName Name = Output->GetName();
									const FManagedArrayCollection DefaultCollection;
									const FManagedArrayCollection& Value = Output->ReadValue(*Context, DefaultCollection);
									TSharedPtr<const FManagedArrayCollection> CopiedCollectionPtr = MakeShared<const FManagedArrayCollection>(Value);
									CollectionSpreadSheet->GetCollectionTable()->GetCollectionInfoMap().Add(Name.ToString(), { CopiedCollectionPtr });
								}
							}
						}
					}
				}
			}

			CollectionSpreadSheet->SetData(GetSelectedNode()->GetName());
		}
		else
		{
			CollectionSpreadSheet->SetData(FString());
		}

		CollectionSpreadSheet->RefreshWidget();
	}
}


void FDataflowCollectionSpreadSheet::SetCollectionSpreadSheet(TSharedPtr<SCollectionSpreadSheetWidget>& InCollectionSpreadSheet)
{
	ensure(!CollectionSpreadSheet);

	CollectionSpreadSheet = InCollectionSpreadSheet;

	if (CollectionSpreadSheet)
	{
		OnPinnedDownChangedDelegateHandle = CollectionSpreadSheet->GetOnPinnedDownChangedDelegate().AddRaw(this, &FDataflowCollectionSpreadSheet::OnPinnedDownChanged);
		OnRefreshLockedChangedDelegateHandle = CollectionSpreadSheet->GetOnRefreshLockedChangedDelegate().AddRaw(this, &FDataflowCollectionSpreadSheet::OnRefreshLockedChanged);
	}
}


FDataflowCollectionSpreadSheet::~FDataflowCollectionSpreadSheet()
{
	if (CollectionSpreadSheet)
	{
		CollectionSpreadSheet->GetOnPinnedDownChangedDelegate().Remove(OnPinnedDownChangedDelegateHandle);
		CollectionSpreadSheet->GetOnRefreshLockedChangedDelegate().Remove(OnRefreshLockedChangedDelegateHandle);
	}
}
