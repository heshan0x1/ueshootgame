// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticBitArray.h"
#include "Customizations/NiagaraStackObjectPropertyCustomization.h"

#define UE_API NIAGARAEDITOR_API

class FNiagaraEmitterHandleViewModel;
class FNiagaraEmitterInstance;
class UNiagaraStatelessEmitter;
class UNiagaraStackPropertyRow;
class UMaterial;
class FDetailTreeNode;
struct FExpressionInput;


class FNiagaraStackObjectPropertyCustomization_StatelessModule_DynamicMaterialParameters
	: public FNiagaraStackObjectPropertyCustomization
{
public:
	UE_API FNiagaraStackObjectPropertyCustomization_StatelessModule_DynamicMaterialParameters();

	static UE_API TSharedRef<FNiagaraStackObjectPropertyCustomization> MakeInstance();
	
	UE_API virtual TOptional<TSharedPtr<SWidget>> GenerateNameWidget(UNiagaraStackPropertyRow* PropertyRow) const override;

private:
	UE_API TOptional<FText> TryGetDisplayNameForDynamicMaterialParameter(TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel, int32 ParameterIndex, int32 ParameterChannel) const;
	
	UE_API void GetChannelUsedBitMask(FExpressionInput* Input, TStaticBitArray<4>& ChannelUsedMask) const;
	UE_API TArray<UMaterial*> GetMaterialsFromEmitter(const UNiagaraStatelessEmitter& InEmitter, const FNiagaraEmitterInstance* InEmitterInstance) const;
	UE_API void GetParameterIndexAndChannel(TSharedRef<FDetailTreeNode> DetailTreeNode, int32& OutParameterIndex, int32& OutParameterChannel) const;

private:
	TMap<FName, int32> ParameterIndexMap;
	TMap<FName, int32> ParameterChannelMap;
};

#undef UE_API
