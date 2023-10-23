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
#include "Misc/CoreDelegates.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkLightRole.h"
#include "Roles/LiveLinkLightTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "LiveLinkLensRole.h"
#include "LiveLinkLensTypes.h"
#include "LiveLinkLensController.h"
#include "SyncroLink.h"

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

	bool threadStopped = false;
	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
		Stopped = true;
	}
	if (Socket != nullptr)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
	}
}


void FLONET2LiveLinkSource::OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent)
{
	
		ILiveLinkSource::OnSettingsChanged(Settings, PropertyChangedEvent);

		FProperty* MemberProperty = PropertyChangedEvent.MemberProperty;
		FProperty* Property = PropertyChangedEvent.Property;
		if (Property && MemberProperty && (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive))
		{
			USyncrolinkSubjectSettings* SourceSettings = Cast<USyncrolinkSubjectSettings>(Settings);
			if (SavedSourceSettings != SourceSettings)
			{
				//UE_LOG(LogLiveLinkFreeD, Error, TEXT("LONET2: OnSettingsChanged pointers don't match - this should never happen!"));
				return;
			}

			if (SourceSettings != nullptr)
			{
				static FName NAME_SensorSize = GET_MEMBER_NAME_CHECKED(USyncrolinkSubjectSettings, SensorSize);
				const FName PropertyName = Property->GetFName();
				const FName MemberPropertyName = MemberProperty->GetFName();
				bool bSensorChanged = false;
				EncounteredSubjects.Empty();
				if (MemberPropertyName == NAME_SensorSize)
				{
					
					bSensorChanged = true;
				}

			}

		}
	
}

void FLONET2LiveLinkSource::InitializeSettings(ULiveLinkSourceSettings* Settings)
{
	// Save our source settings pointer so we can use it directly
	SavedSourceSettings = Cast<USyncrolinkSubjectSettings>(Settings);
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
	//while(!Stopped){}
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
    if (Stopping) { return; } 
	//UE_LOG(ModuleLog, Warning, TEXT("HandleReceiveData"));
	FString JsonString;
	JsonString.Empty(ReceivedData->Num());
	bool bXD =  false;
	for (uint8& Byte : *ReceivedData.Get())
	{
		if (Byte == 0xf3) { bXD = true; }
		JsonString += TCHAR(Byte);
	}
	if (bXD) {
		static SyncroLink::FSyncroLink_t sync = SyncroLink::ParseSyncrolink(JsonString);

		FString tmpName = sync.lensName;
		FName SubjectName(tmpName);

		if (!EncounteredSubjects.Contains(SubjectName)) {
			FLiveLinkStaticDataStruct DistortionDataStaticStruct = FLiveLinkStaticDataStruct(FLiveLinkLensStaticData::StaticStruct());
			FLiveLinkLensStaticData& DistortionData = *DistortionDataStaticStruct.Cast<FLiveLinkLensStaticData>();
			
			DistortionData.bIsAspectRatioSupported = false;
			DistortionData.bIsFieldOfViewSupported = false;
			DistortionData.bIsFocalLengthSupported = true;
			DistortionData.bIsApertureSupported = true;
			DistortionData.bIsFocusDistanceSupported = true;
			DistortionData.bIsLocationSupported = false;
			DistortionData.bIsScaleSupported = false;
			DistortionData.bIsRotationSupported = false;
			DistortionData.LensModel = "spherical";

			DistortionData.PropertyNames.SetNumUninitialized(29);
			DistortionData.PropertyNames[0] = FName("entrance_pupil");
			DistortionData.PropertyNames[1] = FName("hyperfocal");
			DistortionData.PropertyNames[2] = FName("close_focus");
			DistortionData.PropertyNames[3] = FName("far_focus");
			DistortionData.PropertyNames[4] = FName("horizontal_fov");
			DistortionData.PropertyNames[5] = FName("image_height_0");
			DistortionData.PropertyNames[6] = FName("image_height_1");
			DistortionData.PropertyNames[7] = FName("image_height_2");
			DistortionData.PropertyNames[8] = FName("image_height_3");
			DistortionData.PropertyNames[9] = FName("image_height_4");
			DistortionData.PropertyNames[10] = FName("image_height_5");
			DistortionData.PropertyNames[11] = FName("image_height_6");
			DistortionData.PropertyNames[12] = FName("shading_0");
			DistortionData.PropertyNames[13] = FName("shading_1");
			DistortionData.PropertyNames[14] = FName("shading_2");
			DistortionData.PropertyNames[15] = FName("shading_3");
			DistortionData.PropertyNames[16] = FName("shading_4");
			DistortionData.PropertyNames[17] = FName("shading_5");
			DistortionData.PropertyNames[18] = FName("shading_6");
			DistortionData.PropertyNames[19] = FName("K1");
			DistortionData.PropertyNames[20] = FName("K2");
			DistortionData.PropertyNames[21] = FName("P1");
			DistortionData.PropertyNames[22] = FName("P2");
			DistortionData.PropertyNames[23] = FName("K3");
			DistortionData.PropertyNames[24] = FName("K4");
			DistortionData.PropertyNames[25] = FName("K5");
			DistortionData.PropertyNames[26] = FName("K6");
			DistortionData.PropertyNames[27] = FName("Cx");
			DistortionData.PropertyNames[28] = FName("Cy");
			DistortionData.FilmBackWidth = SavedSourceSettings->SensorSize.SensorSize.X;
			DistortionData.FilmBackHeight = SavedSourceSettings->SensorSize.SensorSize.Y;

			Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName }, ULiveLinkLensRole::StaticClass(), MoveTemp(DistortionDataStaticStruct));
			EncounteredSubjects.Add(SubjectName);
		}

		FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkLensFrameData::StaticStruct());
		FLiveLinkLensFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkLensFrameData>();
		
		FrameData.FxFy.X = sync.focalLength * SavedSourceSettings->SensorSize.Resolution.X / SavedSourceSettings->SensorSize.SensorSize.X;
		FrameData.FxFy.Y = sync.focalLength * SavedSourceSettings->SensorSize.Resolution.Y / SavedSourceSettings->SensorSize.SensorSize.Y;
		
		FrameData.PrincipalPoint.X = sync.cX;
		FrameData.PrincipalPoint.X = sync.cY;

		
		FrameData.DistortionParameters.Add(sync.K1Distortion);
		FrameData.DistortionParameters.Add(sync.K2Distortion);
		FrameData.DistortionParameters.Add(sync.K3Distortion);
		//FrameData.DistortionParameters[3] = sync.K4Distortion; //Only used for fisheye lenses
		//FrameData.DistortionParameters[4] = sync.K5Distortion;
		//FrameData.DistortionParameters[5] = sync.K6Distortion;
		
		FrameData.FocusDistance = sync.focusMM / 10;
		FrameData.Aperture = sync.apertureStops;
		FrameData.FocalLength = sync.focalLength;
		
		FrameData.ProjectionMode = ELiveLinkCameraProjectionMode::Perspective;

		FrameData.PropertyValues.SetNumUninitialized(29);
		FrameData.PropertyValues[0] = sync.entrancePupilMM;
		FrameData.PropertyValues[1] = sync.hyperfocalDistMM;
		FrameData.PropertyValues[2] = sync.nearFocusMM;
		FrameData.PropertyValues[3] = sync.farFocusMM;
		FrameData.PropertyValues[4] = sync.HFOVDegrees;
		FrameData.PropertyValues[5] = sync.imageHeight0MM;
		FrameData.PropertyValues[6] = sync.imageHeight1MM;
		FrameData.PropertyValues[7] = sync.imageHeight2MM;
		FrameData.PropertyValues[8] = sync.imageHeight3MM;
		FrameData.PropertyValues[9] = sync.imageHeight4MM;
		FrameData.PropertyValues[10] = sync.imageHeight5MM;
		FrameData.PropertyValues[11] = sync.imageHeight6MM;
		FrameData.PropertyValues[12] = sync.shadingPercent0;
		FrameData.PropertyValues[13] = sync.shadingPercent1;
		FrameData.PropertyValues[14] = sync.shadingPercent2;
		FrameData.PropertyValues[15] = sync.shadingPercent3;
		FrameData.PropertyValues[16] = sync.shadingPercent4;
		FrameData.PropertyValues[17] = sync.shadingPercent5;
		FrameData.PropertyValues[18] = sync.shadingPercent6;
		FrameData.PropertyValues[19] = sync.K1Distortion;
		FrameData.PropertyValues[20] = sync.K2Distortion;
		FrameData.PropertyValues[21] = sync.P1Distortion;
		FrameData.PropertyValues[22] = sync.P2Distortion;
		FrameData.PropertyValues[23] = sync.K3Distortion;
		FrameData.PropertyValues[24] = sync.K4Distortion;
		FrameData.PropertyValues[25] = sync.K5Distortion;
		FrameData.PropertyValues[26] = sync.K6Distortion;
		FrameData.PropertyValues[27] = sync.cX;
		FrameData.PropertyValues[28] = sync.cY;
		



		FrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(sync.timecode, sync.frameRate);
		Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameDataStruct));

	}
	else {//Not from Syncrolink
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{

			//Encoders
			const TSharedPtr<FJsonObject>* EncoderObject;
			bool bHasEncoderData = JsonObject->TryGetObjectField("encoder_data", EncoderObject);
			if (bHasEncoderData) {//Pure encoder data should come in as camera data
				FString tmpName = EncoderObject->Get()->GetStringField("cameraName") + " Encoders";
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
				if (EncoderObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit)) {
					FrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(timecodeToSplit, frameRate);
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
				FString tmpName = DistortionObject->Get()->GetStringField("cameraName") + " Lens";
				FName SubjectName(tmpName);

				if (!EncounteredSubjects.Contains(SubjectName)) {
					FLiveLinkStaticDataStruct DistortionDataStaticStruct = FLiveLinkStaticDataStruct(FLiveLinkLensStaticData::StaticStruct());
					FLiveLinkLensStaticData& DistortionData = *DistortionDataStaticStruct.Cast<FLiveLinkLensStaticData>();

					DistortionData.bIsAspectRatioSupported = false;
					DistortionData.bIsFieldOfViewSupported = false;
					DistortionData.bIsFocalLengthSupported = true;
					DistortionData.bIsApertureSupported = true;
					DistortionData.bIsFocusDistanceSupported = true;
					DistortionData.bIsLocationSupported = false;
					DistortionData.bIsScaleSupported = false;
					DistortionData.bIsRotationSupported = false;
					DistortionData.LensModel = "spherical";

					Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName }, ULiveLinkLensRole::StaticClass(), MoveTemp(DistortionDataStaticStruct));
					EncounteredSubjects.Add(SubjectName);
				}

				FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkLensFrameData::StaticStruct());
				FLiveLinkLensFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkLensFrameData>();

				double focalLengthRaw, focalLengthMapped, irisRaw, irisMapped, focusRaw, focusMapped, frameRate;
				const TArray<TSharedPtr<FJsonValue>>* fXfY;
				const TArray<TSharedPtr<FJsonValue>>* principalPoint;
				const TArray<TSharedPtr<FJsonValue>>* distortionParameters;

				if (DistortionObject->Get()->TryGetArrayField("fXfY", fXfY)) {
					int i = 0;
					for (auto fx : *fXfY) {
						FrameData.FxFy[i] = fx->AsNumber();
						i++;
					}
				}

				if (DistortionObject->Get()->TryGetArrayField("principalPoint", principalPoint)) {
					int i = 0;
					for (auto pp : *principalPoint) {
						FrameData.PrincipalPoint[i] = pp->AsNumber();
						i++;
					}
				}

				if (DistortionObject->Get()->TryGetArrayField("distortionParameters", distortionParameters)) {
					int i = 0;
					for (auto val : *distortionParameters) {
						FrameData.DistortionParameters.Push(val.Get()[i].AsNumber());
						i++;
					}
				}

				bool hasDistortion = DistortionObject->Get()->TryGetArrayField("distortionParameters", principalPoint);

				DistortionObject->Get()->TryGetNumberField(TEXT("focalLengthRaw"), focalLengthRaw);
				DistortionObject->Get()->TryGetNumberField(TEXT("focalLengthMapped"), focalLengthMapped);
				DistortionObject->Get()->TryGetNumberField(TEXT("irisRaw"), irisRaw);
				DistortionObject->Get()->TryGetNumberField(TEXT("irisMapped"), irisMapped);
				DistortionObject->Get()->TryGetNumberField(TEXT("focusRaw"), focusRaw);
				DistortionObject->Get()->TryGetNumberField(TEXT("focusMapped"), focusMapped);
				DistortionObject->Get()->TryGetNumberField(TEXT("frameRate"), frameRate);

				FString timecodeToSplit;
				DistortionObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit);

				FrameData.Aperture = irisMapped;
				FrameData.FocalLength = focalLengthMapped;
				FrameData.FocusDistance = focusMapped;
				FrameData.ProjectionMode = ELiveLinkCameraProjectionMode::Perspective;

				FrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(timecodeToSplit, frameRate);
				Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameDataStruct));

			}

			////camera


			const TSharedPtr<FJsonObject>* CameraObject;
			bool bHasCamersData = JsonObject->TryGetObjectField("camera_transform_data", CameraObject);
			if (bHasCamersData) {
				FString camName = CameraObject->Get()->GetStringField("cameraName");
				FString tmpName = camName;
				FName SubjectName(tmpName);

				FString tmpNameBase = camName + " Data";
				FName SubjectNameBase(tmpNameBase);

				if (!EncounteredSubjects.Contains(SubjectName)) {
					FLiveLinkStaticDataStruct CameraDataStaticStruct = FLiveLinkStaticDataStruct(FLiveLinkCameraStaticData::StaticStruct());
					FLiveLinkCameraStaticData& CameraData = *CameraDataStaticStruct.Cast< FLiveLinkCameraStaticData>();

					CameraData.bIsLocationSupported = true;
					CameraData.bIsScaleSupported = false;
					CameraData.bIsRotationSupported = true;
					CameraData.bIsFocalLengthSupported = true;
					CameraData.bIsApertureSupported = true;
					CameraData.bIsFocusDistanceSupported = true;

					CameraData.PropertyNames.SetNumUninitialized(6);
					CameraData.PropertyNames[0] = FName("whiteBalance");
					CameraData.PropertyNames[1] = FName("tint");
					CameraData.PropertyNames[2] = FName("ISO");
					CameraData.PropertyNames[3] = FName("shutter");
					CameraData.PropertyNames[4] = FName("sensorX");
					CameraData.PropertyNames[5] = FName("sensorY");

					Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName }, ULiveLinkCameraRole::StaticClass(), MoveTemp(CameraDataStaticStruct));
					EncounteredSubjects.Add(SubjectName);
				}

				FLiveLinkFrameDataStruct FrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkCameraFrameData::StaticStruct());
				FLiveLinkCameraFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkCameraFrameData>();


				double focalLengthRaw, irisRaw, focusRaw, whiteBalance, tint, ISO, shutter, frameRate;
				
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
				TArray<TSharedPtr<FJsonValue>> sensorSizeArray = CameraObject->Get()->GetArrayField("sensorSize");

				FString timecodeToSplit;
				CameraObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit);

				FrameData.FocusDistance = focusRaw;
				FrameData.Aperture = irisRaw;
				FrameData.FocalLength = focalLengthRaw;

				FrameData.Transform.SetLocation(FVector(positionArray[0].Get()->AsNumber(), positionArray[1].Get()->AsNumber(), positionArray[2].Get()->AsNumber()));
				FrameData.Transform.SetRotation(FQuat(rotationArray[0].Get()->AsNumber(), rotationArray[1].Get()->AsNumber(), rotationArray[2].Get()->AsNumber(), rotationArray[3].Get()->AsNumber()));
				FrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(timecodeToSplit, frameRate);

				//UserFrameData.PropertyValues.Empty();
				FrameData.PropertyValues.SetNumUninitialized(6);
				FrameData.PropertyValues[0] = whiteBalance;
				FrameData.PropertyValues[1] = tint;
				FrameData.PropertyValues[2] = ISO;
				FrameData.PropertyValues[3] = shutter;
				FrameData.PropertyValues[4] = sensorSizeArray[0].Get()->AsNumber();
				FrameData.PropertyValues[5] = sensorSizeArray[1].Get()->AsNumber();
				FrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(timecodeToSplit, frameRate);

				Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(FrameDataStruct));
			}

			//Controller
			const TSharedPtr<FJsonObject>* ControllerObject;
			bool bHasControllerData = JsonObject->TryGetObjectField("controller_data", ControllerObject);
			if (bHasControllerData) {
				FString controllerName = ControllerObject->Get()->GetStringField("controllerName");
				FString tmpNameBase = controllerName + " Controller";
				FName SubjectNameBase(tmpNameBase);

				if (!EncounteredSubjects.Contains(SubjectNameBase)) {

					FLiveLinkStaticDataStruct UserStaticDataStruct = FLiveLinkStaticDataStruct(FLiveLinkBaseStaticData::StaticStruct());
					FLiveLinkBaseStaticData& UserStaticData = *UserStaticDataStruct.Cast<FLiveLinkBaseStaticData>();

					UserStaticData.PropertyNames.SetNumUninitialized(7);
					UserStaticData.PropertyNames[0] = FName("button1");
					UserStaticData.PropertyNames[1] = FName("button2");
					UserStaticData.PropertyNames[2] = FName("button3");
					UserStaticData.PropertyNames[3] = FName("trigger");
					UserStaticData.PropertyNames[4] = FName("touchpadPressed");
					UserStaticData.PropertyNames[5] = FName("touchpadX");
					UserStaticData.PropertyNames[6] = FName("touchpadY");

					Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectNameBase }, ULiveLinkBasicRole::StaticClass(), MoveTemp(UserStaticDataStruct));
					EncounteredSubjects.Add(SubjectNameBase);
				}

				FLiveLinkFrameDataStruct UserFrameDataStruct = FLiveLinkFrameDataStruct(FLiveLinkBaseFrameData::StaticStruct());
				FLiveLinkBaseFrameData& UserFrameData = *UserFrameDataStruct.Cast<FLiveLinkBaseFrameData>();


				double button1, button2, button3, trigger, touchpadPressed, touchpadX, touchpadY, frameRate;

				ControllerObject->Get()->TryGetNumberField(TEXT("button1"), button1);
				ControllerObject->Get()->TryGetNumberField(TEXT("button2"), button2);
				ControllerObject->Get()->TryGetNumberField(TEXT("button3"), button3);
				ControllerObject->Get()->TryGetNumberField(TEXT("trigger"), trigger);
				ControllerObject->Get()->TryGetNumberField(TEXT("touchpadPressed"), touchpadPressed);
				ControllerObject->Get()->TryGetNumberField(TEXT("touchpadX"), touchpadX);
				ControllerObject->Get()->TryGetNumberField(TEXT("touchpadY"), touchpadY);

				UserFrameData.PropertyValues.SetNumUninitialized(7);
				UserFrameData.PropertyValues[0] = button1;
				UserFrameData.PropertyValues[1] = button2;
				UserFrameData.PropertyValues[2] = button3;
				UserFrameData.PropertyValues[3] = trigger;
				UserFrameData.PropertyValues[4] = touchpadPressed;
				UserFrameData.PropertyValues[5] = touchpadX;
				UserFrameData.PropertyValues[6] = touchpadY;

				FString timecodeToSplit;
				ControllerObject->Get()->TryGetNumberField(TEXT("frameRate"), frameRate);
				ControllerObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit);
				UserFrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(timecodeToSplit, frameRate);

				Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectNameBase }, MoveTemp(UserFrameDataStruct));
			}
			
		}
        
		FPlatformProcess::Sleep(0.f);
	}
    JsonString.Empty();
}

#undef LOCTEXT_NAMESPACE
