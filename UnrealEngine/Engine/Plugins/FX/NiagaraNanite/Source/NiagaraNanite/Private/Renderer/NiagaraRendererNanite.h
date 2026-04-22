// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraRenderer.h"

class UNiagaraStaticMeshComponent;
class UStaticMesh;

class FNiagaraRendererNanite : public FNiagaraRenderer
{
	struct FMeshData
	{
		TWeakObjectPtr<UNiagaraStaticMeshComponent>	MeshComponent;
		FVector3f									Scale = FVector3f::OneVector;
		FQuat4f										Rotation = FQuat4f::Identity;
		//FVector3f									PivotOffset = FVector3f::ZeroVector;
		TArray<uint32, TInlineAllocator<4>>			MaterialRemapTable;
	};

public:
	explicit FNiagaraRendererNanite(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties* InProps, const FNiagaraEmitterInstance* Emitter);
	~FNiagaraRendererNanite();

	void Initialize(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* InEmitter, const FNiagaraSystemInstanceController& InController) override;

	void ReleaseComponents();

	// FNiagaraRenderer interface BEGIN
	virtual void PostSystemTick_GameThread(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) override;
	virtual void OnSystemComplete_GameThread(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) override;
	// FNiagaraRenderer interface END

	static int32 GetMaxCustomFloats();

private:
	int32 MeshIndexComponentOffset = INDEX_NONE;
	int32 RendererVisComponentOffset = INDEX_NONE;

	TArray<FMeshData, TInlineAllocator<4>>	MeshDatas;
};
