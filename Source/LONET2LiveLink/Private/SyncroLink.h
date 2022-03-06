// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
class SyncroLink
{
public:


	struct FSyncroLink_t {
		FString identifier;
		FString lensName;
		float focusMM;
		float apertureStops;
		float focalLength;
		float entrancePupilMM;
		float hyperfocalDistMM;
		float nearFocusMM;
		float farFocusMM;
		float HFOVDegrees;

		float imageHeight0MM;
		float imageHeight1MM;
		float imageHeight2MM;
		float imageHeight3MM;
		float imageHeight4MM;
		float imageHeight5MM;
		float imageHeight6MM;

		float shadingPercent0;
		float shadingPercent1;
		float shadingPercent2;
		float shadingPercent3;
		float shadingPercent4;
		float shadingPercent5;
		float shadingPercent6;

		float K1Distortion;
		float K2Distortion;
		float P1Distortion;
		float P2Distortion;
		float K3Distortion;
		float K4Distortion;
		float K5Distortion;
		float K6Distortion;
		float cX;
		float cY;

		FString timecode;
		bool dropFrame;
		float frameRate;
		FString manufacturer;
		FString serialNumber;
		FString lensOwner;
		FString lensFirmware;
	};

	static FSyncroLink_t ParseSyncrolink(FString string);

	SyncroLink();
	~SyncroLink();
};
