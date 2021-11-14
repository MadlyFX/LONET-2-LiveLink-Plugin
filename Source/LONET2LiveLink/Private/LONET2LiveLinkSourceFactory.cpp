///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC
#include "LONET2LiveLinkSourceFactory.h"
#include "LONET2LiveLinkSource.h"
#include "SLONET2LiveLinkSourceFactory.h"

#define LOCTEXT_NAMESPACE "LONET2LiveLinkSourceFactory"

FText ULONET2LiveLinkSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("SourceDisplayName", "LONET 2 LiveLink");
}

FText ULONET2LiveLinkSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("SourceTooltip", "Creates a connection to a LONET 2 UDP Stream");
}

TSharedPtr<SWidget> ULONET2LiveLinkSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated InOnLiveLinkSourceCreated) const
{
	return SNew(SLONET2LiveLinkSourceFactory)
		.OnOkClicked(SLONET2LiveLinkSourceFactory::FOnOkClicked::CreateUObject(this, &ULONET2LiveLinkSourceFactory::OnOkClicked, InOnLiveLinkSourceCreated));
}

TSharedPtr<ILiveLinkSource> ULONET2LiveLinkSourceFactory::CreateSource(const FString& InConnectionString) const
{
	FIPv4Endpoint DeviceEndPoint;
	if (!FIPv4Endpoint::Parse(InConnectionString, DeviceEndPoint))
	{
		return TSharedPtr<ILiveLinkSource>();
	}

	return MakeShared<FLONET2LiveLinkSource>(DeviceEndPoint);
}

void ULONET2LiveLinkSourceFactory::OnOkClicked(FIPv4Endpoint InEndpoint, FOnLiveLinkSourceCreated InOnLiveLinkSourceCreated) const
{
	InOnLiveLinkSourceCreated.ExecuteIfBound(MakeShared<FLONET2LiveLinkSource>(InEndpoint), InEndpoint.ToString());
}

#undef LOCTEXT_NAMESPACE