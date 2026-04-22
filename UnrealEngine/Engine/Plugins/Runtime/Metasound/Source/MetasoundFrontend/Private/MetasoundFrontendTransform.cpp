// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendTransform.h"

#include "Algo/Transform.h"
#include "Interfaces/MetasoundFrontendInterface.h"
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "NodeTemplates/MetasoundFrontendNodeTemplateInput.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendDocumentController.h"
#include "MetasoundFrontendDocumentIdGenerator.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLog.h"
#include "MetasoundTrace.h"
#include "Misc/App.h"

namespace Metasound
{
	namespace Frontend
	{
#define	METASOUND_VERSIONING_LOG(Verbosity, Format, ...) if (DocumentTransform::bVersioningLoggingEnabled) { UE_LOG(LogMetaSound, Verbosity, Format, ##__VA_ARGS__); }

		namespace DocumentTransform
		{
			bool bVersioningLoggingEnabled = true;

			void LogAutoUpdateWarning(const FString& LogMessage)
			{
				// These should eventually move back to warning on cook 
				// but are temporarily downgraded to prevent 
				// warnings on things like unused test content from 
				// blocking code checkins 
				if (IsRunningCookCommandlet())
				{
					METASOUND_VERSIONING_LOG(Display, TEXT("%s"), *LogMessage);
				}
				else
				{
					METASOUND_VERSIONING_LOG(Warning, TEXT("%s"), *LogMessage);
				}
			}

#if WITH_EDITOR
			FGetNodeDisplayNameProjection NodeDisplayNameProjection = [] (const FNodeHandle&) { return FText(); };

			bool GetVersioningLoggingEnabled()
			{
				return bVersioningLoggingEnabled;
			}

			void SetVersioningLoggingEnabled(bool bEnabled)
			{
				bVersioningLoggingEnabled = bEnabled;
			}

			void RegisterNodeDisplayNameProjection(FGetNodeDisplayNameProjection&& InNameProjection)
			{
				NodeDisplayNameProjection = MoveTemp(InNameProjection);
			}

			FGetNodeDisplayNameProjectionRef GetNodeDisplayNameProjection()
			{
				return NodeDisplayNameProjection;
			}
#endif // WITH_EDITOR
		} // namespace DocumentTransform

		bool IDocumentTransform::Transform(FMetasoundFrontendDocument& InOutDocument) const
		{
			FDocumentAccessPtr DocAccessPtr = MakeAccessPtr<FDocumentAccessPtr>(InOutDocument.AccessPoint, InOutDocument);
			return Transform(FDocumentController::CreateDocumentHandle(DocAccessPtr));
		}

		bool INodeTransform::Transform(const FGuid& InNodeID, FMetaSoundFrontendDocumentBuilder& OutBuilder) const
		{
			return false;
		}

		FModifyRootGraphInterfaces::FModifyRootGraphInterfaces(const TArray<FMetasoundFrontendInterface>& InInterfacesToRemove, const TArray<FMetasoundFrontendInterface>& InInterfacesToAdd)
			: InterfacesToRemove(InInterfacesToRemove)
			, InterfacesToAdd(InInterfacesToAdd)
		{
			Init();
		}

		FModifyRootGraphInterfaces::FModifyRootGraphInterfaces(const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToRemove, const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToAdd)
		{
			Algo::Transform(InInterfaceVersionsToRemove, InterfacesToRemove, [](const FMetasoundFrontendVersion& Version)
			{
				FMetasoundFrontendInterface Interface;
				const bool bFromInterfaceFound = IInterfaceRegistry::Get().FindInterface(GetInterfaceRegistryKey(Version), Interface);
				if (!ensureAlways(bFromInterfaceFound))
				{
					METASOUND_VERSIONING_LOG(Error, TEXT("Failed to find interface '%s' to remove"), *Version.ToString());
				}
				return Interface;
			});

			Algo::Transform(InInterfaceVersionsToAdd, InterfacesToAdd, [](const FMetasoundFrontendVersion& Version)
			{
				FMetasoundFrontendInterface Interface;
				const bool bToInterfaceFound = IInterfaceRegistry::Get().FindInterface(GetInterfaceRegistryKey(Version), Interface);
				if (!ensureAlways(bToInterfaceFound))
				{
					METASOUND_VERSIONING_LOG(Error, TEXT("Failed to find interface '%s' to add"), *Version.ToString());
				}
				return Interface;
			});

			Init();
		}

#if WITH_EDITOR
		void FModifyRootGraphInterfaces::SetDefaultNodeLocations(bool bInSetDefaultNodeLocations)
		{
			bSetDefaultNodeLocations = bInSetDefaultNodeLocations;
		}
#endif // WITH_EDITOR

		void FModifyRootGraphInterfaces::SetNamePairingFunction(const TFunction<bool(FName, FName)>& InNamePairingFunction)
		{
			// Reinit required to rebuild list of pairs
			Init(&InNamePairingFunction);
		}

		bool FModifyRootGraphInterfaces::AddMissingVertices(FGraphHandle GraphHandle) const
		{
			for (const FInputData& InputData : InputsToAdd)
			{
				const FMetasoundFrontendClassInput& InputToAdd = InputData.Input;
				GraphHandle->AddInputVertex(InputToAdd);
			}

			for (const FOutputData& OutputData : OutputsToAdd)
			{
				const FMetasoundFrontendClassOutput& OutputToAdd = OutputData.Output;
				GraphHandle->AddOutputVertex(OutputToAdd);
			}

			return !InputsToAdd.IsEmpty() || !OutputsToAdd.IsEmpty();
		}

		void FModifyRootGraphInterfaces::Init(const TFunction<bool(FName, FName)>* InNamePairingFunction)
		{
			InputsToRemove.Reset();
			InputsToAdd.Reset();
			OutputsToRemove.Reset();
			OutputsToAdd.Reset();
			PairedInputs.Reset();
			PairedOutputs.Reset();

			for (const FMetasoundFrontendInterface& FromInterface : InterfacesToRemove)
			{
				InputsToRemove.Append(FromInterface.Inputs);
				OutputsToRemove.Append(FromInterface.Outputs);
			}

			// This function combines all the inputs of all interfaces into one input list and ptrs to their originating interfaces.
			// The interface ptr will be used to query the interface for required validations on inputs. Interfaces define required inputs (and possibly other validation requirements).
			for (const FMetasoundFrontendInterface& ToInterface : InterfacesToAdd)
			{
				TArray<FInputData> NewInputDataArray;
				for (const FMetasoundFrontendClassInput& Input : ToInterface.Inputs)
				{
					FInputData NewData;
					NewData.Input = Input;
					NewData.InputInterface = &ToInterface;
					NewInputDataArray.Add(NewData);
				}

				InputsToAdd.Append(NewInputDataArray);

				TArray<FOutputData> NewOutputDataArray;
				for (const FMetasoundFrontendClassOutput& Output : ToInterface.Outputs)
				{
					FOutputData NewData;
					NewData.Output = Output;
					NewData.OutputInterface = &ToInterface;
					NewOutputDataArray.Add(NewData);
				}

				OutputsToAdd.Append(NewOutputDataArray);
			}

			// Iterate in reverse to allow removal from `InputsToAdd`
			for (int32 AddIndex = InputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
			{
				const FMetasoundFrontendClassVertex& VertexToAdd = InputsToAdd[AddIndex].Input;

				const int32 RemoveIndex = InputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
				{
					if (VertexToAdd.TypeName != VertexToRemove.TypeName)
					{
						return false;
					}

					if (InNamePairingFunction && *InNamePairingFunction)
					{
						return (*InNamePairingFunction)(VertexToAdd.Name, VertexToRemove.Name);
					}

					FName ParamA;
					FName ParamB;
					FName Namespace;
					VertexToAdd.SplitName(Namespace, ParamA);
					VertexToRemove.SplitName(Namespace, ParamB);

					return ParamA == ParamB;
				});

				if (INDEX_NONE != RemoveIndex)
				{
					PairedInputs.Add(FVertexPair{InputsToRemove[RemoveIndex], InputsToAdd[AddIndex].Input});
					InputsToRemove.RemoveAtSwap(RemoveIndex);
					InputsToAdd.RemoveAtSwap(AddIndex);
				}
			}

			// Iterate in reverse to allow removal from `OutputsToAdd`
			for (int32 AddIndex = OutputsToAdd.Num() - 1; AddIndex >= 0; AddIndex--)
			{
				const FMetasoundFrontendClassVertex& VertexToAdd = OutputsToAdd[AddIndex].Output;

				const int32 RemoveIndex = OutputsToRemove.IndexOfByPredicate([&](const FMetasoundFrontendClassVertex& VertexToRemove)
				{
					if (VertexToAdd.TypeName != VertexToRemove.TypeName)
					{
						return false;
					}

					if (InNamePairingFunction && *InNamePairingFunction)
					{
						return (*InNamePairingFunction)(VertexToAdd.Name, VertexToRemove.Name);
					}

					FName ParamA;
					FName ParamB;
					FName Namespace;
					VertexToAdd.SplitName(Namespace, ParamA);
					VertexToRemove.SplitName(Namespace, ParamB);

					return ParamA == ParamB;
				});

				if (INDEX_NONE != RemoveIndex)
				{
					PairedOutputs.Add(FVertexPair { OutputsToRemove[RemoveIndex], OutputsToAdd[AddIndex].Output });
					OutputsToRemove.RemoveAtSwap(RemoveIndex);
					OutputsToAdd.RemoveAtSwap(AddIndex);
				}
			}
		}

		bool FModifyRootGraphInterfaces::RemoveUnsupportedVertices(FGraphHandle GraphHandle) const
		{
			// Remove unsupported inputs
			for (const FMetasoundFrontendClassVertex& InputToRemove : InputsToRemove)
			{
				if (const FMetasoundFrontendClassInput* ClassInput = GraphHandle->FindClassInputWithName(InputToRemove.Name).Get())
				{
					if (FMetasoundFrontendClassInput::IsFunctionalEquivalent(*ClassInput, InputToRemove))
					{
						GraphHandle->RemoveInputVertex(InputToRemove.Name);
					}
				}
			}

			// Remove unsupported outputs
			for (const FMetasoundFrontendClassVertex& OutputToRemove : OutputsToRemove)
			{
				if (const FMetasoundFrontendClassOutput* ClassOutput = GraphHandle->FindClassOutputWithName(OutputToRemove.Name).Get())
				{
					if (FMetasoundFrontendClassOutput::IsFunctionalEquivalent(*ClassOutput, OutputToRemove))
					{
						GraphHandle->RemoveOutputVertex(OutputToRemove.Name);
					}
				}
			}

			return !InputsToRemove.IsEmpty() || !OutputsToRemove.IsEmpty();
		}

		bool FModifyRootGraphInterfaces::SwapPairedVertices(FGraphHandle GraphHandle) const
		{
			for (const FVertexPair& InputPair : PairedInputs)
			{
				const FMetasoundFrontendClassVertex& OriginalVertex = InputPair.Get<0>();
				FMetasoundFrontendClassInput NewVertex = InputPair.Get<1>();

				// Cache off node locations and connections to push to new node
				TMap<FGuid, FVector2D> Locations;
				TArray<FInputHandle> ConnectedInputs;
				if (const FMetasoundFrontendClassInput* ClassInput = GraphHandle->FindClassInputWithName(OriginalVertex.Name).Get())
				{
					if (FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassInput, OriginalVertex))
					{
						const FMetasoundFrontendLiteral& DefaultLiteral = ClassInput->FindConstDefaultChecked(Frontend::DefaultPageID);
						NewVertex.FindDefaultChecked(Frontend::DefaultPageID) = DefaultLiteral;
						NewVertex.NodeID = ClassInput->NodeID;
						FNodeHandle OriginalInputNode = GraphHandle->GetInputNodeWithName(OriginalVertex.Name);

#if WITH_EDITOR
						Locations = OriginalInputNode->GetNodeStyle().Display.Locations;
#endif // WITH_EDITOR

						FOutputHandle OriginalInputNodeOutput = OriginalInputNode->GetOutputWithVertexName(OriginalVertex.Name);
						ConnectedInputs = OriginalInputNodeOutput->GetConnectedInputs();
						GraphHandle->RemoveInputVertex(OriginalVertex.Name);
					}
				}

				FNodeHandle NewInputNode = GraphHandle->AddInputVertex(NewVertex);

#if WITH_EDITOR
				// Copy prior node locations
				if (!Locations.IsEmpty())
				{
					FMetasoundFrontendNodeStyle Style = NewInputNode->GetNodeStyle();
					Style.Display.Locations = Locations;
					NewInputNode->SetNodeStyle(Style);
				}
#endif // WITH_EDITOR

				// Copy prior node connections
				FOutputHandle OutputHandle = NewInputNode->GetOutputWithVertexName(NewVertex.Name);
				for (FInputHandle& ConnectedInput : ConnectedInputs)
				{
					OutputHandle->Connect(*ConnectedInput);
				}
			}

			// Swap paired outputs.
			for (const FVertexPair& OutputPair : PairedOutputs)
			{
				const FMetasoundFrontendClassVertex& OriginalVertex = OutputPair.Get<0>();
				FMetasoundFrontendClassVertex NewVertex = OutputPair.Get<1>();

#if WITH_EDITOR
				// Cache off node locations to push to new node
				// Default add output node to origin.
				TMap<FGuid, FVector2D> Locations;
				Locations.Add(FGuid(), FVector2D { 0.f, 0.f });
#endif // WITH_EDITOR

				FOutputHandle ConnectedOutput = IOutputController::GetInvalidHandle();
				if (const FMetasoundFrontendClassOutput* ClassOutput = GraphHandle->FindClassOutputWithName(OriginalVertex.Name).Get())
				{
					if (FMetasoundFrontendVertex::IsFunctionalEquivalent(*ClassOutput, OriginalVertex))
					{
						NewVertex.NodeID = ClassOutput->NodeID;

#if WITH_EDITOR
						// Interface members do not serialize text to avoid localization
						// mismatches between assets and interfaces defined in code.
						NewVertex.Metadata.SetSerializeText(false);
#endif // WITH_EDITOR

						FNodeHandle OriginalOutputNode = GraphHandle->GetOutputNodeWithName(OriginalVertex.Name);

#if WITH_EDITOR
						Locations = OriginalOutputNode->GetNodeStyle().Display.Locations;
#endif // WITH_EDITOR

						FInputHandle Input = OriginalOutputNode->GetInputWithVertexName(OriginalVertex.Name);
						ConnectedOutput = Input->GetConnectedOutput();
						GraphHandle->RemoveOutputVertex(OriginalVertex.Name);
					}
				}

				FNodeHandle NewOutputNode = GraphHandle->AddOutputVertex(NewVertex);

#if WITH_EDITOR
				if (Locations.Num() > 0)
				{
					FMetasoundFrontendNodeStyle Style = NewOutputNode->GetNodeStyle();
					Style.Display.Locations = Locations;
					NewOutputNode->SetNodeStyle(Style);
				}
#endif // WITH_EDITOR

				// Copy prior node connections
				FInputHandle InputHandle = NewOutputNode->GetInputWithVertexName(NewVertex.Name);
				ConnectedOutput->Connect(*InputHandle);
			}

			return !PairedInputs.IsEmpty() || !PairedOutputs.IsEmpty();
		}

		bool FModifyRootGraphInterfaces::Transform(FDocumentHandle InDocument) const
		{
			bool bDidEdit = false;

			FGraphHandle GraphHandle = InDocument->GetRootGraph();
			if (ensure(GraphHandle->IsValid()))
			{
				bDidEdit |= UpdateInterfacesInternal(InDocument);

				const bool bAddedVertices = AddMissingVertices(GraphHandle);
				bDidEdit |= bAddedVertices;

				bDidEdit |= SwapPairedVertices(GraphHandle);
				bDidEdit |= RemoveUnsupportedVertices(GraphHandle);

#if WITH_EDITORONLY_DATA
				if (bAddedVertices && bSetDefaultNodeLocations)
				{
					UpdateAddedVertexNodePositions(GraphHandle);
				}
#endif // WITH_EDITORONLY_DATA
			}

			return bDidEdit;
		}

		bool FModifyRootGraphInterfaces::Transform(FMetasoundFrontendDocument& InOutDocument) const
		{
			FDocumentAccessPtr DocAccessPtr = MakeAccessPtr<FDocumentAccessPtr>(InOutDocument.AccessPoint, InOutDocument);
			return Transform(FDocumentController::CreateDocumentHandle(DocAccessPtr));
		}

		bool FModifyRootGraphInterfaces::UpdateInterfacesInternal(FDocumentHandle DocumentHandle) const
		{
			for (const FMetasoundFrontendInterface& Interface : InterfacesToRemove)
			{
				DocumentHandle->RemoveInterfaceVersion(Interface.Metadata.Version);
			}

			for (const FMetasoundFrontendInterface& Interface : InterfacesToAdd)
			{
				DocumentHandle->AddInterfaceVersion(Interface.Metadata.Version);
			}

			return !InterfacesToRemove.IsEmpty() || !InterfacesToAdd.IsEmpty();
		}

#if WITH_EDITORONLY_DATA
		void FModifyRootGraphInterfaces::UpdateAddedVertexNodePositions(FGraphHandle GraphHandle) const
		{
			auto SortAndPlaceMemberNodes = [&GraphHandle](EMetasoundFrontendClassType ClassType, TSet<FName>& AddedNames, TFunctionRef<int32(const FVertexName&)> InGetSortOrder)
			{
				// Add graph member nodes by sort order
				TSortedMap<int32, FNodeHandle> SortOrderToName;
				GraphHandle->IterateNodes([&GraphHandle, &SortOrderToName, &InGetSortOrder](FNodeHandle NodeHandle)
				{
					const int32 Index = InGetSortOrder(NodeHandle->GetNodeName());
					SortOrderToName.Add(Index, NodeHandle);
				}, ClassType);

				// Prime the first location as an offset prior to an existing location (as provided by a swapped member)
				//  to avoid placing away from user's active area if possible.
				FVector2D NextLocation = { 0.0f, 0.0f };
				{
					int32 NumBeforeDefined = 1;
					for (const TPair<int32, FNodeHandle>& Pair : SortOrderToName) //-V1078
					{
						const FConstNodeHandle& NodeHandle = Pair.Value;
						const FName NodeName = NodeHandle->GetNodeName();
						if (AddedNames.Contains(NodeName))
						{
							NumBeforeDefined++;
						}
						else
						{
							const TMap<FGuid, FVector2D>& Locations = NodeHandle->GetNodeStyle().Display.Locations;
							if (!Locations.IsEmpty())
							{
								auto It = Locations.CreateConstIterator();
								const TPair<FGuid, FVector2D>& Location = *It;
								NextLocation = Location.Value - (NumBeforeDefined * DisplayStyle::NodeLayout::DefaultOffsetY);
								break;
							}
						}
					}
				}

				// Iterate through sorted map in sequence, slotting in new locations after existing swapped nodes with predefined locations.
				for (TPair<int32, FNodeHandle>& Pair : SortOrderToName) //-V1078
				{
					FNodeHandle& NodeHandle = Pair.Value;
					const FName NodeName = NodeHandle->GetNodeName();
					if (AddedNames.Contains(NodeName))
					{
						FMetasoundFrontendNodeStyle NewStyle = NodeHandle->GetNodeStyle();
						NewStyle.Display.Locations.Add(FGuid(), NextLocation);
						NextLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
						NodeHandle->SetNodeStyle(NewStyle);
					}
					else
					{
						for (const TPair<FGuid, FVector2D>& Location : NodeHandle->GetNodeStyle().Display.Locations)
						{
							NextLocation = Location.Value + DisplayStyle::NodeLayout::DefaultOffsetY;
						}
					}
				}
			};

			// Sort/Place Inputs
			{
				TSet<FName> AddedNames;
				Algo::Transform(InputsToAdd, AddedNames, [](const FInputData& InputData) { return InputData.Input.Name; });
				auto GetInputSortOrder = [&GraphHandle](const FVertexName& InVertexName) { return GraphHandle->GetSortOrderIndexForInput(InVertexName); };
				SortAndPlaceMemberNodes(EMetasoundFrontendClassType::Input, AddedNames, GetInputSortOrder);
			}

			// Sort/Place Outputs
			{
				TSet<FName> AddedNames;
				Algo::Transform(OutputsToAdd, AddedNames, [](const FOutputData& OutputData) { return OutputData.Output.Name; });
				auto GetOutputSortOrder = [&GraphHandle](const FVertexName& InVertexName) { return GraphHandle->GetSortOrderIndexForOutput(InVertexName); };
				SortAndPlaceMemberNodes(EMetasoundFrontendClassType::Output, AddedNames, GetOutputSortOrder);
			}
		}

		bool FAutoUpdateRootGraph::Transform(FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FAutoUpdateRootGraph::Transform);
			bool bDidEdit = false;

			const bool bIsPreset = InOutBuilderToTransform.IsPreset();
			const FMetasoundFrontendGraphClass& RootGraph = InOutBuilderToTransform.GetConstDocumentChecked().RootGraph;
			
			// If preset, rebuild root graph 
			if (bIsPreset)
			{
				FMetasoundAssetBase* PresetReferencedMetaSoundAsset = InOutBuilderToTransform.GetReferencedPresetAsset();
				if (!PresetReferencedMetaSoundAsset)
				{
					METASOUND_VERSIONING_LOG(Error, TEXT("Auto-Updating preset '%s' failed: Referenced class missing."), *DebugAssetPath);
					return false;
				}
				TScriptInterface<IMetaSoundDocumentInterface> RefDocInterface = PresetReferencedMetaSoundAsset->GetOwningAsset();
				const FMetaSoundFrontendDocumentBuilder& ReferenceBuilder = IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(RefDocInterface);

				bDidEdit |= FRebuildPresetRootGraph(ReferenceBuilder).Transform(InOutBuilderToTransform);
				if (bDidEdit)
				{
					FMetasoundFrontendClassMetadata PresetMetadata = InOutBuilderToTransform.GetConstDocumentChecked().RootGraph.Metadata;
					PresetMetadata.SetType(EMetasoundFrontendClassType::External);
					const FNodeRegistryKey RegistryKey(PresetMetadata);
					FMetasoundAssetBase* PresetMetaSoundAsset = IMetaSoundAssetManager::GetChecked().TryLoadAssetFromKey(RegistryKey);
					if (ensure(PresetMetaSoundAsset))
					{
						TScriptInterface<IMetaSoundDocumentInterface> PresetInterface = PresetMetaSoundAsset->GetOwningAsset();
						check(PresetInterface);
						PresetInterface->ConformObjectToDocument();
					}

					InOutBuilderToTransform.RemoveUnusedDependencies();
					InOutBuilderToTransform.SynchronizeDependencyMetadata();
				}
			}
			// Non preset
			else
			{
				// Update external node dependencies until there are no more updates
				// (A node could need a minor version update, then an update transform, 
				// and then updated to another minor version which would require multiple passes)
				bool bUpdated = true;
				while (bUpdated)
				{
					bUpdated = false;
					bUpdated |= UpdateExternalDependencies(InOutBuilderToTransform);
					bDidEdit |= bUpdated;
				}
			}
				
			return bDidEdit;
		}

		bool FAutoUpdateRootGraph::UpdateExternalDependencies(FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform)
		{
			bool bDidEdit = false;
			// <Old dependency, new dependency>
			using FDependencyPair = TPair<FNodeClassRegistryKey, FNodeClassRegistryKey>;
			TSet<FDependencyPair> DependenciesToUpdate;
			TSet<FNodeClassRegistryKey> DependenciesToTransform;
			
			// Collect updates 
			const FMetasoundFrontendGraphClass& RootGraph = InOutBuilderToTransform.GetConstDocumentChecked().RootGraph;
			RootGraph.IterateGraphPages([&](const FMetasoundFrontendGraph& Graph)
			{
				const FGuid PageID = Graph.PageID;
				InOutBuilderToTransform.IterateNodesByClassType([&](const FMetasoundFrontendClass& Class, const FMetasoundFrontendNode& Node)
				{
					const FNodeClassRegistryKey& NodeClassKey = FNodeClassRegistryKey(Class.Metadata);

					const bool bHasUpdated = UpdatedClasses.Contains(Class.ID);
					const EAutoUpdateEligibility AutoUpdateReason = InOutBuilderToTransform.CanAutoUpdate(Node.GetID(), &PageID);
					if (bHasUpdated || AutoUpdateReason == EAutoUpdateEligibility::Ineligible)
					{
						return;
					}

					UpdatedClasses.Add(Class.ID);

					// Check if a updated minor version exists.
					FMetasoundFrontendClass ClassWithHighestMinorVersion;
					const bool bFoundClassInSearchEngine = Frontend::ISearchEngine::Get().FindClassWithHighestMinorVersion(NodeClassKey.ClassName, NodeClassKey.Version.Major, ClassWithHighestMinorVersion);

					if (bFoundClassInSearchEngine && (ClassWithHighestMinorVersion.Metadata.GetVersion() > NodeClassKey.Version))
					{
						const FMetasoundFrontendVersionNumber UpdateVersion = ClassWithHighestMinorVersion.Metadata.GetVersion();

						METASOUND_VERSIONING_LOG(Display, TEXT("Auto-Updating '%s' node class '%s': Newer version '%s' found."), *DebugAssetPath, *NodeClassKey.ClassName.ToString(), *UpdateVersion.ToString());
						DependenciesToUpdate.Add(FDependencyPair(NodeClassKey, FNodeClassRegistryKey(ClassWithHighestMinorVersion.Metadata)));
						bDidEdit |= true;
						
					}
					else if (AutoUpdateReason == EAutoUpdateEligibility::Eligible_InterfaceChange)
					{
						METASOUND_VERSIONING_LOG(Display, TEXT("Auto-Updating '%s' node class '%s (%s)': Interface change detected."), *DebugAssetPath, *NodeClassKey.ClassName.ToString(), *NodeClassKey.Version.ToString());
						DependenciesToUpdate.Add(FDependencyPair(NodeClassKey, NodeClassKey));
						bDidEdit |= true;
					}
					// Order is intentional; node must be updated to highest minor version before node update transform can be applied
					else if (AutoUpdateReason == EAutoUpdateEligibility::Eligible_NodeUpdateTransform)
					{
						METASOUND_VERSIONING_LOG(Display, TEXT("Auto-Updating '%s' node class '%s (%s)': Node update transform found."), *DebugAssetPath, *NodeClassKey.ClassName.ToString(), *NodeClassKey.Version.ToString());
						DependenciesToTransform.Add(NodeClassKey);
						bDidEdit |= true;
					}
					else
					{
						DependenciesToUpdate.Add(FDependencyPair(NodeClassKey, NodeClassKey));
					}
				}, EMetasoundFrontendClassType::External, &PageID);
			});
			
			// Apply dependency updates and log disconnections
			for (const FDependencyPair& DependencyPair : DependenciesToUpdate)
			{
				using FVertexNameAndType = FMetaSoundFrontendDocumentBuilder::FVertexNameAndType;
				TArray<FVertexNameAndType> DisconnectedInputs;
				TArray<FVertexNameAndType> DisconnectedOutputs;
				InOutBuilderToTransform.ReplaceDependency(DependencyPair.Key, DependencyPair.Value, &DisconnectedInputs, &DisconnectedOutputs);

				// Log warnings for any disconnections
				if (bLogWarningOnDroppedConnection)
				{
					if ((DisconnectedInputs.Num() > 0) || (DisconnectedOutputs.Num() > 0))
					{
						const FString NodeClassName = DependencyPair.Key.ClassName.ToString();
						const FString NewClassVersion = DependencyPair.Value.Version.ToString();

						for (const FVertexNameAndType& InputPin : DisconnectedInputs)
						{
							DocumentTransform::LogAutoUpdateWarning(FString::Printf(TEXT("Auto-Updating '%s' node class '%s (%s)': Previously connected input '%s' with data type '%s' no longer exists."), *DebugAssetPath, *NodeClassName, *NewClassVersion, *InputPin.Get<0>().ToString(), *InputPin.Get<1>().ToString()));
						}

						for (const FVertexNameAndType& OutputPin : DisconnectedOutputs)
						{
							DocumentTransform::LogAutoUpdateWarning(FString::Printf(TEXT("Auto-Updating '%s' node class '%s (%s)': Previously connected output '%s' with data type '%s' no longer exists."), *DebugAssetPath, *NodeClassName, *NewClassVersion, *OutputPin.Get<0>().ToString(), *OutputPin.Get<1>().ToString()));
						}
					}
				}
			}

			// Apply dependency transforms 
			for (const FNodeClassRegistryKey& NodeClassKey : DependenciesToTransform)
			{
				InOutBuilderToTransform.ApplyDependencyUpdateTransform(NodeClassKey);
			}

			InOutBuilderToTransform.RemoveUnusedDependencies();
			InOutBuilderToTransform.SynchronizeDependencyMetadata();

			return bDidEdit;
		}

		bool FAutoUpdateRootGraph::Transform(FDocumentHandle InDocument)
		{
			return false;
		}
#endif // WITH_EDITORONLY_DATA

		bool FRebuildPresetRootGraph::Transform(FDocumentHandle InDocument) const
		{
			return false;
		}
	
		FRebuildPresetRootGraph::FRebuildPresetRootGraph(const FMetasoundFrontendDocument& InReferencedDocument)
		{
		}

		bool FRebuildPresetRootGraph::Transform(FMetasoundFrontendDocument& InDocument) const
		{
			return false;
		}

		bool FRebuildPresetRootGraph::Transform(FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform) const
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::Frontend::FRebuildPresetRootGraph::Transform);
			
			// Callers of this transform should check that the graph is supposed to
			// be managed externally and the parent builder is valid before calling this transform. If a scenario
			// arises where this transform is used outside of AutoUpdate, then this
			// early exist should be removed as it's mostly here to protect against
			// accidental manipulation of metasound graphs.
			if (!InOutBuilderToTransform.IsPreset() || !ParentBuilder)
			{
				return false;
			}
	
			const FMetasoundFrontendDocument& DocumentToTransform = InOutBuilderToTransform.GetConstDocumentChecked();
			const FMetasoundFrontendDocument& ParentDocument = ParentBuilder->GetConstDocumentChecked();
			
			// Determine the inputs and outputs needed in the wrapping graph. Also
			// cache any exiting literals that have been set on the wrapping graph.
			TSet<FName> InputsInheritingDefault;
			TArray<FMetasoundFrontendClassInput> ClassInputs = GenerateRequiredClassInputs(InOutBuilderToTransform, InputsInheritingDefault);
			TArray<FMetasoundFrontendClassOutput> ClassOutputs = GenerateRequiredClassOutputs(InOutBuilderToTransform);

#if WITH_EDITORONLY_DATA
			// Cache off member metadata so it be can be readded if necessary after the graph is cleared 
			FMemberIDToMetadataMap CachedMemberMetadata;
			CacheMemberMetadata(InOutBuilderToTransform, InputsInheritingDefault, CachedMemberMetadata);
#endif // WITH_EDITORONLY_DATA

			FGuid PresetNodeID;
			InOutBuilderToTransform.IterateNodesByClassType([&](const FMetasoundFrontendClass&, const FMetasoundFrontendNode& Node)
			{
				PresetNodeID = Node.GetID();
			}, EMetasoundFrontendClassType::External);

			if (!PresetNodeID.IsValid())
			{
				// This ID was originally being set to FGuid::NewGuid. 
				// If you were reliant on that ID, please resave the asset so it is serialized with a valid ID
				PresetNodeID = DocumentToTransform.RootGraph.ID;
			}

			// Clear the root graph so it can be rebuilt.
			TSharedRef<FDocumentModifyDelegates> ModifyDelegates = MakeShared<FDocumentModifyDelegates>(InOutBuilderToTransform.GetDocumentDelegates());
			InOutBuilderToTransform.ClearDocument(ModifyDelegates);

			InOutBuilderToTransform.SetPresetFlags();

			// Ensure preset interfaces match those found in referenced graph.  Referenced graph is assumed to be
			// well-formed (i.e. all inputs/outputs/environment variables declared by interfaces are present, and
			// of proper name & data type). Pass in parent builder to copy member ids so added member ids can be consistent
			const TSet<FMetasoundFrontendVersion>& RefInterfaceVersions = ParentDocument.Interfaces;
			for (const FMetasoundFrontendVersion& Version : RefInterfaceVersions)
			{
				InOutBuilderToTransform.AddInterface(Version.Name, /*bAddUserModifiableInterfaceOnly = */false, ParentBuilder);
			}

			// Add referenced node
			const FMetasoundFrontendClassMetadata& ParentClassMetadata = ParentDocument.RootGraph.Metadata;
			const FMetasoundFrontendNode* ReferencedNode = InOutBuilderToTransform.AddNodeByClassName(ParentClassMetadata.GetClassName(), ParentClassMetadata.GetVersion().Major, PresetNodeID);
			check(ReferencedNode);
			
#if WITH_EDITOR
			// Set node location, offset to be to the right of input nodes
			const FGuid EdNodeGuid = FGuid::NewGuid(); // EdNodes are now never serialized and are transient, so just assign here
			InOutBuilderToTransform.SetNodeLocation(PresetNodeID, DisplayStyle::NodeLayout::DefaultOffsetX, &EdNodeGuid);
#endif // WITH_EDITOR

			// Connect parent graph to referenced graph
			InOutBuilderToTransform.SetGraphInputsInheritingDefault(MoveTemp(InputsInheritingDefault));
			AddAndConnectInputs(ClassInputs, InOutBuilderToTransform, PresetNodeID);
			AddAndConnectOutputs(ClassOutputs, InOutBuilderToTransform, PresetNodeID);

#if WITH_EDITORONLY_DATA
			AddMemberMetadata(CachedMemberMetadata, InOutBuilderToTransform);
#endif // WITH_EDITORONLY_DATA

			return true;
		}

#if WITH_EDITORONLY_DATA
		void FRebuildPresetRootGraph::AddMemberMetadata(const FMemberIDToMetadataMap& InCachedMemberMetadata, FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform) const
		{
			// Add member metadata if a member with the corresponding node ID exists in the preset graph
			if (!InCachedMemberMetadata.IsEmpty())
			{
				for (const TPair<FGuid, TObjectPtr<UMetaSoundFrontendMemberMetadata>>& MemberMetadataPair : InCachedMemberMetadata)
				{
					if (InOutBuilderToTransform.FindNode(MemberMetadataPair.Key))
					{
						InOutBuilderToTransform.SetMemberMetadata(*MemberMetadataPair.Value);
					}
				}
			}
		}
		
		void FRebuildPresetRootGraph::CacheMemberMetadata(FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform, const TSet<FName>& InInputsInheritingDefault, FMemberIDToMetadataMap& InOutCachedMemberMetadata) const
		{
			const FMetasoundFrontendDocument& ParentDocument = ParentBuilder->GetConstDocumentChecked();

			auto CreateNewLiteral = [&](UMetaSoundFrontendMemberMetadata* TemplateObject, FGuid MemberID) -> UMetaSoundFrontendMemberMetadata*
			{
				if (TemplateObject)
				{
					UMetaSoundFrontendMemberMetadata* NewMetadata = NewObject<UMetaSoundFrontendMemberMetadata>(&InOutBuilderToTransform.CastDocumentObjectChecked<UObject>(), TemplateObject->GetClass(), FName(), RF_Transactional, TemplateObject);
					check(NewMetadata);
					NewMetadata->MemberID = MemberID;
					return NewMetadata;
				}
				return nullptr;
			};
			
			for (const FMetasoundFrontendClassInput& ParentClassInput : ParentDocument.RootGraph.GetDefaultInterface().Inputs)
			{
				UMetaSoundFrontendMemberMetadata* MemberMetadata = nullptr;
				// Member id is id in InOutBuilder, which may not be the same as the node id of the class input of ParentBuilder
				FGuid MemberID;
				const FMetasoundFrontendClassInput* GraphInput = InOutBuilderToTransform.FindGraphInput(ParentClassInput.Name);

				if (GraphInput && GraphInput->TypeName == ParentClassInput.TypeName)
				{
					MemberID = GraphInput->NodeID;
					// If the input vertex already exists in the parent graph,
					// check if parent should be used or not from set of managed
					// input names.
					if (InInputsInheritingDefault.Contains(ParentClassInput.Name))
					{
						UMetaSoundFrontendMemberMetadata* ParentMetadata = ParentBuilder->FindMemberMetadata(ParentClassInput.NodeID);
						MemberMetadata = CreateNewLiteral(ParentMetadata, MemberID);
					}
					else
					{
						// Use existing defaults 
						MemberMetadata = InOutBuilderToTransform.FindMemberMetadata(GraphInput->NodeID);
					}
				}
				else
				{
					UMetaSoundFrontendMemberMetadata* ParentMetadata = ParentBuilder->FindMemberMetadata(ParentClassInput.NodeID);
					MemberID = ParentClassInput.NodeID;
					MemberMetadata = CreateNewLiteral(ParentMetadata, MemberID);
				}

				if (MemberMetadata)
				{
					InOutCachedMemberMetadata.Emplace(MemberID, MemberMetadata);
				}
			}
		}
#endif // WITH_EDITORONLY_DATA

		void FRebuildPresetRootGraph::AddAndConnectInputs(const TArray<FMetasoundFrontendClassInput>& InClassInputs, FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform, const FGuid& InReferencedNodeID) const
		{
			// Add inputs and space appropriately
			const INodeTemplate* InputTemplate = INodeTemplateRegistry::Get().FindTemplate(FInputNodeTemplate::ClassName);
			check(InputTemplate);

			TArray<const FMetasoundFrontendVertex*> ReferencedNodeInputVertices;
			TArray<const FMetasoundFrontendNode*> InputTemplateNodes;
			const TSet<FName>* InputsInheritingDefault = InOutBuilderToTransform.GetGraphInputsInheritingDefault();
			check(InputsInheritingDefault);
			
			for (const FMetasoundFrontendClassInput& ClassInput : InClassInputs)
			{
				const FName InputName = ClassInput.Name;
				// Input may have already been added if interface member, but defaults need to be overridden 
				const FMetasoundFrontendNode* InputNode = InOutBuilderToTransform.FindGraphInputNode(InputName);
				if (!InputNode)
				{
					InputNode = InOutBuilderToTransform.AddGraphInput(ClassInput);
				}
				else
				{
					// Defaults must be set as either the parent default or existing default may be different than the registered interface default
					// Setting defaults will set the input as non inheriting even if we're just copying the value 
					// so cache off bool and set back if necessary
					const bool bInheritsDefault = InputsInheritingDefault->Contains(InputName);
					InOutBuilderToTransform.SetGraphInputDefaults(InputName, ClassInput.GetDefaults());
					if (bInheritsDefault)
					{
						InOutBuilderToTransform.SetGraphInputInheritsDefault(InputName, /*bInputInheritsDefault=*/true);
					}
				}

				const FGuid InputNodeID = InputNode->GetID();
				check(InputNode);
				const FMetasoundFrontendVertex* InputNodeOutputVertex = InOutBuilderToTransform.FindNodeOutput(InputNode->GetID(), InputName);
				check(InputNodeOutputVertex);
				const FMetasoundFrontendVertex* ReferencedNodeInputVertex = InOutBuilderToTransform.FindNodeInput(InReferencedNodeID, InputName);
				check(ReferencedNodeInputVertex);
				
				ReferencedNodeInputVertices.Add(ReferencedNodeInputVertex);
				// If not editor, just make connection directly between input and output node
				#if !WITH_EDITORONLY_DATA
				InOutBuilderToTransform.AddEdge(FMetasoundFrontendEdge
				{
					InputNodeID,
					InputNodeOutputVertex->VertexID,
					InReferencedNodeID,
					ReferencedNodeInputVertex->VertexID
				});
				// If editor, add template node and connections
				#else
				// template node takes on data type of concrete input node's output type
				const FName DataType = InputNode->Interface.Outputs.Last().TypeName;
				
				FNodeTemplateGenerateInterfaceParams Params{ { }, { DataType } };
				const FMetasoundFrontendNode* TemplateNode = InOutBuilderToTransform.AddNodeByTemplate(*InputTemplate, MoveTemp(Params));

				check(TemplateNode);
				InputTemplateNodes.Add(TemplateNode);

				const FGuid TemplateNodeInputVertexID = TemplateNode->Interface.Inputs.Last().VertexID;
				const FGuid TemplateNodeOutputVertexID = TemplateNode->Interface.Outputs.Last().VertexID;
				InOutBuilderToTransform.AddEdge(FMetasoundFrontendEdge
				{
					InputNodeID,
					InputNodeOutputVertex->VertexID, 
					TemplateNode->GetID(),
					TemplateNodeInputVertexID
				});
				InOutBuilderToTransform.AddEdge(FMetasoundFrontendEdge
				{
					TemplateNode->GetID(),
					TemplateNodeOutputVertexID,
					InReferencedNodeID,
					ReferencedNodeInputVertex->VertexID
				});
				#endif
			}

#if WITH_EDITOR
			// Sort before adding nodes to graph layout & copy to preset (must be done after all
			// inputs/outputs are added but before setting locations to propagate effectively)
			const FMetasoundFrontendGraphClass& ParentRootGraph = ParentBuilder->GetConstDocumentChecked().RootGraph;
			FMetasoundFrontendInterfaceStyle Style = ParentRootGraph.GetDefaultInterface().GetInputStyle();

			// Sort vertices on referenced node
			// then use that order to order connected input nodes (which share the same names)
			auto GetInputDisplayName = [&InOutBuilderToTransform, &InReferencedNodeID](const FMetasoundFrontendVertex& Vertex)
			{
				return InOutBuilderToTransform.GetNodeInputDisplayName(InReferencedNodeID, Vertex.Name);
			};
			Style.SortVertices(ReferencedNodeInputVertices, GetInputDisplayName);
			InOutBuilderToTransform.SetInputStyle(MoveTemp(Style));

			// Set editor node locations
			FVector2D InputNodeLocation = FVector2D::ZeroVector;
			for (const FMetasoundFrontendNode* TemplateNode : InputTemplateNodes)
			{
				InOutBuilderToTransform.SetNodeLocation(TemplateNode->GetID(), InputNodeLocation);
				InputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
			}
#endif // WITH_EDITOR
		}

		void FRebuildPresetRootGraph::AddAndConnectOutputs(const TArray<FMetasoundFrontendClassOutput>& InClassOutputs, FMetaSoundFrontendDocumentBuilder& InOutBuilderToTransform, const FGuid& InReferencedNodeID) const
		{
			// Add outputs and space appropriately
			TArray<const FMetasoundFrontendVertex*> ReferencedNodeOutputVertices;

			for (const FMetasoundFrontendClassOutput& ClassOutput : InClassOutputs)
			{
				// Output may have already been added if interface member
				const FMetasoundFrontendNode* OutputNode = InOutBuilderToTransform.FindGraphOutputNode(ClassOutput.Name);
				if (!OutputNode)
				{
					OutputNode = InOutBuilderToTransform.AddGraphOutput(ClassOutput);
				}
				check(OutputNode);
				
				// Connect output node input vertex to corresponding referenced node output vertex. 
				const FMetasoundFrontendVertex* ReferencedNodeOutputVertex = InOutBuilderToTransform.FindNodeOutput(InReferencedNodeID, ClassOutput.Name);
				check(ReferencedNodeOutputVertex);
				const FMetasoundFrontendVertex* OutputNodeInputVertex = InOutBuilderToTransform.FindNodeInput(OutputNode->GetID(), ClassOutput.Name);
				check(OutputNodeInputVertex);

				InOutBuilderToTransform.AddEdge(FMetasoundFrontendEdge
				{
					InReferencedNodeID,
					ReferencedNodeOutputVertex->VertexID,
					OutputNode->GetID(),
					OutputNodeInputVertex->VertexID
				});
				ReferencedNodeOutputVertices.Add(ReferencedNodeOutputVertex);
			}

#if WITH_EDITOR
			// Sort before adding nodes to graph layout & copy to preset (must be done after all
			// inputs/outputs are added but before setting locations to propagate effectively)
			const FMetasoundFrontendGraphClass& ParentRootGraph = ParentBuilder->GetConstDocumentChecked().RootGraph;
			FMetasoundFrontendInterfaceStyle Style = ParentRootGraph.GetDefaultInterface().GetOutputStyle();

			// Sort vertices on referenced node
			// then use that order to order connected output nodes (which share the same names)
			auto GetOutputDisplayName = [&InOutBuilderToTransform, &InReferencedNodeID](const FMetasoundFrontendVertex& Vertex)
			{
				return InOutBuilderToTransform.GetNodeOutputDisplayName(InReferencedNodeID, Vertex.Name);
			};
			Style.SortVertices(ReferencedNodeOutputVertices, GetOutputDisplayName);

			InOutBuilderToTransform.SetOutputStyle(MoveTemp(Style));

			// Set output node locations
			FVector2D OutputNodeLocation = (2 * DisplayStyle::NodeLayout::DefaultOffsetX);
			for (const FMetasoundFrontendVertex* OutputVertex : ReferencedNodeOutputVertices)
			{
				const FName NodeName = OutputVertex->Name;
				const FGuid OutputNodeID = InOutBuilderToTransform.FindGraphOutputNode(NodeName)->GetID();

				// Set editor node locations
				const FGuid EdNodeGuid = FGuid::NewGuid(); // EdNodes are now never serialized and are transient, so just assign here
				InOutBuilderToTransform.SetNodeLocation(OutputNodeID, OutputNodeLocation);
				OutputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
			}
#endif // WITH_EDITOR
		}

		TArray<FMetasoundFrontendClassInput> FRebuildPresetRootGraph::GenerateRequiredClassInputs(FMetaSoundFrontendDocumentBuilder& InDocumentToTransformBuilder, TSet<FName>& OutInputsInheritingDefault) const
		{
			TArray<FMetasoundFrontendClassInput> ClassInputs;
			check(ParentBuilder);

			const FMetasoundFrontendGraphClass& ParentRootGraph = ParentBuilder->GetConstDocumentChecked().RootGraph;
			const FMetasoundFrontendDocument& ParentDocument = ParentBuilder->GetConstDocumentChecked();
			const FMetasoundFrontendDocument& DocumentToTransform = InDocumentToTransformBuilder.GetConstDocumentChecked();

			const TSet<FName>* ExistingInputsInheritingDefaultPtr = InDocumentToTransformBuilder.GetGraphInputsInheritingDefault();
			check(ExistingInputsInheritingDefaultPtr);
			TSet<FName> InputsInheritingDefault = *ExistingInputsInheritingDefaultPtr;

			// Iterate through all input nodes of referenced graph
			for (const FMetasoundFrontendClassInput& ParentClassInput : ParentDocument.RootGraph.GetDefaultInterface().Inputs)
			{
				// Copy class input and reset defaults and id
				const FName& NodeName = ParentClassInput.Name;
				FMetasoundFrontendClassInput NewClassInput = ParentClassInput;
				NewClassInput.VertexID = FDocumentIDGenerator::Get().CreateVertexID(DocumentToTransform);
				NewClassInput.ResetDefaults(/*bInitializeDefaultPage=*/false);

				if (const FMetasoundFrontendClassInput* ExistingClassInput = InDocumentToTransformBuilder.FindGraphInput(ParentClassInput.Name))
				{
					NewClassInput.NodeID = ExistingClassInput->NodeID;
				}
				
				auto InheritDefaultsFromGraph = [&NewClassInput, &NodeName](const FMetaSoundFrontendDocumentBuilder& InBuilder)
				{
					if (const FMetasoundFrontendClassInput* GraphClassInput = InBuilder.FindGraphInput(NodeName))
					{
						GraphClassInput->IterateDefaults([&NewClassInput](const FGuid& PageID, const FMetasoundFrontendLiteral& Literal)
						{
							NewClassInput.AddDefault(PageID) = Literal;
						});
					}
					else
					{
						NewClassInput.InitDefault();
					}
				};

				const FMetasoundFrontendClassInput* GraphInput = InDocumentToTransformBuilder.FindGraphInput(NodeName);
				if (GraphInput && GraphInput->TypeName == ParentClassInput.TypeName)
				{
					// If the input vertex already exists in the parent graph,
					// check if parent should be used or not from set of managed
					// input names.
					if (InputsInheritingDefault.Contains(NodeName))
					{
						InheritDefaultsFromGraph(*ParentBuilder);
					}
					else
					{
						// Use existing defaults 
						InheritDefaultsFromGraph(InDocumentToTransformBuilder);
					}
				}
				else
				{
					InheritDefaultsFromGraph(*ParentBuilder);
					// Add this to the list of inheriting as the
					// type no longer matches or it no longer exists
					InputsInheritingDefault.Add(NodeName);
				}
				ClassInputs.Add(MoveTemp(NewClassInput));
			}

			OutInputsInheritingDefault.Reset();
			Algo::TransformIf(ClassInputs, OutInputsInheritingDefault,
				[&InputsInheritingDefault](const FMetasoundFrontendClassInput& Input)
				{
					return InputsInheritingDefault.Contains(Input.Name);
				},
				[](const FMetasoundFrontendClassInput& Input)
				{
					return Input.Name;
				});
			
			return ClassInputs;
		}
		
		TArray<FMetasoundFrontendClassOutput> FRebuildPresetRootGraph::GenerateRequiredClassOutputs(FMetaSoundFrontendDocumentBuilder& InDocumentToTransformBuilder) const
		{
			TArray<FMetasoundFrontendClassOutput> ClassOutputs;
			check(ParentBuilder);

			const FMetasoundFrontendDocument& ParentDocument = ParentBuilder->GetConstDocumentChecked();
			const FMetasoundFrontendDocument& DocumentToTransform = InDocumentToTransformBuilder.GetConstDocumentChecked();

			for (const FMetasoundFrontendClassOutput& ParentClassOutput : ParentDocument.RootGraph.GetDefaultInterface().Outputs)
			{
				// Copy class output and reset defaults and id
				FMetasoundFrontendClassOutput NewClassOutput = ParentClassOutput;
				NewClassOutput.VertexID = FDocumentIDGenerator::Get().CreateVertexID(DocumentToTransform);

				if (const FMetasoundFrontendClassOutput* ExistingClassOutput = InDocumentToTransformBuilder.FindGraphOutput(ParentClassOutput.Name))
				{
					NewClassOutput.NodeID = ExistingClassOutput->NodeID;
				}
				ClassOutputs.Add(MoveTemp(NewClassOutput));
			}
			return ClassOutputs;
		}

		bool FRenameRootGraphClass::Transform(FDocumentHandle InDocument) const
		{
			return false;
		}

		bool FRenameRootGraphClass::Transform(FMetasoundFrontendDocument& InOutDocument) const
		{
			return false;
		}
#undef METASOUND_VERSIONING_LOG
	} // namespace Frontend
} // namespace Metasound
