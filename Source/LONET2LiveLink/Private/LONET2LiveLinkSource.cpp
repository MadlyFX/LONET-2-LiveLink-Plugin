////COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#include "LONET2LiveLinkSource.h"

#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
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


//enable logging step 2
DEFINE_LOG_CATEGORY(ModuleLog)

#define LOCTEXT_NAMESPACE "LONET2LiveLinkSource"

#define RECV_BUFFER_SIZE 1024 * 1024

FLONET2LiveLinkSource::FLONET2LiveLinkSource(FIPv4Endpoint InEndpoint)
	: DeviceEndpoint(InEndpoint)
{
	SourceStatus = LOCTEXT("SourceStatus_DeviceNotFound", "Device Not Found");
	SourceType = LOCTEXT("LONET2LiveLinkSourceType", "LONET 2 LiveLink");
	SourceMachineName = FText::FromString(DeviceEndpoint.ToString());

	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FLONET2LiveLinkSource::OnEnginePreExit);

	if (OpenSocket())
	{
		SourceStatus = LOCTEXT("SourceStatus_Receiving", "Receiving");
	}
}

FLONET2LiveLinkSource::~FLONET2LiveLinkSource()
{
	CloseSockets();
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
}

void FLONET2LiveLinkSource::OnEnginePreExit()
{
	RequestSourceShutdown();
	Update();
}

bool FLONET2LiveLinkSource::OpenSocket()
{
	UE_LOG(ModuleLog, Warning, TEXT("Setup socket"));

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

	if (Socket == nullptr || Socket->GetSocketType() != SOCKTYPE_Datagram)
	{
		UE_LOG(ModuleLog, Error, TEXT("Failed to create UDP socket on %s"), *DeviceEndpoint.ToString());
		return false;
	}

	const FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(100);

	UdpReceiver = MakeUnique<FUdpSocketReceiver>(Socket, ThreadWaitTime, TEXT("LONET2_UdpReceiver"));
	UdpReceiver->OnDataReceived().BindRaw(this, &FLONET2LiveLinkSource::HandleReceivedData);
	UdpReceiver->SetMaxReadBufferSize(RECV_BUFFER_SIZE);
	UdpReceiver->Start();

	return true;
}

void FLONET2LiveLinkSource::CloseSockets()
{
	// Stop receiver thread first (blocks until thread exits),
	// then destroy socket — same order as Epic's CloseSockets().
	UdpReceiver.Reset();

	if (Socket != nullptr)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FLONET2LiveLinkSource::OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent)
{
	ILiveLinkSource::OnSettingsChanged(Settings, PropertyChangedEvent);

	FProperty* MemberProperty = PropertyChangedEvent.MemberProperty;
	FProperty* Property = PropertyChangedEvent.Property;
	if (Property && MemberProperty && (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive))
	{

	}
}

void FLONET2LiveLinkSource::InitializeSettings(ULiveLinkSourceSettings* Settings)
{
	
}

void FLONET2LiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
}

bool FLONET2LiveLinkSource::IsSourceStillValid() const
{
	return !bShutdownRequested && Socket != nullptr;
}

bool FLONET2LiveLinkSource::RequestSourceShutdown()
{

	bShutdownRequested = true;

	if (Client != nullptr)
	{
		for (const FName& SubjectName : EncounteredSubjects)
		{
			Client->RemoveSubject_AnyThread({ SourceGuid, SubjectName });
		}
		EncounteredSubjects.Empty();
	}
	CloseSockets();
	return true;
}

void FLONET2LiveLinkSource::Update()
{
	if (bShutdownRequested)
	{
		CloseSockets();
		SourceStatus = LOCTEXT("SourceStatus_ShutDown", "Shut Down");
	}
}

void FLONET2LiveLinkSource::HandleReceivedData(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& Sender)
{
	if (bShutdownRequested || !Data.IsValid() || Client == nullptr)
	{
		return;
	}

	TSharedPtr<FArrayReader, ESPMode::ThreadSafe> DataCopy = Data;
	AsyncTask(ENamedThreads::GameThread, [this, DataCopy]()
		{
			if (bShutdownRequested || Client == nullptr) { return; }
			ProcessJsonData(*DataCopy);
		});
}

void FLONET2LiveLinkSource::ProcessJsonData(const TArray<uint8>& RawData)
{

	FString JsonString;
	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(RawData.GetData()), RawData.Num());
	JsonString = FString(Converter.Length(), Converter.Get());

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	//Encoders
	const TSharedPtr<FJsonObject>* EncoderObject;
	bool bHasEncoderData = JsonObject->TryGetObjectField("encoder_data", EncoderObject);
	if (bHasEncoderData) {
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

		double focalLengthMapped = 0.0, irisMapped = 0.0, focusMapped = 0.0, frameRate = 24.0;
		EncoderObject->Get()->TryGetNumberField(TEXT("focalLengthMapped"), focalLengthMapped);
		EncoderObject->Get()->TryGetNumberField(TEXT("irisMapped"), irisMapped);
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
	if (bHasDistortionData) {
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

		double focalLengthMapped = 0.0, irisMapped = 0.0, focusMapped = 0.0, frameRate = 24.0;
		const TArray<TSharedPtr<FJsonValue>>* fXfY;
		const TArray<TSharedPtr<FJsonValue>>* principalPoint;
		const TArray<TSharedPtr<FJsonValue>>* distortionParameters;

		if (DistortionObject->Get()->TryGetArrayField("fXfY", fXfY) && fXfY->Num() >= 2) {
			FrameData.FxFy[0] = (*fXfY)[0]->AsNumber();
			FrameData.FxFy[1] = (*fXfY)[1]->AsNumber();
		}

		if (DistortionObject->Get()->TryGetArrayField("principalPoint", principalPoint) && principalPoint->Num() >= 2) {
			FrameData.PrincipalPoint[0] = (*principalPoint)[0]->AsNumber();
			FrameData.PrincipalPoint[1] = (*principalPoint)[1]->AsNumber();
		}

		if (DistortionObject->Get()->TryGetArrayField("distortionParameters", distortionParameters)) {
			for (const auto& val : *distortionParameters) {
				FrameData.DistortionParameters.Push(val->AsNumber());
			}
		}

		DistortionObject->Get()->TryGetNumberField(TEXT("focalLengthMapped"), focalLengthMapped);
		DistortionObject->Get()->TryGetNumberField(TEXT("irisMapped"), irisMapped);
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
	bool bHasCameraData = JsonObject->TryGetObjectField("camera_transform_data", CameraObject);
	if (bHasCameraData) {
		FString camName = CameraObject->Get()->GetStringField("cameraName");
		FName SubjectName(camName);

		if (!EncounteredSubjects.Contains(SubjectName)) {
			FLiveLinkStaticDataStruct CameraDataStaticStruct = FLiveLinkStaticDataStruct(FLiveLinkCameraStaticData::StaticStruct());
			FLiveLinkCameraStaticData& CameraData = *CameraDataStaticStruct.Cast<FLiveLinkCameraStaticData>();

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

		double focalLengthRaw = 0.0, irisRaw = 0.0, focusRaw = 0.0;
		double whiteBalance = 0.0, tint = 0.0, ISO = 0.0, shutter = 0.0, frameRate = 24.0;

		CameraObject->Get()->TryGetNumberField(TEXT("whiteBalance"), whiteBalance);
		CameraObject->Get()->TryGetNumberField(TEXT("tint"), tint);
		CameraObject->Get()->TryGetNumberField(TEXT("ISO"), ISO);
		CameraObject->Get()->TryGetNumberField(TEXT("shutter"), shutter);
		CameraObject->Get()->TryGetNumberField(TEXT("focalLengthRaw"), focalLengthRaw);
		CameraObject->Get()->TryGetNumberField(TEXT("irisRaw"), irisRaw);
		CameraObject->Get()->TryGetNumberField(TEXT("focusRaw"), focusRaw);
		CameraObject->Get()->TryGetNumberField(TEXT("frameRate"), frameRate);

		TArray<TSharedPtr<FJsonValue>> positionArray = CameraObject->Get()->GetArrayField("position");
		TArray<TSharedPtr<FJsonValue>> rotationArray = CameraObject->Get()->GetArrayField("orientation");
		TArray<TSharedPtr<FJsonValue>> sensorSizeArray = CameraObject->Get()->GetArrayField("sensorSize");

		FString timecodeToSplit;
		CameraObject->Get()->TryGetStringField(TEXT("timecode"), timecodeToSplit);

		FrameData.FocusDistance = focusRaw;
		FrameData.Aperture = irisRaw;
		FrameData.FocalLength = focalLengthRaw;

		if (positionArray.Num() >= 3)
		{
			FrameData.Transform.SetLocation(FVector(positionArray[0]->AsNumber(), positionArray[1]->AsNumber(), positionArray[2]->AsNumber()));
		}
		if (rotationArray.Num() >= 4)
		{
			FrameData.Transform.SetRotation(FQuat(rotationArray[0]->AsNumber(), rotationArray[1]->AsNumber(), rotationArray[2]->AsNumber(), rotationArray[3]->AsNumber()));
		}

		FrameData.MetaData.SceneTime = LoledUtilities::timeFromTimecodeString(timecodeToSplit, frameRate);

		FrameData.PropertyValues.SetNumUninitialized(6);
		FrameData.PropertyValues[0] = whiteBalance;
		FrameData.PropertyValues[1] = tint;
		FrameData.PropertyValues[2] = ISO;
		FrameData.PropertyValues[3] = shutter;
		if (sensorSizeArray.Num() >= 2)
		{
			FrameData.PropertyValues[4] = sensorSizeArray[0]->AsNumber();
			FrameData.PropertyValues[5] = sensorSizeArray[1]->AsNumber();
		}

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

		double button1 = 0.0, button2 = 0.0, button3 = 0.0, trigger = 0.0;
		double touchpadPressed = 0.0, touchpadX = 0.0, touchpadY = 0.0, frameRate = 24.0;

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

#undef LOCTEXT_NAMESPACE