// Fill out your copyright notice in the Description page of Project Settings.


#include "SyncroLink.h"

SyncroLink::SyncroLink()
{
}

SyncroLink::FSyncroLink_t SyncroLink::ParseSyncrolink(FString string)
{

	TArray<FString> splitData;
	string.ParseIntoArray(splitData, TEXT(","), true);

	FSyncroLink_t data;
	data.identifier = splitData[0];
	data.timecode = splitData[34];
	data.frameRate = FCString::Atof(*splitData[36]);
	data.dropFrame = (bool)FCString::Atoi(*splitData[35]);
	data.lensName = splitData[1];
	data.manufacturer = splitData[37];
	data.lensOwner = splitData[39];
	data.serialNumber = splitData[38];
	data.lensFirmware = splitData[40];
	data.focusMM = FCString::Atof(*splitData[2]);
	data.apertureStops = FCString::Atof(*splitData[3]);
	data.focalLength = FCString::Atof(*splitData[4]);
	data.entrancePupilMM = FCString::Atof(*splitData[5]);
	data.hyperfocalDistMM = FCString::Atof(*splitData[6]);
	data.nearFocusMM = FCString::Atof(*splitData[7]);
	data.farFocusMM = FCString::Atof(*splitData[8]);
	data.HFOVDegrees = FCString::Atof(*splitData[9]);

	data.imageHeight0MM = FCString::Atof(*splitData[10]);
	data.imageHeight1MM = FCString::Atof(*splitData[11]);
	data.imageHeight2MM = FCString::Atof(*splitData[12]);
	data.imageHeight3MM = FCString::Atof(*splitData[13]);
	data.imageHeight4MM = FCString::Atof(*splitData[14]);
	data.imageHeight5MM = FCString::Atof(*splitData[15]);
	data.imageHeight6MM = FCString::Atof(*splitData[16]);

	data.shadingPercent0 = FCString::Atof(*splitData[17]);
	data.shadingPercent1 = FCString::Atof(*splitData[18]);
	data.shadingPercent2 = FCString::Atof(*splitData[19]);
	data.shadingPercent3 = FCString::Atof(*splitData[20]);
	data.shadingPercent4 = FCString::Atof(*splitData[21]);
	data.shadingPercent5 = FCString::Atof(*splitData[22]);
	data.shadingPercent6 = FCString::Atof(*splitData[23]);

	data.K1Distortion = FCString::Atof(*splitData[24]);
	data.K2Distortion = FCString::Atof(*splitData[25]);
	data.P1Distortion = FCString::Atof(*splitData[26]);
	data.P2Distortion = FCString::Atof(*splitData[27]);
	data.K3Distortion = FCString::Atof(*splitData[28]);
	data.K4Distortion = FCString::Atof(*splitData[29]);
	data.K5Distortion = FCString::Atof(*splitData[30]);
	data.K6Distortion = FCString::Atof(*splitData[31]);
	data.cX = FCString::Atof(*splitData[32]);
	data.cY = FCString::Atof(*splitData[33]);

	return data;
}



SyncroLink::~SyncroLink()
{
}
