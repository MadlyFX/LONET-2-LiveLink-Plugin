///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Input/Reply.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SEditableTextBox;

class SLONET2LiveLinkSourceFactory : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnOkClicked, FIPv4Endpoint);

	SLATE_BEGIN_ARGS(SLONET2LiveLinkSourceFactory){}
		SLATE_EVENT(FOnOkClicked, OnOkClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

private:

	void OnEndpointChanged(const FText& NewValue, ETextCommit::Type);

	FReply OnOkClicked();

	TWeakPtr<SEditableTextBox> EditabledText;
	FOnOkClicked OkClicked;
};