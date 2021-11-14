///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "LiveLinkSourceFactory.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "LONET2LiveLinkSourceFactory.generated.h"

class SLONET2LiveLinkSourceEditor;

UCLASS()
class ULONET2LiveLinkSourceFactory : public ULiveLinkSourceFactory
{
public:

	GENERATED_BODY()

	virtual FText GetSourceDisplayName() const override;
	virtual FText GetSourceTooltip() const override;

	virtual EMenuType GetMenuType() const override { return EMenuType::SubPanel; }
	virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const override;
	TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const override;
private:
	void OnOkClicked(FIPv4Endpoint Endpoint, FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const;
};