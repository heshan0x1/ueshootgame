// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace UE::UAF
{
	class IAbstractSkeletonEditor;
}

namespace UE::UAF::Labels
{
	class ILabelBindingWidget;
	class ILabelSkeletonTreeWidget;

	class ILabelsTab
	{
	public:
		virtual ~ILabelsTab() = default;
		
		virtual TSharedPtr<ILabelBindingWidget> GetLabelBindingWidget() const = 0;

		virtual TSharedPtr<ILabelSkeletonTreeWidget> GetLabelSkeletonTreeWidget() const = 0;

		virtual void RepopulateLabelData() = 0;

		virtual TWeakPtr<IAbstractSkeletonEditor> GetAbstractSkeletonEditor() const = 0;
	};

}