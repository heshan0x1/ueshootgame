// Copyright Epic Games, Inc. All Rights Reserved.

#include "CodeRunnerBegin.h"

#include "CodeRunner.h"
#include "MuR/MutableTrace.h"
#include "MuR/Operations.h"
#include "MuR/SystemPrivate.h"


namespace UE::Mutable::Private
{
	CodeRunnerBegin::CodeRunnerBegin(FSystem& InSystem, const TSharedPtr<const FModel>& InModel, const TSharedPtr<const FParameters>& InParams, uint32 InLodMask) :
		Model(InModel),
		Params(InParams),
		System(InSystem),
		Program(InModel->GetPrivate()->Program),
		LodMask(InLodMask),
		Executed(InModel->GetPrivate()->Program.OpAddress.Num())
	{
	}

	
	bool FScheduledOpInline::operator==(const FScheduledOpInline& Other) const
	{
		return At == Other.At && Stage == Other.Stage && ExecutionIndex == Other.ExecutionIndex;
	}


	uint32 GetTypeHash(const FScheduledOpInline& Op)
	{
		uint32 Hash = Op.At;
		
		Hash = HashCombineFast(Hash, Op.Stage);
		Hash = HashCombineFast(Hash, Op.ExecutionIndex);

		return Hash;
	}


	uint32 GetTypeHash(const FCacheAddressInline& Address)
	{
		return HashCombineFast(Address.At, static_cast<uint32>(Address.ExecutionIndex));
	}


	FOpSet::FOpSet(int32 NumElements)
	{
		Index0.SetNum(NumElements, false);
	}


	bool FOpSet::Contains(const FCacheAddressInline& Item)
	{
		if (Item.ExecutionIndex == 0)
		{
			return Index0[Item.At];
		}
		else
		{
			return IndexOther.Contains(Item);
		}
	}


	void FOpSet::Add(const FCacheAddressInline& Item)
	{
		if (Item.ExecutionIndex == 0)
		{
			Index0[Item.At] = true;
		}
		else
		{
			IndexOther.Add(Item);
		}
	}
	
	
	FStackValue FStack::Pop()
	{
		return TArray::Pop(EAllowShrinking::No);
	}
	

	FProgramCache& CodeRunnerBegin::GetMemory() const
	{
		return *System.GetPrivate()->WorkingMemoryManager.CurrentInstanceCache;
	}


	FScheduledOpInline CodeRunnerBegin::PopOp()
	{
		return Items.Pop(EAllowShrinking::No);
	}


	void CodeRunnerBegin::PushOp(const FScheduledOpInline& Item)
	{
		check(Item.At < static_cast<uint32>(Program.OpAddress.Num()));

		if (!Item.At)
		{
			return;
		}

		Items.Push(Item);
	}


	void CodeRunnerBegin::StoreInt(const FCacheAddressInline& To, int32 Value)
	{
		if (!To.At)
		{
			return;
		}
		
		Executed.Add(To);

		FStackValue StackValue;
		StackValue.Set<int32>(Value);
		Stack.Push(StackValue);

		ResultsInt.Add(To, Value);
	}


	void CodeRunnerBegin::StoreFloat(const FCacheAddressInline& To, float Value)
	{
		if (!To.At)
		{
			return;
		}
		
		Executed.Add(To);

		FStackValue StackValue;
		StackValue.Set<float>(Value);
		Stack.Push(StackValue);

		ResultsFloat.Add(To, Value);
	}


	void CodeRunnerBegin::StoreBool(const FCacheAddressInline& To, bool Value)
	{
		if (!To.At)
		{
			return;
		}
		
		Executed.Add(To);

		FStackValue StackValue;
		StackValue.Set<bool>(Value);
		Stack.Push(StackValue);
			
		ResultsBool.Add(To, Value);
	}


	void CodeRunnerBegin::StoreInstance(const FCacheAddressInline& To, const TSharedPtr<const FInstance>& Value)
	{
		if (!To.At)
		{
			return;
		}

		Executed.Add(To);

		FStackValue StackValue;
		StackValue.Set<TSharedPtr<const FInstance>>(Value);
		Stack.Push(StackValue);

		ResultsInstance.Add(To, Value);
	}


	void CodeRunnerBegin::StoreString(const FCacheAddressInline& To, const TSharedPtr<const String>& Value)
	{
		if (!To.At)
		{
			return;
		}
		
		Executed.Add(To);

		FStackValue StackValue;
		StackValue.Set<TSharedPtr<const String>>(Value);
		Stack.Push(StackValue);

		ResultsString.Add(To, Value);
	}


	void CodeRunnerBegin::StoreExtensionData(const FCacheAddressInline& To, const TSharedPtr<const FExtensionData>& Value)
	{
		if (!To.At)
		{
			return;
		}
		
		Executed.Add(To);

		FStackValue StackValue;
		StackValue.Set<TSharedPtr<const FExtensionData>>(Value);
		Stack.Push(StackValue);

		ResultsExtensionData.Add(To, Value);
	}


	void CodeRunnerBegin::StoreNone(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return;
		}
		
		Executed.Add(To);
	}


	int32 CodeRunnerBegin::LoadInt(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return 0;	
		}
		
		return Stack.Pop().Get<int32>();
	}


	float CodeRunnerBegin::LoadFloat(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return 0.0f;	
		}

		return Stack.Pop().Get<float>();
	}


	bool CodeRunnerBegin::LoadBool(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return false;	
		}

		return Stack.Pop().Get<bool>();
	}


	TSharedPtr<const FInstance> CodeRunnerBegin::LoadInstance(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return nullptr;	
		}
			
		return Stack.Pop().Get<TSharedPtr<const FInstance>>();
	}


	TSharedPtr<const String> CodeRunnerBegin::LoadString(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return nullptr;	
		}
		
		return Stack.Pop().Get<TSharedPtr<const String>>();
	}


	TSharedPtr<const FExtensionData> CodeRunnerBegin::LoadExtensionData(const FCacheAddressInline& To)
	{
		if (!To.At)
		{
			return nullptr;	
		}
		
		return Stack.Pop().Get<TSharedPtr<const FExtensionData>>();
	}


	TSharedPtr<const FInstance> CodeRunnerBegin::RunCode(const FScheduledOpInline& Root)
	{
		MUTABLE_CPUPROFILER_SCOPE(CodeRunnerBegin::RunCode);
		
		PushOp(Root);
		
		while (!Items.IsEmpty())
		{
			const FScheduledOpInline Item = PopOp();
			
			const EOpType Type = Program.GetOpType(Item.At);

			if (Executed.Contains(Item))
			{
				switch (GetOpDataType(Type))
				{
				case EDataType::Bool:
					{
						FStackValue Value;
						Value.Set<bool>(ResultsBool[Item]);
					
						Stack.Push(Value);
						break;
					}

				case EDataType::Int:
					{
						FStackValue Value;
						Value.Set<int32>(ResultsInt[Item]);
					
						Stack.Push(Value);
						break;
					}
					
				case EDataType::Instance:
					{
						FStackValue Value;
						Value.Set<TSharedPtr<const FInstance>>(ResultsInstance[Item]);
					
						Stack.Push(Value);
						break;
					}
					
				case EDataType::String:
					{
						FStackValue Value;
						Value.Set<TSharedPtr<const String>>(ResultsString[Item]);
					
						Stack.Push(Value);
						break;
					}
								
				case EDataType::ExtensionData:
					{
						FStackValue Value;
						Value.Set<TSharedPtr<const FExtensionData>>(ResultsExtensionData[Item]);
					
						Stack.Push(Value);
						break;
					}

				case EDataType::Scalar:
					{
						FStackValue Value;
						Value.Set<float>(ResultsFloat[Item]);
					
						Stack.Push(Value);
						break;
					}
					
				case EDataType::Color:		
				case EDataType::Projector:	
				case EDataType::Mesh:		
				case EDataType::Image:		
				case EDataType::Layout:		
				case EDataType::Material:		
					break;

				default:
					unimplemented();
				}

				continue;
			}
			
			switch (Type)
			{
			case EOpType::CO_CONSTANT:
			case EOpType::IM_CONSTANT:
			case EOpType::LA_CONSTANT:
			case EOpType::MA_CONSTANT:
			case EOpType::PR_CONSTANT:
			case EOpType::CO_PARAMETER:
			case EOpType::MA_PARAMETER:
			case EOpType::PR_PARAMETER:
			case EOpType::ME_PARAMETER:
			case EOpType::IM_PARAMETER:
			case EOpType::MI_PARAMETER:
			case EOpType::IM_PARAMETER_FROM_MATERIAL:
				{
					StoreNone(Item);
					break;
				}
			
			case EOpType::CO_CONDITIONAL:
			case EOpType::ED_CONDITIONAL:
			case EOpType::IM_CONDITIONAL:
			case EOpType::IN_CONDITIONAL:
			case EOpType::LA_CONDITIONAL:
			case EOpType::ME_CONDITIONAL:
			case EOpType::SC_CONDITIONAL:
			case EOpType::MI_CONDITIONAL:
				{
					OP::ConditionalArgs Args = Program.GetOpArgs<OP::ConditionalArgs>(Item.At);

					switch(Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline( Item,1));
							PushOp(FScheduledOpInline( Args.condition, Item));

							break;
						}

					case 1:
						{
							const bool Value = LoadBool(Args.condition);

							OP::ADDRESS ResultAt = Value ? Args.yes : Args.no;

							// Schedule the end of this instruction if necessary
							FScheduledOpInline NextStage(Item, 2);
							NextStage.CustomState = ResultAt;
							PushOp(NextStage);
							
							PushOp(FScheduledOpInline(ResultAt, Item));
							
							break;
						}

					case 2:
						{
							switch (GetOpDataType(Type))
							{
							case EDataType::Int:
								StoreInt(Item, LoadInt(Item.CustomState));
								break;
							
							case EDataType::String:
								StoreString(Item, LoadString(Item.CustomState));
								break;

							case EDataType::Instance:
								StoreInstance(Item, LoadInstance(Item.CustomState));
								break;

							case EDataType::ExtensionData:
								StoreExtensionData(Item, LoadExtensionData(Item.CustomState));
								break;
							
							case EDataType::Scalar:		
								StoreFloat(Item, LoadFloat(Item.CustomState));
								break;

							case EDataType::Color:		
							case EDataType::Projector:	
							case EDataType::Mesh:		
							case EDataType::Image:		
							case EDataType::Layout:		
							case EDataType::Material:		
								StoreNone(Item);
								break;

							case EDataType::Bool:
								check(false);
								
							default:
								unimplemented();
							}

							break;
						}

					default:
						unimplemented();
					}
				
					break;
				}

			case EOpType::NU_CONDITIONAL:
				{
					OP::ConditionalArgs Args = Program.GetOpArgs<OP::ConditionalArgs>(Item.At);

					switch(Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item,1));
							PushOp(FScheduledOpInline(Args.condition, Item));

							break;
						}

					case 1:
						{
							const bool Value = LoadBool(Args.condition);
							
							OP::ADDRESS ResultAt = Value ? Args.yes : Args.no;

							PushOp(FScheduledOpInline(ResultAt, Item));

							StoreNone(Item);
							break;
						}

					default:
						unimplemented();
					}
				
					break;
				}
				
			case EOpType::CO_SWITCH:
			case EOpType::ED_SWITCH:
			case EOpType::IM_SWITCH:
			case EOpType::IN_SWITCH:
			case EOpType::LA_SWITCH:
			case EOpType::ME_SWITCH:
			case EOpType::NU_SWITCH:
			case EOpType::SC_SWITCH:
			case EOpType::MI_SWITCH:
				{
					const uint8* Data = Program.GetOpArgsPointer(Item.At);

					OP::ADDRESS VarAddress;
					FMemory::Memcpy(&VarAddress, Data, sizeof(OP::ADDRESS));
					Data += sizeof(OP::ADDRESS);

					OP::ADDRESS DefAddress;
					FMemory::Memcpy(&DefAddress, Data, sizeof(OP::ADDRESS));
					Data += sizeof(OP::ADDRESS);

					OP::FSwitchCaseDescriptor CaseDesc;
					FMemory::Memcpy(&CaseDesc, Data, sizeof(OP::FSwitchCaseDescriptor));
					Data += sizeof(OP::FSwitchCaseDescriptor);

					switch (Item.Stage)
					{
					case 0:
						{
							if (VarAddress)
							{
								PushOp(FScheduledOpInline(Item, 1));
								PushOp(FScheduledOpInline(VarAddress, Item));
							}
							else
							{
								switch (GetOpDataType(Type))
								{
								case EDataType::Bool:
									StoreBool(Item, false);
									break;
								
								case EDataType::Int:
									StoreInt(Item, 0);
									break;
								
								case EDataType::Instance:
									StoreInstance(Item, nullptr);
									break;

								case EDataType::String:
									StoreString(Item, nullptr);
									break;
								
								case EDataType::ExtensionData:
									StoreExtensionData(Item, MakeShared<FExtensionData>());
									break;

								case EDataType::Scalar:
									StoreFloat(Item, 0.0f);
									break;
									
								case EDataType::Color:			
								case EDataType::Projector:		
								case EDataType::Mesh:			
								case EDataType::Image:			
								case EDataType::Layout:			
								case EDataType::Material:			
									StoreNone(Item);
									break;
								
								default:
									unimplemented()
								}
							}
							break;
						}

					case 1:
						{
							// Get the variable result
							int32 Var = LoadInt(VarAddress);

							OP::ADDRESS ValueAt = DefAddress;

							if (!CaseDesc.bUseRanges)
							{
								for (uint32 C = 0; C < CaseDesc.Count; ++C)
								{
									int32 Condition;
									FMemory::Memcpy(&Condition, Data, sizeof(int32));
									Data += sizeof(int32);

									OP::ADDRESS CaseAt;
									FMemory::Memcpy(&CaseAt, Data, sizeof(OP::ADDRESS));
									Data += sizeof(OP::ADDRESS);

									if (CaseAt && Var == (int32)Condition)
									{
										ValueAt = CaseAt;
										break;
									}
								}
							}
							else
							{
								for (uint32 C = 0; C < CaseDesc.Count; ++C)
								{
									int32 ConditionStart;
									FMemory::Memcpy(&ConditionStart, Data, sizeof(int32));
									Data += sizeof(int32);

									uint32 RangeSize;
									FMemory::Memcpy(&RangeSize, Data, sizeof(uint32));
									Data += sizeof(uint32);

									OP::ADDRESS CaseAt;
									FMemory::Memcpy(&CaseAt, Data, sizeof(OP::ADDRESS));
									Data += sizeof(OP::ADDRESS);

									if (CaseAt && Var >= ConditionStart && Var < int32(ConditionStart + RangeSize))
									{
										ValueAt = CaseAt;
										break;
									}
								}
							}

							// Schedule the end of this instruction if necessary
							FScheduledOpInline NextStage(Item, 2);
							NextStage.CustomState = ValueAt;
							PushOp(NextStage);

							PushOp(FScheduledOpInline(ValueAt, Item));
							
							break;
						}
						
					case 2:
						{
							switch (GetOpDataType(Type))
							{
							case EDataType::Bool:
								StoreBool(Item, LoadBool(Item.CustomState));
								break;

							case EDataType::Int:
								StoreInt(Item, LoadInt(Item.CustomState));
								break;

							case EDataType::Instance:
								StoreInstance(Item, LoadInstance(Item.CustomState));
								break;
								
							case EDataType::String:
								StoreString(Item, LoadString(Item.CustomState));
								break;
								
							case EDataType::ExtensionData:
								StoreExtensionData(Item, LoadExtensionData(Item.CustomState));
								break;

							case EDataType::Scalar:
								StoreFloat(Item, LoadFloat(Item.CustomState));
								break;
								
							case EDataType::Color:		
							case EDataType::Projector:	
							case EDataType::Mesh:		
							case EDataType::Image:		
							case EDataType::Layout:		
							case EDataType::Material:		
								StoreNone(Item);
								break;

							default:
								unimplemented();
							}

							break;
						}

					default:
						unimplemented();
					}

					break;
				}

			case EOpType::IN_ADDVECTOR:
			case EOpType::IN_ADDSCALAR:
				{
					OP::InstanceAddArgs Args = Program.GetOpArgs<OP::InstanceAddArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.instance, Item));

							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.instance)
							{
								InInstance = LoadInstance(Args.instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}

							StoreInstance( Item, Instance);
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
        		
			case EOpType::IN_ADDMESH:
				{
					OP::InstanceAddArgs Args = Program.GetOpArgs<OP::InstanceAddArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.instance, Item));
							PushOp(FScheduledOpInline(Args.value, Item));
							
							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.instance)
							{
								InInstance = LoadInstance(Args.instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}
							
							if (Args.value)
							{
								FMeshId MeshId = System.GetPrivate()->WorkingMemoryManager.GetMeshId(Model, Params.Get(), Args.RelevantParametersListIndex, Args.value);
								OP::ADDRESS NameAd = Args.name;
								check(NameAd < (uint32)Program.ConstantStrings.Num());
								const FString& Name = Program.ConstantStrings[NameAd];
								Instance->SetMesh(0, 0, MeshId, FName(Name));
							}
							
							StoreInstance(Item, Instance);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}

			case EOpType::IN_ADDIMAGE:
				{
					OP::InstanceAddArgs Args = Program.GetOpArgs<OP::InstanceAddArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.instance, Item));
							PushOp(FScheduledOpInline(Args.value, Item));

							break;
						}
						
					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.instance)
							{
								InInstance = LoadInstance(Args.instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}
							
							if (Args.value)
							{
								FImageId ImageId = System.GetPrivate()->WorkingMemoryManager.GetImageId(Model, Params.Get(), Args.RelevantParametersListIndex, Args.value);
								OP::ADDRESS NameAd = Args.name;
								check(NameAd < (uint32)Program.ConstantStrings.Num());
								const FString& Name = Program.ConstantStrings[NameAd];
								Instance->AddImage(0, 0, 0, ImageId, FName(Name) );
							}
							
							StoreInstance(Item, Instance);
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
			case EOpType::IN_ADDMATERIAL:
				{
					OP::InstanceAddMaterialArgs Args = Program.GetOpArgs<OP::InstanceAddMaterialArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.Instance, Item));
							PushOp(FScheduledOpInline(Args.Material, Item));
							
							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.Instance)
							{
								InInstance = LoadInstance(Args.Instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}


							if (Args.Material)
							{
								FMaterialId MaterialId = System.GetPrivate()->WorkingMemoryManager.GetMaterialId(Model, Params.Get(), Args.RelevantParametersListIndex, Args.Material);
								Instance->SetMaterialId(0, 0, 0, MaterialId);
							}
							
							StoreInstance( Item, Instance);
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
					
			case EOpType::IN_ADDOVERLAYMATERIAL:
				{
					OP::InstanceAddMaterialArgs Args = Program.GetOpArgs<OP::InstanceAddMaterialArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.Instance, Item));
							PushOp(FScheduledOpInline(Args.Material, Item));

							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.Instance)
							{
								InInstance = LoadInstance(Args.Instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}

							if (Args.Material)
							{
								FMaterialId MaterialId = System.GetPrivate()->WorkingMemoryManager.GetMaterialId(Model, Params.Get(), Args.RelevantParametersListIndex, Args.Material);
								Instance->SetOverlayMaterialId(0, MaterialId);
							}
							
							StoreInstance( Item, Instance);
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
				
			case EOpType::IN_ADDCOMPONENT:
				{
					OP::InstanceAddArgs Args = Program.GetOpArgs<OP::InstanceAddArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.instance, Item));
							PushOp(FScheduledOpInline(Args.value, Item));
							
							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.instance)
							{
								InInstance = LoadInstance(Args.instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}

							if (Args.value)
							{
								TSharedPtr<const FInstance> pComp = LoadInstance(Args.value);

								int32 NewComponentIndex = Instance->AddComponent();

								if ( !pComp->Components.IsEmpty() )
								{
									Instance->Components[NewComponentIndex] = pComp->Components[0];

									// Id
									Instance->Components[NewComponentIndex].Id = Args.ExternalId;
								}
							}

							StoreInstance(Item, Instance);							
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
				
			case EOpType::IN_ADDSTRING:
				{
					OP::InstanceAddArgs Args = Program.GetOpArgs<OP::InstanceAddArgs>( Item.At );
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.instance, Item));
							PushOp(FScheduledOpInline(Args.value, Item));

							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.instance)
							{
								InInstance = LoadInstance(Args.instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}
							
							StoreInstance( Item, Instance );
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
				
			case EOpType::IN_ADDSURFACE:
				{
					OP::InstanceAddArgs Args = Program.GetOpArgs<OP::InstanceAddArgs>(Item.At);
		            switch (Item.Stage)
		            {
		            case 0:
			            {
		            		PushOp(FScheduledOpInline(Item, 1));
				            PushOp(FScheduledOpInline(Args.instance, Item));
				            PushOp(FScheduledOpInline(Args.value, Item));

				            break;
			            }
		            
		            case 1:
		            {
		            	TSharedPtr<const FInstance> InInstance;
		            	if (Args.instance)
		            	{
		            		InInstance = LoadInstance(Args.instance);
		            	}

		            	TSharedPtr<FInstance> Instance;
		            	if (InInstance)
		            	{
		            		Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
		            	}
		            	else
		            	{
		            		Instance = MakeShared<FInstance>();
		            	}

		                // Empty surfaces are ok, they still need to be created, because they may contain
		                // additional information like internal or external IDs
		            	TSharedPtr<const FInstance> Value;
		            	if (Args.value)
		            	{
		            		Value = LoadInstance(Args.value);
		            	}
		            		
	                    int32 SurfaceIndex = Instance->AddSurface(0, 0);

	                    // Surface data
	                    if (Value &&
	                        Value->Components.Num() &&
							Value->Components[0].LODs.Num() &&
	                        Value->Components[0].LODs[0].Surfaces.Num())
	                    {
	                        Instance->Components[0].LODs[0].Surfaces[SurfaceIndex] = Value->Components[0].LODs[0].Surfaces[0];
	                    }

	                    // Name
	                    OP::ADDRESS nameAd = Args.name;
	                    check( nameAd < (uint32)Program.ConstantStrings.Num() );
	                    const FString& Name = Program.ConstantStrings[ nameAd ];
	                    Instance->SetSurfaceName( 0, 0, SurfaceIndex, FName(Name) );

	                    // IDs
	                    Instance->Components[0].LODs[0].Surfaces[SurfaceIndex].InternalId = Args.id;
	                    Instance->Components[0].LODs[0].Surfaces[SurfaceIndex].ExternalId = Args.ExternalId;
	                    Instance->Components[0].LODs[0].Surfaces[SurfaceIndex].SharedId = Args.SharedSurfaceId;
		            		
		                StoreInstance(Item, Instance);
		                break;
		            }

		            default:
		                check(false);
		            }

		            break;
				}

			case EOpType::IN_ADDLOD:
				{
					const uint8* Data = Program.GetOpArgsPointer(Item.At);

					uint8 LODCount;
					FMemory::Memcpy(&LODCount, Data, sizeof(uint8));
					Data += sizeof(uint8);

					switch (Item.Stage)
		            {
		            case 0:
		            {
		            	PushOp(FScheduledOpInline( Item, 1));

		                for (uint8 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
		                {
							OP::ADDRESS LODAddress;
							FMemory::Memcpy(&LODAddress, Data, sizeof(OP::ADDRESS));
							Data += sizeof(OP::ADDRESS);

		                    if (LODAddress)
		                    {
		                        const bool bSelectedLod = ((1 << LODIndex) & LodMask) != 0;
		                        if (bSelectedLod)
		                        {
		                        	PushOp(FScheduledOpInline(LODAddress, Item));
		                        }
		                    }
		                }


		                break;
		            }

		            case 1:
		            {
		                // Assemble result
						TSharedPtr<FInstance> Result = MakeShared<FInstance>();
						int32 ComponentIndex = Result->AddComponent();

		                for (uint8 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
		                {
							OP::ADDRESS LODAddress;
							FMemory::Memcpy(&LODAddress, Data, sizeof(OP::ADDRESS));
							Data += sizeof(OP::ADDRESS);
							
							if (LODAddress)
		                    {						
								bool bIsSelectedLod = ((1 << LODIndex) & LodMask ) != 0;

								// Add an empty LOD even if not selected.
								int32 InstanceLODIndex = Result->AddLOD(ComponentIndex);
								
								if (bIsSelectedLod)
		                        {
									TSharedPtr<const FInstance> LOD = LoadInstance(LODAddress);

		                            // In a degenerated case, the returned pLOD may not have an LOD inside
 									if (!LOD->Components.IsEmpty() &&
										!LOD->Components[0].LODs.IsEmpty())
									{
										Result->Components[ComponentIndex].LODs[InstanceLODIndex] = LOD->Components[0].LODs[0];
									}
								}
		                    }
		                }

		                StoreInstance(Item, Result);
		                break;
		            }

		            default:
		                unimplemented();
		            }

		            break;
		        }
				
			case EOpType::IN_ADDEXTENSIONDATA:
				{
					OP::InstanceAddExtensionDataArgs Args = Program.GetOpArgs<OP::InstanceAddExtensionDataArgs>(Item.At);

					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.Instance, Item));
							PushOp(FScheduledOpInline(Args.ExtensionData, Item));

							break;
						}

					case 1:
						{
							TSharedPtr<const FInstance> InInstance;
							if (Args.Instance)
							{
								InInstance = LoadInstance(Args.Instance);
							}

							TSharedPtr<FInstance> Instance;
							if (InInstance)
							{
								Instance = UE::Mutable::Private::CloneOrTakeOver<FInstance>(InInstance);
							}
							else
							{
								Instance = MakeShared<FInstance>();
							}
							
							if (TSharedPtr<const FExtensionData> ExtensionData = LoadExtensionData(Args.ExtensionData))
							{
								const OP::ADDRESS NameAddress = Args.ExtensionDataName;
								check(NameAddress < (uint32)Program.ConstantStrings.Num());
								const FString& NameString = Program.ConstantStrings[NameAddress];

								Instance->SetExtensionData(ExtensionData.ToSharedRef(), FName(NameString));
							}

							StoreInstance(Item, Instance);
							break;
						}

					default:
						unimplemented();
					}

					break;
				}
				
			case EOpType::BO_CONSTANT:
				{
					OP::BoolConstantArgs Args = Program.GetOpArgs<OP::BoolConstantArgs>(Item.At);

					StoreBool(Item, Args.bValue);
					break;
				}

			case EOpType::BO_PARAMETER:
				{
					OP::ParameterArgs Args = Program.GetOpArgs<OP::ParameterArgs>(Item.At);

					FScheduledOp Op;
					Op.At = Item.At;
					Op.ExecutionIndex = Item.ExecutionIndex;
        		
					TSharedPtr<FRangeIndex> Index = BuildCurrentOpRangeIndex(Op, *Params, *Model.Get(), GetMemory(), Args.variable);
					const bool Value = Params->GetBoolValue(Args.variable, Index.Get());
					
					StoreBool(Item, Value);
					break;
				}

			case EOpType::BO_AND:
				{
					OP::BoolBinaryArgs Args = Program.GetOpArgs<OP::BoolBinaryArgs>(Item.At);

					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.A, Item));
							PushOp(FScheduledOpInline(Args.B, Item));

							break;
						}

					case 1:
						{
							const bool ValueB = LoadBool(Args.A);
							const bool ValueA = LoadBool(Args.B);

							const bool Result = ValueA && ValueB;

							StoreBool(Item, Result);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}

			case EOpType::BO_OR:
				{
					OP::BoolBinaryArgs Args = Program.GetOpArgs<OP::BoolBinaryArgs>(Item.At);

					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.A, Item));
							PushOp(FScheduledOpInline(Args.B, Item));

							break;
						}

					case 1:
						{
							const bool ValueB = LoadBool(Args.A);
							const bool ValueA = LoadBool(Args.B);

							const bool Result = ValueA || ValueB;
							
							StoreBool(Item, Result);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}

			case EOpType::BO_NOT:
				{
					OP::BoolNotArgs Args = Program.GetOpArgs<OP::BoolNotArgs>(Item.At);

					switch ( Item.Stage )
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.A, Item));

							break;
						}
					
					case 1:
						{
							const bool Value = LoadBool(Args.A);
							
							const bool Result = !Value;
							
							StoreBool(Item, Result);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}

			case EOpType::BO_EQUAL_INT_CONST:
				{
					OP::BoolEqualScalarConstArgs Args = Program.GetOpArgs<OP::BoolEqualScalarConstArgs>(Item.At);

					switch ( Item.Stage )
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.Value, Item));

							break;
						}
					
					case 1:
						{
							const int32 Value = LoadInt(Args.Value);
							
							const bool Result = Value == Args.Constant;

							StoreBool(Item, Result);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}

			case EOpType::SC_CONSTANT:
				{
					OP::ScalarConstantArgs Args = Program.GetOpArgs<OP::ScalarConstantArgs>(Item.At);

					StoreFloat(Item, Args.Value);
					break;
				}
			
				
			case EOpType::SC_PARAMETER:
				{
					OP::ParameterArgs Args = Program.GetOpArgs<OP::ParameterArgs>(Item.At);

					FScheduledOp Op;
					Op.At = Item.At;
					Op.ExecutionIndex = Item.ExecutionIndex;
					
					TSharedPtr<FRangeIndex> Index = BuildCurrentOpRangeIndex(Op, *Params, *Model.Get(), GetMemory(), Args.variable);
					const float Result = Params->GetFloatValue( Args.variable, Index.Get());

					StoreFloat(Item, Result);
					break;
				}
				
			case EOpType::SC_ARITHMETIC:
				{
					OP::ArithmeticArgs Args = Program.GetOpArgs<OP::ArithmeticArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.A, Item));
							PushOp(FScheduledOpInline(Args.B, Item));

							break;
						}

					case 1:
						{
							float ValueA = LoadFloat(Args.A);
							float ValueB = LoadFloat(Args.B);

							float Result = 1.0f;
							switch (Args.Operation)
							{
							case OP::ArithmeticArgs::ADD:
								Result = ValueA + ValueB;
								break;

							case OP::ArithmeticArgs::MULTIPLY:
								Result = ValueA * ValueB;
								break;

							case OP::ArithmeticArgs::SUBTRACT:
								Result = ValueA - ValueB;
								break;

							case OP::ArithmeticArgs::DIVIDE:
								Result = ValueA / ValueB;
								break;

							default:
								unimplemented();
							}

							StoreFloat(Item, Result);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}
			
			case EOpType::SC_CURVE:
				{
					OP::ScalarCurveArgs Args = Program.GetOpArgs<OP::ScalarCurveArgs>(Item.At);
					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.time, Item));

							break;
						}

					case 1:
						{
							const float Time = LoadFloat(Args.time);

							const FRichCurve& Curve = Program.ConstantCurves[Args.curve];
							float Result = Curve.Eval(Time);

							StoreFloat(Item, Result);
							break;
						}

					default:
						unimplemented();
					}
					
					break;
				}
				
			case EOpType::ED_CONSTANT:
				{
					OP::ResourceConstantArgs Args = Program.GetOpArgs<OP::ResourceConstantArgs>(Item.At);

					// Assume the ROM has been loaded previously
					TSharedPtr<const FExtensionData> SourceConst;
					Program.GetExtensionDataConstant(Args.value, SourceConst);

					check(SourceConst);
					
					StoreExtensionData(Item, SourceConst);
					break;
				}

			case EOpType::ST_CONSTANT:
				{
					OP::ResourceConstantArgs Args = Program.GetOpArgs<OP::ResourceConstantArgs>(Item.At);
				
					check(Args.value < static_cast<uint32>(Model->GetPrivate()->Program.ConstantStrings.Num()));

					const FString& Result = Program.ConstantStrings[Args.value];

					TSharedPtr<const String> Value = MakeShared<String>(Result);

					StoreString(Item, Value);
					break;
				}

			case EOpType::ST_PARAMETER:
				{
					OP::ParameterArgs Args = Program.GetOpArgs<OP::ParameterArgs>(Item.At);

					FScheduledOp Op;
					Op.At = Item.At;
					Op.ExecutionIndex = Item.ExecutionIndex;
				
					TSharedPtr<FRangeIndex> Index = BuildCurrentOpRangeIndex(Op, *Params, *Model.Get(), GetMemory(), Args.variable);

					FString Result;
					Params->GetStringValue(Args.variable, Result, Index.Get());

					TSharedPtr<const String> Value = MakeShared<String>(Result);
					
					StoreString(Item, Value);
					break;
				}

			case EOpType::NU_CONSTANT:
				{
					OP::IntConstantArgs Args = Program.GetOpArgs<OP::IntConstantArgs>(Item.At);

					const int32 Value = Args.Value;
					
					StoreInt(Item, Value);
					break;
				}

			case EOpType::NU_PARAMETER:
				{
					OP::ParameterArgs Args = Program.GetOpArgs<OP::ParameterArgs>(Item.At);

					const int32 Value = Params->GetIntValue(Args.variable);

					StoreInt(Item, Value);
					break;
				}
			
			case EOpType::CO_ARITHMETIC:
				{
					OP::ArithmeticArgs Args = Program.GetOpArgs<OP::ArithmeticArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.A, Item));
					PushOp(FScheduledOpInline(Args.B, Item));

					StoreNone(Item);
					break;	
				}
	
			case EOpType::CO_FROMSCALARS:
				{
					StoreNone(Item);
					break;
				}
				
			case EOpType::CO_SAMPLEIMAGE:
				{
					OP::ColourSampleImageArgs Args = Program.GetOpArgs<OP::ColourSampleImageArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Image, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::CO_SWIZZLE:
				{
					OP::ColourSwizzleArgs Args = Program.GetOpArgs<OP::ColourSwizzleArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.sources[0], Item));
					PushOp(FScheduledOpInline(Args.sources[1], Item));
					PushOp(FScheduledOpInline(Args.sources[2], Item));
					PushOp(FScheduledOpInline(Args.sources[3], Item));

					StoreNone(Item);
					break;
				}

			case EOpType::ME_CONSTANT:
				{
					OP::MeshConstantArgs Args = Program.GetOpArgs<OP::MeshConstantArgs>(Item.At);
					ME_SKELETON_ID.Push(Args.Skeleton);

					StoreNone(Item);
					break;
				}
				
			case EOpType::ME_REFERENCE:
				{
					OP::ResourceReferenceArgs Args = Program.GetOpArgs<OP::ResourceReferenceArgs>(Item.At);
					ME_REFERENCE_ID.Push(Args.ID);

					StoreNone(Item);
					break;
				}

			case EOpType::ME_APPLYLAYOUT:
				{
					OP::MeshApplyLayoutArgs Args = Program.GetOpArgs<OP::MeshApplyLayoutArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Mesh, Item));

					StoreNone(Item);
					break;
				}
	
	
			case EOpType::ME_PREPARELAYOUT:
				{
					OP::MeshPrepareLayoutArgs Args = Program.GetOpArgs<OP::MeshPrepareLayoutArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Mesh, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_DIFFERENCE:
				{
					const uint8* data = Program.GetOpArgsPointer(Item.At);

					OP::ADDRESS BaseAt = 0;
					FMemory::Memcpy(&BaseAt, data, sizeof(OP::ADDRESS)); 
					data += sizeof(OP::ADDRESS);

					OP::ADDRESS TargetAt = 0;
					FMemory::Memcpy(&TargetAt, data, sizeof(OP::ADDRESS)); 
					data += sizeof(OP::ADDRESS);

					PushOp(FScheduledOpInline(BaseAt, Item));
					PushOp(FScheduledOpInline(TargetAt, Item));

					StoreNone(Item);	
					break;
				}
	
			case EOpType::ME_MORPH:
				{
					const uint8* Data = Program.GetOpArgsPointer(Item.At);

					OP::CONSTANT_STRING_ADDRESS NameAt = 0;
					FMemory::Memcpy(&NameAt, Data, sizeof(OP::CONSTANT_STRING_ADDRESS)); 
					Data += sizeof(OP::CONSTANT_STRING_ADDRESS);
					
					OP::ADDRESS FactorAt = 0;
					FMemory::Memcpy(&FactorAt, Data, sizeof(OP::ADDRESS)); 
					Data += sizeof(OP::ADDRESS);
			
					OP::ADDRESS BaseAt = 0;
					FMemory::Memcpy(&BaseAt, Data, sizeof(OP::ADDRESS)); 
					Data += sizeof(OP::ADDRESS);

					OP::ADDRESS TargetAt = 0;
					FMemory::Memcpy(&TargetAt, Data, sizeof(OP::ADDRESS)); 
					Data += sizeof(OP::ADDRESS);

					PushOp(FScheduledOpInline(BaseAt, Item));
					PushOp(FScheduledOpInline(TargetAt, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_MERGE:
				{
					OP::MeshMergeArgs Args = Program.GetOpArgs<OP::MeshMergeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));
					PushOp(FScheduledOpInline(Args.Added, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_MASKCLIPMESH:
				{
					OP::MeshMaskClipMeshArgs Args = Program.GetOpArgs<OP::MeshMaskClipMeshArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));
					PushOp(FScheduledOpInline(Args.clip, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_MASKCLIPUVMASK:
				{
					OP::MeshMaskClipUVMaskArgs Args = Program.GetOpArgs<OP::MeshMaskClipUVMaskArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));
					PushOp(FScheduledOpInline(Args.UVSource, Item));
					PushOp(FScheduledOpInline(Args.MaskImage, Item));
					PushOp(FScheduledOpInline(Args.LayoutIndex, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_MASKDIFF:
				{
					OP::MeshMaskDiffArgs Args = Program.GetOpArgs<OP::MeshMaskDiffArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));
					PushOp(FScheduledOpInline(Args.Fragment, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::ME_FORMAT:
				{
					OP::MeshFormatArgs Args = Program.GetOpArgs<OP::MeshFormatArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));
					PushOp(FScheduledOpInline(Args.format, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_EXTRACTLAYOUTBLOCK:
				{
					const uint8* Data = Program.GetOpArgsPointer(Item.At);

					OP::ADDRESS Source;
					FMemory::Memcpy( &Source, Data, sizeof(OP::ADDRESS) );
					Data += sizeof(OP::ADDRESS);
					
					PushOp(FScheduledOpInline(Source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_TRANSFORM:
				{
					OP::MeshTransformArgs Args = Program.GetOpArgs<OP::MeshTransformArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_CLIPMORPHPLANE:
				{
					OP::MeshClipMorphPlaneArgs Args = Program.GetOpArgs<OP::MeshClipMorphPlaneArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));

					StoreNone(Item);
					break;
				}
			
			case EOpType::ME_CLIPWITHMESH:
				{
					OP::MeshClipWithMeshArgs Args = Program.GetOpArgs<OP::MeshClipWithMeshArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));
					PushOp(FScheduledOpInline(Args.ClipMesh, Item));

					StoreNone(Item);
					break;
				}
				
			case EOpType::ME_CLIPDEFORM:
				{
					OP::MeshClipDeformArgs Args = Program.GetOpArgs<OP::MeshClipDeformArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.mesh, Item));
					PushOp(FScheduledOpInline(Args.clipShape, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::ME_APPLYPOSE:
				{
					OP::MeshApplyPoseArgs Args = Program.GetOpArgs<OP::MeshApplyPoseArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.base, Item));
					PushOp(FScheduledOpInline(Args.pose, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_BINDSHAPE:
				{
					OP::MeshBindShapeArgs Args = Program.GetOpArgs<OP::MeshBindShapeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.mesh, Item));
					PushOp(FScheduledOpInline(Args.shape, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_APPLYSHAPE:
				{
					OP::MeshApplyShapeArgs Args = Program.GetOpArgs<OP::MeshApplyShapeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.mesh, Item));
					PushOp(FScheduledOpInline(Args.shape, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_MORPHRESHAPE:
				{
					OP::MeshMorphReshapeArgs Args = Program.GetOpArgs<OP::MeshMorphReshapeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Morph, Item));
					PushOp(FScheduledOpInline(Args.Reshape, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_SETSKELETON:
				{
					OP::MeshSetSkeletonArgs Args = Program.GetOpArgs<OP::MeshSetSkeletonArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));
					PushOp(FScheduledOpInline(Args.Skeleton, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::ME_REMOVEMASK:
				{
					const uint8* Data = Program.GetOpArgsPointer(Item.At);

					OP::ADDRESS Source;
					FMemory::Memcpy(&Source,Data,sizeof(OP::ADDRESS)); 
					Data += sizeof(OP::ADDRESS);

					PushOp(FScheduledOpInline(Source, Item));

					EFaceCullStrategy FaceCullStrategy;
					FMemory::Memcpy(&FaceCullStrategy, Data, sizeof(EFaceCullStrategy));
					Data += sizeof(EFaceCullStrategy);

					uint16 NumRemoves;
					FMemory::Memcpy(&NumRemoves,Data,sizeof(uint16)); 
					Data += sizeof(uint16);

					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));

							for (uint16 Index = 0; Index < NumRemoves; ++Index)
							{
								OP::ADDRESS Condition;
								FMemory::Memcpy(&Condition, Data, sizeof(OP::ADDRESS)); 
								Data += sizeof(OP::ADDRESS);

								if (Condition)
								{
									PushOp(FScheduledOpInline(Condition, Item));								
								}

								OP::ADDRESS Mask;
								FMemory::Memcpy(&Mask, Data,sizeof(OP::ADDRESS)); 
								Data += sizeof(OP::ADDRESS);
							}

							break;
						}

					case 1:
						{
							for (uint16 Index = 0; Index < NumRemoves; ++Index)
							{
								OP::ADDRESS Condition;
								FMemory::Memcpy(&Condition,Data,sizeof(OP::ADDRESS)); 
								Data += sizeof(OP::ADDRESS);
								
								OP::ADDRESS Mask;
								FMemory::Memcpy(&Mask,Data,sizeof(OP::ADDRESS)); 
								Data += sizeof(OP::ADDRESS);

								bool bValue = true;
								if (Condition)
								{
								 	bValue = LoadBool(Condition);
								}
								
								if (bValue)
								{
									PushOp(FScheduledOpInline(Mask, Item));
								}
							}

							StoreNone(Item);
							break;
						}

					default:
						unimplemented()		
					}
					
					break;
				}

			case EOpType::ME_ADDMETADATA:
				{
					const OP::MeshAddMetadataArgs OpArgs = Program.GetOpArgs<OP::MeshAddMetadataArgs>(Item.At);
					PushOp(FScheduledOpInline(OpArgs.Source, Item));

					using OpEnumFlags = OP::MeshAddMetadataArgs::EnumFlags;
					const bool bIsSkeletonList = EnumHasAnyFlags(OpArgs.Flags, OpEnumFlags::IsSkeletonList);

					if (bIsSkeletonList)
					{
						if (Program.ConstantUInt32Lists.IsValidIndex(OpArgs.SkeletonIds.ListAddress))
						{
							ME_SKELETON_ID.Append(Program.ConstantUInt32Lists[OpArgs.SkeletonIds.ListAddress]);
						}
						else
						{
							check(false);
						}
					}
					else
					{
						ME_SKELETON_ID.Push(OpArgs.SkeletonIds.SkeletonId);
					}

					StoreNone(Item);
					break;
				}
				
			case EOpType::ME_PROJECT:
				{
					OP::MeshProjectArgs Args = Program.GetOpArgs<OP::MeshProjectArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Mesh, Item));
					PushOp(FScheduledOpInline(Args.Projector, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::ME_OPTIMIZESKINNING:
				{
					OP::MeshOptimizeSkinningArgs Args = Program.GetOpArgs<OP::MeshOptimizeSkinningArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::ME_TRANSFORMWITHMESH:
				{
					OP::MeshTransformWithinMeshArgs Args = Program.GetOpArgs<OP::MeshTransformWithinMeshArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.sourceMesh, Item));
					PushOp(FScheduledOpInline(Args.boundingMesh, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::ME_TRANSFORMWITHBONE:
				{
					OP::MeshTransformWithBoneArgs Args = Program.GetOpArgs<OP::MeshTransformWithBoneArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.SourceMesh, Item));

					StoreNone(Item);
					break;
				}
			
			case EOpType::IM_REFERENCE:
				{
					OP::ResourceReferenceArgs Args = Program.GetOpArgs<OP::ResourceReferenceArgs>(Item.At);
					IM_REFERENCE_ID.Push(Args.ID);

					StoreNone(Item);
					break;
				}
				
			case EOpType::IM_LAYERCOLOUR:
				{
					OP::ImageLayerColourArgs Args = Program.GetOpArgs<OP::ImageLayerColourArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.base, Item));
					PushOp(FScheduledOpInline(Args.mask, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_LAYER:
				{
					OP::ImageLayerArgs Args = Program.GetOpArgs<OP::ImageLayerArgs>(Item.At);	
					PushOp(FScheduledOpInline(Args.base, Item));
					PushOp(FScheduledOpInline(Args.mask, Item));
					PushOp(FScheduledOpInline(Args.blended, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_MULTILAYER:
				{
					OP::ImageMultiLayerArgs Args = Program.GetOpArgs<OP::ImageMultiLayerArgs>(Item.At);

					switch (Item.Stage)
					{
					case 0:
						{
							PushOp(FScheduledOpInline(Item, 1));
							PushOp(FScheduledOpInline(Args.rangeSize, Item));
							PushOp(FScheduledOpInline(Args.base, Item));

							break;
						}

					case 1:
						{
							int32 NumIterations = 0;
							if (Args.rangeSize)
							{
								const EDataType RangeSizeType = GetOpDataType(Model->GetPrivate()->Program.GetOpType(Args.rangeSize) );
								if (RangeSizeType == EDataType::Int)
								{
									NumIterations = LoadInt(Args.rangeSize);
								}
								else if (RangeSizeType == EDataType::Scalar)
								{
									NumIterations = LoadFloat(Args.rangeSize);
								}
							}

							for (int32 IterationIndex = 0; IterationIndex < NumIterations; ++IterationIndex)
							{
								ExecutionIndex Index = GetMemory().GetRangeIndex(Item.ExecutionIndex);
								Index.SetFromModelRangeIndex(Args.rangeId, IterationIndex);

								FScheduledOpInline ItemCopy = Item;
								ItemCopy.ExecutionIndex = GetMemory().GetRangeIndexIndex(Index);

								PushOp(FScheduledOpInline(Args.mask, Item));
								PushOp(FScheduledOpInline(Args.blended, Item));
							}

							StoreNone(Item);
							break;
						}

					default:
						unimplemented()
					}

					break;
				}

			case EOpType::IM_NORMALCOMPOSITE:
				{
					OP::ImageNormalCompositeArgs Args = Program.GetOpArgs<OP::ImageNormalCompositeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.base, Item));
					PushOp(FScheduledOpInline(Args.normal, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_PIXELFORMAT:
				{
					OP::ImagePixelFormatArgs Args = Program.GetOpArgs<OP::ImagePixelFormatArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_MIPMAP:
				{
					OP::ImageMipmapArgs Args = Program.GetOpArgs<OP::ImageMipmapArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_RESIZE:
				{
					OP::ImageResizeArgs Args = Program.GetOpArgs<OP::ImageResizeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_RESIZELIKE:
				{
					OP::ImageResizeLikeArgs Args = Program.GetOpArgs<OP::ImageResizeLikeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));
					PushOp(FScheduledOpInline(Args.SizeSource, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_RESIZEREL:
				{
					OP::ImageResizeRelArgs Args = Program.GetOpArgs<OP::ImageResizeRelArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_BLANKLAYOUT:
				{
					OP::ImageBlankLayoutArgs Args = Program.GetOpArgs<OP::ImageBlankLayoutArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Layout, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_COMPOSE:
				{
					OP::ImageComposeArgs Args = Program.GetOpArgs<OP::ImageComposeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.base, Item));
					PushOp(FScheduledOpInline(Args.blockImage, Item)); 

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_INTERPOLATE:
				{
					OP::ImageInterpolateArgs Args = Program.GetOpArgs<OP::ImageInterpolateArgs>(Item.At);
					
					for (int32 ImageIndex = 0; ImageIndex < MUTABLE_OP_MAX_INTERPOLATE_COUNT; ++ImageIndex)
					{
						if (!Args.Targets[ImageIndex])
						{
							break;
						}

						PushOp(FScheduledOpInline(Args.Targets[ImageIndex], Item));
					}

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_SATURATE:
				{
					OP::ImageSaturateArgs Args = Program.GetOpArgs<OP::ImageSaturateArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_LUMINANCE:
				{
					OP::ImageLuminanceArgs Args = Program.GetOpArgs<OP::ImageLuminanceArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_SWIZZLE:
				{
					OP::ImageSwizzleArgs Args = Program.GetOpArgs<OP::ImageSwizzleArgs>(Item.At);
				
					TArray<OP::ADDRESS, TFixedAllocator<4>> ValidArgs;
					for (int32 SourceIndex = 0; SourceIndex < 4; ++SourceIndex)
					{
						if (Args.sources[SourceIndex])
						{
							PushOp(FScheduledOpInline(Args.sources[SourceIndex], Item));
						}
					}

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_COLOURMAP:
				{
					OP::ImageColourMapArgs Args = Program.GetOpArgs<OP::ImageColourMapArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_BINARISE:
				{
					OP::ImageBinariseArgs Args = Program.GetOpArgs<OP::ImageBinariseArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_INVERT:
				{
					OP::ImageInvertArgs Args = Program.GetOpArgs<OP::ImageInvertArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_PLAINCOLOUR:
				{
					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_CROP:
				{
					OP::ImageCropArgs Args = Program.GetOpArgs<OP::ImageCropArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_PATCH:
				{
					OP::ImagePatchArgs Args = Program.GetOpArgs<OP::ImagePatchArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.base, Item));
					PushOp(FScheduledOpInline(Args.patch, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_RASTERMESH:
				{
					OP::ImageRasterMeshArgs Args = Program.GetOpArgs<OP::ImageRasterMeshArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.mesh, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::IM_MAKEGROWMAP:
				{
					OP::ImageMakeGrowMapArgs Args = Program.GetOpArgs<OP::ImageMakeGrowMapArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.mask, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_DISPLACE:
				{
					OP::ImageDisplaceArgs Args = Program.GetOpArgs<OP::ImageDisplaceArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::IM_TRANSFORM:
				{
					OP::ImageTransformArgs Args = Program.GetOpArgs<OP::ImageTransformArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::LA_MERGE:
				{
					OP::LayoutMergeArgs Args = Program.GetOpArgs<OP::LayoutMergeArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Base, Item));
					PushOp(FScheduledOpInline(Args.Added, Item));

					StoreNone(Item);
					break;
				}
	
			case EOpType::LA_PACK:
				{
					OP::LayoutPackArgs Args = Program.GetOpArgs<OP::LayoutPackArgs>(Item.At);
					PushOp(FScheduledOpInline( Args.Source, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::LA_FROMMESH:
				{
					OP::LayoutFromMeshArgs Args = Program.GetOpArgs<OP::LayoutFromMeshArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Mesh, Item));

					StoreNone(Item);
					break;
				}

			case EOpType::LA_REMOVEBLOCKS:
				{
					OP::LayoutRemoveBlocksArgs Args = Program.GetOpArgs<OP::LayoutRemoveBlocksArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Source, Item));
					PushOp(FScheduledOpInline(Args.ReferenceLayout, Item));

					StoreNone(Item);
					break;
				}
			
			case EOpType::NONE:
				{
					check(Item.At == 0);
					
					StoreNone(Item);
					break;
				}

			case EOpType::MI_CONSTANT:
				{
					OP::ResourceConstantArgs Args = Program.GetOpArgs<OP::ResourceConstantArgs>(Item.At);
					TSharedPtr<const FMaterial> SourceConst = Program.ConstantMaterials[Args.value];
					MI_REFERENCE_ID.Add(SourceConst->ReferenceID);

					StoreNone(Item);
					break;
				}

			case EOpType::IM_MATERIAL_BREAK:
			case EOpType::SC_MATERIAL_BREAK:
			case EOpType::CO_MATERIAL_BREAK:
				{
					OP::MaterialBreakArgs Args = Program.GetOpArgs<OP::MaterialBreakArgs>(Item.At);
					PushOp(FScheduledOpInline(Args.Material, Item));

					StoreNone(Item);
					break;
				}
				
			default:
				unimplemented();
			}
		}

		check(Stack.Num() <= 1);

		return Stack.Num() ? LoadInstance(Root) : MakeShared<FInstance>();
	}
}
