// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterComponentBindingCustomization.h"

#include "Customizations/NiagaraParameterBindingAdapterCustomization.h"
#include "NiagaraParameterComponentBinding.h"

namespace NiagaraFloatParameterComponentBindingCustomizationPrivate
{
	class FFloatAdapter : public FNiagaraParameterBindingAdapter
	{
	public:
		explicit FFloatAdapter(uint8* ObjectAddress)
		{
			check(ObjectAddress != nullptr);
			ParameterBinding = reinterpret_cast<FNiagaraFloatParameterComponentBinding*>(ObjectAddress);
		}

		virtual bool IsSetToParameter() const override
		{
			return ParameterBinding->ResolvedParameter.IsValid();
		}

		virtual bool AllowConstantValue() const override
		{
			return true;
		}

		virtual FNiagaraTypeDefinition GetConstantTypeDef() const override
		{
			return ParameterBinding->GetTypeDef();
		}

		virtual TConstArrayView<uint8> GetConstantValue() const override
		{
			return MakeArrayView(reinterpret_cast<uint8*>(&ParameterBinding->DefaultValue), sizeof(ParameterBinding->DefaultValue));
		}

		virtual void SetConstantValue(TConstArrayView<uint8> Memory) const override
		{
			FMemory::Memcpy(&ParameterBinding->DefaultValue, Memory.GetData(), sizeof(ParameterBinding->DefaultValue));
		}

		virtual const FNiagaraVariableBase& GetBoundParameter() const override
		{
			return ParameterBinding->DisplayParameter;
		}

		virtual void SetBoundParameter(const FInstancedStruct& Parameter) override
		{
			if (Parameter.IsValid())
			{
				const FNiagaraFloatParameterComponentBinding& BindingData = Parameter.Get<FNiagaraFloatParameterComponentBinding>();
				ParameterBinding->AliasedParameter = BindingData.AliasedParameter;
				ParameterBinding->ResolvedParameter = BindingData.ResolvedParameter;
				ParameterBinding->DisplayParameter = BindingData.DisplayParameter;
				ParameterBinding->Component = BindingData.Component;
			}
			else
			{
				ParameterBinding->AliasedParameter = FNiagaraVariableBase();
				ParameterBinding->ResolvedParameter = FNiagaraVariableBase();
				ParameterBinding->DisplayParameter = FNiagaraVariableBase();
			}
		}

		virtual bool IsSetToDefault() const override
		{
			FNiagaraFloatParameterComponentBinding DefaultValue;
			return
				ParameterBinding->ResolvedParameter == DefaultValue.ResolvedParameter && 
				ParameterBinding->AliasedParameter == DefaultValue.AliasedParameter &&
				ParameterBinding->DisplayParameter == DefaultValue.DisplayParameter &&
				ParameterBinding->DefaultValue == DefaultValue.DefaultValue;
		}

		virtual void SetToDefault() override
		{
			FNiagaraFloatParameterComponentBinding DefaultValue;
			ParameterBinding->ResolvedParameter = DefaultValue.ResolvedParameter;
			ParameterBinding->AliasedParameter = DefaultValue.AliasedParameter;
			ParameterBinding->DisplayParameter = DefaultValue.DisplayParameter;
			ParameterBinding->DefaultValue = DefaultValue.DefaultValue;
		}

		virtual void CollectBindings(const FNiagaraVariableBase& AliasedVariable, const FNiagaraVariableBase& ResolvedVariable, TArray<TPair<FName, FInstancedStruct>>& OutBindings) const override
		{
			const FNiagaraTypeDefinition TypeDef = AliasedVariable.GetType();

			int32 NumFloats = 0;
			const TCHAR* ComponentNames = nullptr;
			if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
			{
				NumFloats = 1;
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
			{
				NumFloats = 2;
				ComponentNames = TEXT("XY");
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def() || TypeDef == FNiagaraTypeDefinition::GetPositionDef())
			{
				NumFloats = 3;
				ComponentNames = TEXT("XYZ");
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetVec4Def() || TypeDef == FNiagaraTypeDefinition::GetQuatDef())
			{
				NumFloats = 4;
				ComponentNames = TEXT("XYZW");
			}
			else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
			{
				NumFloats = 4;
				ComponentNames = TEXT("RGBA");
			}

			for (int32 i = 0; i < NumFloats; ++i)
			{
				TPair<FName, FInstancedStruct>& BindingData = OutBindings.AddDefaulted_GetRef();
				BindingData.Key = AliasedVariable.GetName();
				if (ComponentNames != nullptr)
				{
					FNameBuilder NameBuilder(BindingData.Key);
					NameBuilder.AppendChar('.');
					NameBuilder.AppendChar(ComponentNames[i]);
					BindingData.Key = FName(NameBuilder.ToString());
				}

				FNiagaraFloatParameterComponentBinding& Binding = BindingData.Value.InitializeAs<FNiagaraFloatParameterComponentBinding>();
				Binding.AliasedParameter = AliasedVariable;
				Binding.ResolvedParameter = ResolvedVariable;
				Binding.DisplayParameter = FNiagaraVariableBase(AliasedVariable.GetType(), BindingData.Key);
				Binding.Component = i;
			}
		}

		virtual TConstArrayView<FNiagaraTypeDefinition> GetAllowedTypeDefinitions() const override
		{
			static const FNiagaraTypeDefinition AllowedTypes[] =
			{
				FNiagaraTypeDefinition::GetFloatDef(),
				FNiagaraTypeDefinition::GetVec2Def(),
				FNiagaraTypeDefinition::GetVec3Def(),
				FNiagaraTypeDefinition::GetVec4Def(),
				FNiagaraTypeDefinition::GetQuatDef(),
				FNiagaraTypeDefinition::GetColorDef(),
				FNiagaraTypeDefinition::GetPositionDef(),
			};
			return MakeArrayView(AllowedTypes);
		}

		FNiagaraFloatParameterComponentBinding* ParameterBinding = nullptr;
	};

	static TUniquePtr<FNiagaraParameterBindingAdapter> CreateFloatAdapter(uint8* ObjectAddress)
	{
		return MakeUnique<FFloatAdapter>(ObjectAddress);
	}
};

TSharedRef<IPropertyTypeCustomization> FNiagaraFloatParameterComponentBindingCustomization::MakeInstance()
{
	return MakeShared<FNiagaraParameterBindingAdapterCustomization>(&NiagaraFloatParameterComponentBindingCustomizationPrivate::CreateFloatAdapter);
}
