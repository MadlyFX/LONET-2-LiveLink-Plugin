///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "ILiveLinkSource.h"
#include "HAL/ThreadSafeBool.h"
#include "IMessageContext.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "LoledUtilities.h"
#include "Delegates/IDelegateInstance.h"
#include "Common/UdpSocketReceiver.h"

//enable logging step 1
DECLARE_LOG_CATEGORY_EXTERN(ModuleLog, Log, All)

class FSocket;
class ILiveLinkClient;
class ISocketSubsystem;

class LONET2LIVELINK_API FLONET2LiveLinkSource : public ILiveLinkSource
{
public:

	FLONET2LiveLinkSource(FIPv4Endpoint Endpoint);

	virtual ~FLONET2LiveLinkSource();

	// Begin ILiveLinkSource Interface
	
	virtual void OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;

	virtual bool IsSourceStillValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override { return SourceType; };
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override { return SourceStatus; }
	virtual void InitializeSettings(ULiveLinkSourceSettings* Settings) override;
	virtual void Update() override;

	// End ILiveLinkSource Interface

	void HandleReceivedData(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& Sender);

	FTimecode TimeCode;
	FFrameRate FrameRate;

private:


	void OnEnginePreExit();


	bool OpenSocket();


	void CloseSockets();

	void ProcessJsonData(const TArray<uint8>& RawData);

	ILiveLinkClient* Client = nullptr;

	FGuid SourceGuid;

	FText SourceType;
	FText SourceMachineName;
	FText SourceStatus;

	FIPv4Endpoint DeviceEndpoint;

	FSocket* Socket = nullptr;

	TUniquePtr<FUdpSocketReceiver> UdpReceiver;

	FThreadSafeBool bShutdownRequested;


	TSet<FName> EncounteredSubjects;
};