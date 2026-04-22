// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEngineUtil.h"

#include "Dataflow/DataflowAnyType.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowTypePolicy.h"
#include "ReferenceSkeleton.h"
#include "UObject/UnrealType.h"

namespace UE::Dataflow
{

	namespace Animation
	{
		void GlobalTransformsInternal(int32 Index, const FReferenceSkeleton& Ref, TArray<FTransform>& Mat, TArray<bool>& Visited)
		{
			if (!Visited[Index])
			{
				const TArray<FTransform>& RefMat = Ref.GetRefBonePose();

				int32 ParentIndex = Ref.GetParentIndex(Index);
				if (ParentIndex != INDEX_NONE && ParentIndex != Index) // why self check?
				{
					GlobalTransformsInternal(ParentIndex, Ref, Mat, Visited);
					Mat[Index].SetFromMatrix(RefMat[Index].ToMatrixWithScale() * Mat[ParentIndex].ToMatrixWithScale());
				}
				else
				{
					Mat[Index] = RefMat[Index];
				}

				Visited[Index] = true;
			}
		}

		void GlobalTransforms(const FReferenceSkeleton& Ref, TArray<FTransform>& Mat)
		{
			TArray<bool> Visited;
			Visited.Init(false, Ref.GetNum());
			Mat.SetNum(Ref.GetNum());

			int32 Index = Ref.GetNum() - 1;
			while (Index >= 0)
			{
				GlobalTransformsInternal(Index, Ref, Mat, Visited);
				Index--;
			}
		}
	}

	namespace Color
	{
		FLinearColor GetRandomColor(const int32 RandomSeed, int32 Idx)
		{
			FRandomStream RandomStream(RandomSeed * 7 + Idx * 41);

			const uint8 R = static_cast<uint8>(RandomStream.FRandRange(128, 255));
			const uint8 G = static_cast<uint8>(RandomStream.FRandRange(128, 255));
			const uint8 B = static_cast<uint8>(RandomStream.FRandRange(128, 255));

			return FLinearColor(FColor(R, G, B, 255));
		}
	}

	namespace Type
	{
		FPropertyBagPropertyDesc GetPropertyBagPropertyDescFromDataflowType(const FName Name, const FName Type)
		{
			if (Type == UE::Dataflow::GetTypeName<bool>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Bool);
			}
			else if (Type == UE::Dataflow::GetTypeName<int32>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Int32);
			}
			else if (Type == UE::Dataflow::GetTypeName<int64>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Int64);
			}
			else if (Type == UE::Dataflow::GetTypeName<uint32>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::UInt32);
			}
			else if (Type == UE::Dataflow::GetTypeName<uint64>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::UInt64);
			}
			else if (Type == UE::Dataflow::GetTypeName<float>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Float);
			}
			else if (Type == UE::Dataflow::GetTypeName<double>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Double);
			}
			else if (Type == UE::Dataflow::GetTypeName<FName>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Name);
			}
			else if (Type == UE::Dataflow::GetTypeName<FString>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::String);
			}
			else if (Type == UE::Dataflow::GetTypeName<FText>())
			{
				return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Text);
			}
			else
			{
				const FString TypeStr(Type.ToString());

				// Make sure to remove the TObjectPtr
				FString ObjectPtrInnerType;
				if (FDataflowUObjectConvertibleTypePolicy::GetObjectPtrInnerType(TypeStr, ObjectPtrInnerType))
				{
					if (const UClass* ObjectClass = FindFirstObjectSafe<UClass>(*ObjectPtrInnerType))
					{
						return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Object, ObjectClass);
					}
				}
				if (TypeStr.StartsWith("U"))
				{
					const FString ShortTypeName = TypeStr.RightChop(1);
					if (const UClass* ObjectClass = FindFirstObjectSafe<UClass>(*ShortTypeName))
					{
						return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Object, ObjectClass);
					}
				}
				else if (TypeStr.StartsWith("F"))
				{
					const FString ShortTypeName = TypeStr.RightChop(1);
					if (const UScriptStruct* ScriptStruct = FindFirstObjectSafe<UScriptStruct>(*ShortTypeName))
					{
						return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, ScriptStruct);
					}
				}
				else if (FDataflowArrayTypePolicy::SupportsTypeStatic(Type))
				{
					const FName ElementType = FDataflowArrayTypePolicy::GetElementType(Type);
					FPropertyBagPropertyDesc PropertyDesc = GetPropertyBagPropertyDescFromDataflowType(Name, ElementType);
					PropertyDesc.ContainerTypes = FPropertyBagContainerTypes{ EPropertyBagContainerType::Array };
					return PropertyDesc;
				}
			}
			// invalid value 
			return FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Count);
		}

		FPropertyBagPropertyDesc GetPropertyBagPropertyDescFromDataflowConnection(const FDataflowConnection& Connection)
		{
			const FProperty* ConnectionProperty = Connection.GetProperty();
			check(ConnectionProperty);

			// AnyType require special treatment to make sure we give a concrete type
			if (Connection.IsAnyType())
			{
				if (Connection.HasConcreteType())
				{
					const FPropertyBagPropertyDesc Desc = GetPropertyBagPropertyDescFromDataflowType(Connection.GetName(), Connection.GetType());
					if (Desc.ValueType != EPropertyBagPropertyType::Count)
					{
						return Desc;
					}
				}
				// Fallback let's use the AnyType default (Value) type
				if (ConnectionProperty->GetClass()->IsChildOf(FStructProperty::StaticClass()))
				{
					if (const FStructProperty* StructProperty = CastField<FStructProperty>(ConnectionProperty))
					{
						if (const bool bInheritsFromAnyType = StructProperty->Struct->IsChildOf<FDataflowAnyType>())
						{
							for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct); FieldIt; ++FieldIt)
							{
								FProperty* Prop = *FieldIt;
								if (Prop->GetFName() == "Value")
								{
									return FPropertyBagPropertyDesc(Connection.GetName(), Prop);
								}
							}
						}
					}
				}
			}
			// default simply set property desc from the property
			return FPropertyBagPropertyDesc(Connection.GetName(), Connection.GetProperty());
		}

		FName MakeUniqueNameForPropertyBag(FName OriginalName, const FInstancedPropertyBag& PropertyBag)
		{
			FName NewName = OriginalName;
			int32 SuffixNumber = 1;
			while (PropertyBag.FindPropertyDescByName(NewName))
			{
				NewName.SetNumber(SuffixNumber++);
			}
			return NewName;
		}
	}
}

