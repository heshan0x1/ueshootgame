// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanVideoBaseLiveLinkSubjectSettings.h"
#include "MetaHumanLocalLiveLinkSubjectMonitorWidget.h"
#include "SMetaHumanImageViewer.h"

#include "Pipeline/PipelineData.h"

#include "Widgets/SCompoundWidget.h"
#include "Engine/TimerHandle.h"



class METAHUMANLOCALLIVELINKSOURCE_API SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget : public SCompoundWidget, public FGCObject
{
public:

	SLATE_BEGIN_ARGS(SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget) {}
	SLATE_END_ARGS()

	virtual ~SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget();

	void Construct(const FArguments& InArgs, UMetaHumanVideoBaseLiveLinkSubjectSettings* InSettings, bool bInAllowResize);

	//~Begin SWidget interface
	virtual FVector2D ComputeDesiredSize(float InLayoutScaleMultiplier) const override;
	//~End SWidget interface

	//~Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	virtual FString GetReferencerName() const override;
	//~End FGCObject interface

private:

	UMetaHumanVideoBaseLiveLinkSubjectSettings* Settings = nullptr;

	void OnUpdate(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

	// 2D Image review window
	TSharedPtr<SMetaHumanImageViewer> ImageViewer;
	FSlateBrush ImageViewerBrush;
	TObjectPtr<UTexture2D> ImageTexture;

	int32 Dropped = -1;
	int32 DroppedCount = -1;
	double DroppedStart = 0;

	void FillTexture(const UE::MetaHuman::Pipeline::FUEImageDataType& InImage);
	void ClearTexture();

	FTimerHandle EditorTimerHandle;
};
