// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/STedsSearchBox.h"

#include "DataStorage/Features.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Framework/TypedElementQueryContext.h"

#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "TedsQueryNode.h"
#include "TedsRowFilterNode.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STedsSearchBox"

namespace UE::Editor::DataStorage
{
	void STedsSearchBox::Construct(const FArguments& InArgs)
	{
		using namespace UE::Editor::DataStorage::Queries;

		ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(UE::Editor::DataStorage::StorageFeatureName);

		if(!ensureMsgf(Storage, TEXT("Cannot create a TEDS search widget before TEDS is initialized")))
		{
			ChildSlot
			[
				SNew(STextBlock)
					.Text(LOCTEXT("TedsSearchErrorText", "No valid editor data storage available."))
			];
			return;
		}

		if(InArgs._OutSearchNode && InArgs._InSearchableRowNode)
		{
			OutSearchNode = MakeShared<QueryStack::FRowFilterNode>(Storage, InArgs._InSearchableRowNode,
				[this](TQueryContext<SingleRowInfo> Context, const FTypedElementLabelColumn& LabelColumn)
				{
					if(SearchText.IsEmpty())
					{
						return true;
					}

					TArray<FString> SearchTokens;
					SearchText.ToString().ParseIntoArrayWS(SearchTokens);
					const FString Label = LabelColumn.Label;

					for (const FString& SearchToken : SearchTokens)
					{
						if (Label.IsEmpty() || !Label.Contains(SearchToken, ESearchCase::Type::IgnoreCase))
						{
							return false;
						}
					}
					return true;
				});
				
			*InArgs._OutSearchNode = OutSearchNode;
		}
		
		ChildSlot
		[
			SNew(SSearchBox)
				.OnTextChanged(this, &STedsSearchBox::OnTextChanged)
		];
	}

	void STedsSearchBox::OnTextChanged(const FText& InSearchText)
	{
		SearchText = InSearchText;
		if(OutSearchNode)
		{
			OutSearchNode->ForceRefresh();
		}
		
	}
	
} // namespace UE::Editor::DataStorage

#undef LOCTEXT_NAMESPACE //"STedsSearchBox"