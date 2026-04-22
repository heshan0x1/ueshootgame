// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extensions/Text3DDefaultTokenExtension.h"

#include "Internationalization/TextFormatter.h"
#include "Text3DComponent.h"
#include "Tokens/Text3DTokenBase.h"

#if WITH_EDITOR
void UText3DDefaultTokenExtension::PostEditUndo()
{
	Super::PostEditUndo();
	RequestUpdate(EText3DRendererFlags::All, /** Immediate */false);
}

void UText3DDefaultTokenExtension::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	const FName MemberPropertyName = InPropertyChangedEvent.GetMemberPropertyName();

	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UText3DDefaultTokenExtension, Tokens))
	{
		if (InPropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear
			|| InPropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove
			|| InPropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayMove)
		{
			RequestUpdate(EText3DRendererFlags::All, /** Immediate */false);
		}
	}
}
#endif

const FText& UText3DDefaultTokenExtension::GetFormattedText() const
{
	return FormattedText;
}

EText3DExtensionResult UText3DDefaultTokenExtension::PreRendererUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters)
{
	const UText3DComponent* Text3DComponent = GetText3DComponent();
	FormattedText = Text3DComponent->GetText();

	FFormatNamedArguments NamedArguments;
	NamedArguments.Reserve(Tokens.Num() * 2);
	for (UText3DTokenBase* Token : Tokens)
	{
		if (Token)
		{
			Token->CollectTokens(NamedArguments);
		}
	}

	constexpr bool bRebuildText = false;
	constexpr bool bRebuildAsSource = false;
	FormattedText = FTextFormatter::Format(MoveTemp(FormattedText), MoveTemp(NamedArguments), bRebuildText, bRebuildAsSource);

	// For backward compatibility
	Text3DComponent->FormatText(FormattedText);

	return EText3DExtensionResult::Finished;
}

EText3DExtensionResult UText3DDefaultTokenExtension::PostRendererUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters)
{
	return EText3DExtensionResult::Active;
}
