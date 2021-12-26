////COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#include "LONET2LiveLinkSource.h"

#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "Json.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkLightRole.h"
#include "Roles/LiveLinkLightTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "LiveLinkLensRole.h"
#include "LiveLinkLensTypes.h"
#include "LiveLinkLensController.h"

//enable logging step 2
DEFINE_LOG_CATEGORY(ModuleLog)

#define LOCTEXT_NAMESPACE "LONET2LiveLinkSource"

#define RECV_BUFFER_SIZE 1024 * 1024

FLONET2LiveLinkSource::FLONET2LiveLinkSource(FIPv4Endpoint InEndpoint)
: Socket(nullptr)
, Stopping(false)
, Thread(nullptr)
, WaitTime(FTimespan::FromMilliseconds(100))
{
	// defaults
	DeviceEndpoint = InEndpoint;

	SourceStatus = LOCTEXT("SourceStatus_DeviceNotFound", "Device Not Found");
	SourceType = LOCTEXT("LONET2LiveLinkSourceType", "LONET 2 LiveLink");
	SourceMachineName = LOCTEXT("LONET2LiveLinkSourceMachineName", "localhost");

	UE_LOG(ModuleLog, Warning, TEXT("Setup socket"));
	//setup socket
	if (DeviceEndpoint.Address.IsMulticastAddress())
	{
		Socket = FUdpSocketBuilder(TEXT("LONET2SOCKET"))
			.AsNonBlocking()
			.AsReusable()
			.BoundToPort(DeviceEndpoint.Port)
			.WithReceiveBufferSize(RECV_BUFFER_SIZE)

			.BoundToAddress(FIPv4Address::Any)
			.JoinedToGroup(DeviceEndpoint.Address)
			.WithMulticastLoopback()
			.WithMulticastTtl(2);
					
	}
	else
	{
		Socket = FUdpSocketBuilder(TEXT("LONET2SOCKET"))
			.AsNonBlocking()
			.AsReusable()
			.BoundToAddress(DeviceEndpoint.Address)
			.BoundToPort(DeviceEndpoint.Port)
			.WithReceiveBufferSize(RECV_BUFFER_SIZE);
	}

	RecvBuffer.SetNumUninitialized(RECV_BUFFER_SIZE);

	if ((Socket != nullptr) && (Socket->GetSocketType() == SOCKTYPE_Datagram))
	{
		SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

		Start();

		SourceStatus = LOCTEXT("SourceStatus_Receiving", "Receiving");
	}
}

FLONET2LiveLinkSource::~FLONET2LiveLinkSource()
{
	Stop();
	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	if (Socket != nullptr)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
	}

}

void FLONET2LiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
}


bool FLONET2LiveLinkSource::IsSourceStillValid() const
{
	// Source is valid if we have a valid thread and socket
	bool bIsSourceValid = !Stopping && Thread != nullptr && Socket != nullptr;
	return bIsSourceValid;
}


bool FLONET2LiveLinkSource::RequestSourceShutdown()
{
	Stop();

	return true;
}
// FRunnable interface

void FLONET2LiveLinkSource::Start()
{
	ThreadName = "LONET2 UDP Receiver ";
	ThreadName.AppendInt(FAsyncThreadIndex::GetNext());
	
	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
}

void FLONET2LiveLinkSource::Stop()
{
	Stopping = true;
}

uint32 FLONET2LiveLinkSource::Run()
{
	TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
	
	while (!Stopping)
	{
		if (Socket->Wait(ESocketWaitConditions::WaitForRead, WaitTime))
		{
			uint32 Size;

			while (Socket->HasPendingData(Size))
			{
				int32 Read = 0;

				if (Socket->RecvFrom(RecvBuffer.GetData(), RecvBuffer.Num(), Read, *Sender))
				{
					if (Read > 0)
					{
						TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> ReceivedData = MakeShareable(new TArray<uint8>());
						ReceivedData->SetNumUninitialized(Read);
						memcpy(ReceivedData->GetData(), RecvBuffer.GetData(), Read);
						AsyncTask(ENamedThreads::GameThread, [this, ReceivedData]() { HandleReceivedData(ReceivedData); });
					}
				}
			}
		}
	}
	return 0;
}

void FLONET2LiveLinkSource::HandleReceivedData(TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> ReceivedData)
{
	//UE_LOG(ModuleLog, Warning, TEXT("HandleReceiveData"));
	FString JsonString;
	JsonString.Empty(ReceivedData->Num());
	for (uint8& Byte : *ReceivedData.Get())
	{
		JsonString += TCHAR(Byte);
	}
	
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{

		//Encoders
		const TSharedPtr<FJsonObject> *EncoderObject;
		bool bHasEncoderData = JsonObject->TryGetObjectField("encoder_data", EncoderObject);
		if (bHasEncoderData) {//Pure encoder data should come in as camera data
			FString tmpName = EncoderObject->Get()->GetStringField("cameraName") + " Lens";
			FName SubjectName(tmpName);

			if (!EncounteredSubjects.Contains(SubjectName)) {
				FLiveLinkStaticDataStruct StaticDataStruct = FLiveLinkStaticDataStruct(FLiveLinkCameraStaticData::StaticStruct());
				FLiveLinkCameraStaticData& CameraData = *StaticDataStruct.Cast<FLiveLinkCameraStaticData>();
				CameraData.bIsAspectRatioSupported = false;
				CameraData.bIsFieldOfViewSupported = false;
				CameraData.bIsFocalLengthSupported = true;
				CameraData.bIsApertureSupported = true;
				CameraData.bIsFocusDistanceSupported = true;
				CameraData.bIsLocationSupported = false;
				CameraData.bIsScaleSupported = false;
				CameraData.bIsRotationSupported = false;
				
				Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName }, ULiveLinkCameraRole::StaticClass(), MoveTemp(StaticDataStruct));
				EncounteredSubjects.Add(SubjectName);
			}

			FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkCameraFrameData::StaticStruct());
			FLiveLinkCameraFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkCameraFrameData>();

			double focalLengthRaw, focalLengthMapped, irisRaw, irisMapped, focusRaw, focusMapped, frameRate;
			bool dropFrame = false;
			EncoderObject->Get()->TryGetNumberField(TEXT("focalLengthRaw"), focalLengthRaw);
			EncoderObject->Get()->TryGetNumberField(TEXT("focalLengthMapped"), focalLengthMapped);
			EncoderObject->Get()->TryGetNumberField(TEXT("irisRaw"), irisRaw);
			EncoderObject->Get()->TryGetNumberField(TEXT("irisMapped"), irisMapped);
			EncoderObject->Get()->TryGetNumberField(TEXT("focusRaw"), focusRaw);
			EncoderObject->Get()->TryGetNumberField(TEXT("focusMapped"), focusMapped);
			EncoderObject->Get()->TryGetNumberField(TEXT("frameRate"), frameRate);
			FString timecodeToSplit;
			EncoderObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit);
			TArray<FString> splitDataTC;
			timecodeToSplit.ParseIntoArray(splitDataTC, TEXT(":"), true);

			if (splitDataTC.Num() != 4) {
				UE_LOG(LogTemp, Warning, TEXT("TC Packet %s \n"), *timecodeToSplit);
				UE_LOG(LogTemp, Warning, TEXT("LOLED|XDTimecode Malformed"));
				UE_LOG(LogTemp, Warning, TEXT("LOLED|Got %i"), splitDataTC.Num());
			}
			else {
				TimeCode.Hours = FCString::Atoi(*splitDataTC[0]);
				TimeCode.Minutes = FCString::Atoi(*splitDataTC[1]);
				TimeCode.Seconds = FCString::Atoi(*splitDataTC[2]);
				TimeCode.Frames = FCString::Atoi(*splitDataTC[3]);
				TimeCode.bDropFrameFormat = dropFrame;
			}

			int num = 0;
			int dem = 0;
			//It's a decimal framerate, realistically this will only be an NTSC framerate
			if (FGenericPlatformMath::Fmod((float)frameRate, 1)) {
				dem = 1001;
				num = (int)FMath::RoundHalfFromZero(frameRate) * 1000;
			}
			else {
				dem = 1;
				num = (int)frameRate;
			}

			

			FrameData.Aperture = irisMapped;
			FrameData.FocalLength = focalLengthMapped;
			FrameData.FocusDistance = focusMapped;

			Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameDataStruct));
			
		}

		////distortion

		const TSharedPtr<FJsonObject>* DistortionObject;
		bool bHasDistortionData = JsonObject->TryGetObjectField("distortion_data", DistortionObject);
		if (bHasDistortionData) {//Pure encoder data should come in as camera data
			FName SubjectName = (FName)DistortionObject->Get()->GetStringField("serialNumber");

			if (!EncounteredSubjects.Contains(SubjectName)) {
				FLiveLinkStaticDataStruct DistortionDataStaticStruct(FLiveLinkLensStaticData::StaticStruct());
				FLiveLinkLensStaticData DistortionData = *DistortionDataStaticStruct.Cast< FLiveLinkLensStaticData>();

				
				DistortionData.bIsAspectRatioSupported = false;
				DistortionData.bIsFieldOfViewSupported = false;
				DistortionData.bIsFocalLengthSupported = true;
				DistortionData.bIsApertureSupported = true;
				DistortionData.bIsFocusDistanceSupported = true;
				DistortionData.bIsLocationSupported = false;
				DistortionData.bIsScaleSupported = false;
				DistortionData.bIsRotationSupported = false;

				

				DistortionData.LensModel = (FName)DistortionObject->Get()->GetStringField("modelName");

				Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName }, ULiveLinkLensRole::StaticClass(), MoveTemp(DistortionDataStaticStruct));
				EncounteredSubjects.Add(SubjectName);
			}

			FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkLensFrameData::StaticStruct());
			FLiveLinkLensFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkLensFrameData>();

			double focalLengthRaw, focalLengthMapped, irisRaw, irisMapped, focusRaw, focusMapped;
			TArray<TSharedPtr<FJsonValue>> fXfY = DistortionObject->Get()->GetArrayField("fXfY");
			TArray<TSharedPtr<FJsonValue>> principalPoint = DistortionObject->Get()->GetArrayField("principalPoint");
			TArray<TSharedPtr<FJsonValue>> distortionParameters = DistortionObject->Get()->GetArrayField("distortionParameters");

			DistortionObject->Get()->TryGetNumberField(TEXT("focalLengthRaw"), focalLengthRaw);
			DistortionObject->Get()->TryGetNumberField(TEXT("focalLengthMapped"), focalLengthMapped);
			DistortionObject->Get()->TryGetNumberField(TEXT("irisRaw"), irisRaw);
			DistortionObject->Get()->TryGetNumberField(TEXT("irisMapped"), irisMapped);
			DistortionObject->Get()->TryGetNumberField(TEXT("focusRaw"), focusRaw);
			DistortionObject->Get()->TryGetNumberField(TEXT("focusMapped"), focusMapped);

			TArray<float> distortionArray;

			int i = 0;
			for (auto val : distortionParameters) {
				distortionArray.Push(val.Get()[i].AsNumber());
				i++;
			}
			
			FrameData.FxFy = FVector2D(fXfY[0]->AsNumber(), fXfY[1]->AsNumber());
			FrameData.PrincipalPoint = FVector2D(principalPoint[0]->AsNumber(), principalPoint[1]->AsNumber());
			FrameData.DistortionParameters = distortionArray;
			FrameData.Aperture = irisMapped;
			FrameData.FocalLength = focalLengthMapped;
			FrameData.FocusDistance = focusMapped;

			Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameDataStruct));

		}

		////camera

		const TSharedPtr<FJsonObject>* CameraObject;
		bool bHasCamersData = JsonObject->TryGetObjectField("camera_transform_data", CameraObject);
		if (bHasCamersData) {
			FString tmpName = CameraObject->Get()->GetStringField("cameraName") + " Transform";
			FName SubjectName(tmpName);
			
			//FName SubjectName = "trackertest";
			if (!EncounteredSubjects.Contains(SubjectName)) {
				FLiveLinkStaticDataStruct CameraDataStaticStruct(FLiveLinkTransformStaticData::StaticStruct());
				FLiveLinkTransformStaticData CameraData = *CameraDataStaticStruct.Cast< FLiveLinkTransformStaticData>();

				CameraData.bIsLocationSupported = true;
				CameraData.bIsScaleSupported = false;
				CameraData.bIsRotationSupported = true;
				/*
				CameraData.PropertyNames.SetNumUninitialized(7);
				CameraData.PropertyNames[0] = FName("focusRaw");
				CameraData.PropertyNames[1] = FName("irisRaw");
				CameraData.PropertyNames[2] = FName("zoomRaw");
				CameraData.PropertyNames[3] = FName("whiteBalance");
				CameraData.PropertyNames[4] = FName("tint");
				CameraData.PropertyNames[5] = FName("ISO");
				CameraData.PropertyNames[6] = FName("shutter");
				*/

				Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName }, ULiveLinkTransformRole::StaticClass(), MoveTemp(CameraDataStaticStruct));
				EncounteredSubjects.Add(SubjectName);
			}

			FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkTransformFrameData::StaticStruct());
			FLiveLinkTransformFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkTransformFrameData>();

			double focalLengthRaw, irisRaw, focusRaw, whiteBalance, tint, ISO, shutter,frameRate;
			bool dropFrame = false;

			CameraObject->Get()->TryGetNumberField(TEXT("whiteBalance"), whiteBalance);
			CameraObject->Get()->TryGetNumberField(TEXT("tint"), tint);
			CameraObject->Get()->TryGetNumberField(TEXT("ISO"), ISO);
			CameraObject->Get()->TryGetNumberField(TEXT("shutter"), shutter);
			CameraObject->Get()->TryGetNumberField(TEXT("focalLengthRaw"), focalLengthRaw);
			CameraObject->Get()->TryGetNumberField(TEXT("irisRaw"), irisRaw);
			CameraObject->Get()->TryGetNumberField(TEXT("focusRaw"), focusRaw);

			CameraObject->Get()->TryGetNumberField(TEXT("frameRate"), frameRate);
			CameraObject->Get()->TryGetBoolField(TEXT("dropFrame"), dropFrame);
			TArray<TSharedPtr<FJsonValue>> positionArray = CameraObject->Get()->GetArrayField("position");
			TArray<TSharedPtr<FJsonValue>> rotationArray = CameraObject->Get()->GetArrayField("orientation");

			FString timecodeToSplit;
			CameraObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit);
			TArray<FString> splitDataTC;
			timecodeToSplit.ParseIntoArray(splitDataTC, TEXT(":"), true);

			if (splitDataTC.Num() != 4) {
				UE_LOG(LogTemp, Warning, TEXT("TC Packet %s \n"), *timecodeToSplit);
				UE_LOG(LogTemp, Warning, TEXT("LOLED|XDTimecode Malformed"));
				UE_LOG(LogTemp, Warning, TEXT("LOLED|Got %i"), splitDataTC.Num());
			}
			else {
				TimeCode.Hours = FCString::Atoi(*splitDataTC[0]);
				TimeCode.Minutes = FCString::Atoi(*splitDataTC[1]);
				TimeCode.Seconds = FCString::Atoi(*splitDataTC[2]);
				TimeCode.Frames = FCString::Atoi(*splitDataTC[3]);
				TimeCode.bDropFrameFormat = dropFrame;
			}

			int num = 0;
			int dem = 0;
			//It's a decimal framerate, realistically this will only be an NTSC framerate
			if (FGenericPlatformMath::Fmod((float)frameRate, 1)) {
				dem = 1001;
				num = (int)FMath::RoundHalfFromZero(frameRate)*1000;
			}
			else {
				dem = 1;
				num = (int)frameRate;
			}

			FrameData.Transform.SetLocation(FVector(positionArray[0].Get()->AsNumber(), positionArray[1].Get()->AsNumber(), positionArray[2].Get()->AsNumber()));
			FrameData.Transform.SetRotation(FQuat(rotationArray[0].Get()->AsNumber(), rotationArray[1].Get()->AsNumber(), rotationArray[2].Get()->AsNumber(), rotationArray[3].Get()->AsNumber()));
			FrameData.MetaData.SceneTime = FQualifiedFrameTime(TimeCode, FFrameRate(num, dem));
			/*
			FrameData.PropertyValues.SetNumUninitialized(7);
			FrameData.PropertyValues[0] = 1.0f;
			FrameData.PropertyValues[1] = 1.0f;
			FrameData.PropertyValues[2] = 1.0f;
			FrameData.PropertyValues[3] = 1.0f;
			FrameData.PropertyValues[4] = 1.0f;
			FrameData.PropertyValues[5] = 1.0f;
			//FrameData.PropertyValues[6] = 1.0f;
			*/


			Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameDataStruct));

		}


	
	}
}

#undef LOCTEXT_NAMESPACE
