///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "ILiveLinkSource.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "IMessageContext.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "LoledUtilities.h"
#include "Delegates/IDelegateInstance.h"
#include "SyncrolinkSubjectSettings.h" 

//enable logging step 1
DECLARE_LOG_CATEGORY_EXTERN(ModuleLog, Log, All)

class FRunnableThread;
class FSocket;
class ILiveLinkClient;
class ISocketSubsystem;


class LONET2LIVELINK_API FLONET2LiveLinkSource : public ILiveLinkSource, public FRunnable
{
public:

	FLONET2LiveLinkSource(FIPv4Endpoint Endpoint);


	virtual ~FLONET2LiveLinkSource();

	// Begin ILiveLinkSource Interface
	USyncrolinkSubjectSettings* SavedSourceSettings = nullptr;
	virtual TSubclassOf<ULiveLinkSourceSettings> GetSettingsClass() const override { return USyncrolinkSubjectSettings::StaticClass(); }
	virtual void OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;

	virtual bool IsSourceStillValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override { return SourceType; };
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override { return SourceStatus; }
	virtual void InitializeSettings(ULiveLinkSourceSettings* Settings) override;

	// End ILiveLinkSource Interface

	// Begin FRunnable Interface

	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	void Start();
	virtual void Stop() override;
	virtual void Exit() override { }
	// Deferred start delegate handle.
	FDelegateHandle DeferredStartDelegateHandle;
	// End FRunnable Interface

	void HandleReceivedData(TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> ReceivedData);

	FTimecode TimeCode;
	FFrameRate FrameRate;

private:

	ILiveLinkClient* Client;

	// Our identifier in LiveLink
	FGuid SourceGuid;

	FMessageAddress ConnectionAddress;

	FText SourceType;
	FText SourceMachineName;
	FText SourceStatus;

	FIPv4Endpoint DeviceEndpoint;

	// Socket to receive data on
	FSocket* Socket;

	// Subsystem associated to Socket
	ISocketSubsystem* SocketSubsystem;

	// Threadsafe Bool for terminating the main thread loop
	FThreadSafeBool Stopping;
	FThreadSafeBool Stopped;
	// Thread to run socket operations on
	FRunnableThread* Thread;

	// Name of the sockets thread
	FString ThreadName;

	// Time to wait between attempted receives
	FTimespan WaitTime;

	// List of subjects we've already encountered
	TSet<FName> EncounteredSubjects;

	// Buffer to receive socket data into
	TArray<uint8> RecvBuffer;
};
