// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArrayNodes.h"
#include "Misc/EnumClassFlags.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	namespace MetasoundArrayNodesPrivate
	{
		FNodeClassMetadata CreateArrayNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface, int32 MajorVersion, int32 MinorVersion, bool bIsDeprecated)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName { FName("Array"), InOperatorName, InDataTypeName },
				MajorVersion, 
				MinorVersion,
				InDisplayName, 
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{ METASOUND_LOCTEXT("ArrayCategory", "Array") },
				{ METASOUND_LOCTEXT("MetasoundArrayKeyword", "Array") },
			};

			if (bIsDeprecated)
			{
				EnumAddFlags(Metadata.AccessFlags, ENodeClassAccessFlags::Deprecated);
			}

			return Metadata;
		}

		FVertexInterface CreateArrayNumInterface(const FName& InArrayDataTypeName)
		{
			using namespace ArrayNodeVertexNames;

			return FVertexInterface {
				FInputVertexInterface(
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputArray), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(InputArray))
				),
				FOutputVertexInterface(
					TOutputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputNum))
				)
			};
		}

		FVertexInterface CreateArrayGetInterface(const FName& InArrayDataTypeName, const FName& InDataTypeName)
		{
			using namespace ArrayNodeVertexNames;
			return FVertexInterface {
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTriggerGet)),
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputArray), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(InputArray)),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputIndex))
				),
				FOutputVertexInterface(
					FOutputDataVertex(METASOUND_GET_PARAM_NAME(OutputValue), InDataTypeName, METASOUND_GET_PARAM_METADATA(OutputValue), EVertexAccessType::Reference)
				)
			};
		}

		FVertexInterface CreateArraySetInterface(const FName& InArrayDataTypeName, const FName& InDataTypeName)
		{
			using namespace ArrayNodeVertexNames;
			return FVertexInterface{ 
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTriggerSet)),
					FInputDataVertex(InputInitialArrayName, InArrayDataTypeName, FDataVertexMetadata { InputInitialArrayTooltip, InputInitialArrayDisplayName }),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputIndex)),
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputValue), InDataTypeName, METASOUND_GET_PARAM_METADATA(InputValue))
				),
				FOutputVertexInterface(
					FOutputDataVertex(METASOUND_GET_PARAM_NAME(OutputArraySet), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(OutputArraySet), EVertexAccessType::Reference)

				)
			};
		}

		FVertexInterface CreateArrayConcatInterface(const FName& InArrayDataTypeName)
		{
			using namespace ArrayNodeVertexNames;

			return FVertexInterface{
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTriggerGet)),
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputLeftArray), InArrayDataTypeName,METASOUND_GET_PARAM_METADATA(InputLeftArray)),
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputRightArray), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(InputRightArray))
				),
				FOutputVertexInterface(
					FOutputDataVertex(METASOUND_GET_PARAM_NAME(OutputArrayConcat), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(OutputArrayConcat), EVertexAccessType::Reference)
				)
			};
		}

		FVertexInterface CreateArraySubsetInterface(const FName& InArrayDataTypeName)
		{
			using namespace ArrayNodeVertexNames;

			return FVertexInterface {
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTriggerGet)),
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputArray), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(InputArray)),

					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputStartIndex)),
					TInputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputEndIndex))

				),
				FOutputVertexInterface(
					FOutputDataVertex(METASOUND_GET_PARAM_NAME(OutputArraySubset), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(OutputArraySubset), EVertexAccessType::Reference)

				)
			};
		}

		FVertexInterface CreateArrayLastIndexInterface(const FName& InArrayDataTypeName)
		{
			using namespace ArrayNodeVertexNames;

			return FVertexInterface {
				FInputVertexInterface(
					FInputDataVertex(METASOUND_GET_PARAM_NAME(InputArray), InArrayDataTypeName, METASOUND_GET_PARAM_METADATA(InputArray))
				),
				FOutputVertexInterface(
					TOutputDataVertex<int32>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputLastIndex))
				)
			};
		}
	}
}

#undef LOCTEXT_NAMESPACE
