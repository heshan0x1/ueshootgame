// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowPreviewProfileController.h"

class FDataflowConstructionScene;
class FDataflowSimulationScene;

class FDataflowConstructionSceneProfileIndexStorage : public FDataflowPreviewProfileController::IProfileIndexStorage
{
public:

	explicit FDataflowConstructionSceneProfileIndexStorage(TWeakPtr<FDataflowConstructionScene> InConstructionScene);
	virtual ~FDataflowConstructionSceneProfileIndexStorage() override = default;

	virtual void StoreProfileIndex(int32 Index) override;
	virtual int32 RetrieveProfileIndex() override;

private:

	TWeakPtr<FDataflowConstructionScene> ConstructionScene;
};

class FDataflowSimulationSceneProfileIndexStorage : public FDataflowPreviewProfileController::IProfileIndexStorage
{
public:

	explicit FDataflowSimulationSceneProfileIndexStorage(TWeakPtr<FDataflowSimulationScene> InSimulationScene);
	virtual ~FDataflowSimulationSceneProfileIndexStorage() override = default;

	virtual void StoreProfileIndex(int32 Index) override;
	virtual int32 RetrieveProfileIndex() override;

private:

	TWeakPtr<FDataflowSimulationScene> SimulationScene;
};

