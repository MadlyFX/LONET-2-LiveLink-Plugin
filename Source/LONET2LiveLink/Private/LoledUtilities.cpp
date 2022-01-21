// Fill out your copyright notice in the Description page of Project Settings.


#include "LoledUtilities.h"

LoledUtilities::LoledUtilities()
{
}

LoledUtilities::~LoledUtilities()
{
}

FQualifiedFrameTime LoledUtilities::timeFromTimecodeString(FString timecode, float frameRate)
{
	bool dropFrame = false;
	FTimecode TimeCode;
	TArray<FString> splitDataTC;


	int num = 0;
	int dem = 0;
	//It's a decimal framerate, realistically this will only be an drop frame framerate. If this causes you a problem, email me.
	if (FGenericPlatformMath::Fmod((float)frameRate, 1)) {
		dem = 1001;
		num = (int)FMath::RoundHalfFromZero(frameRate) * 1000;
		dropFrame = true;
	}
	else {
		dem = 1;
		num = (int)frameRate;
	}

	timecode.ParseIntoArray(splitDataTC, TEXT(":"), true);

	if (splitDataTC.Num() != 4) {
		UE_LOG(LogTemp, Warning, TEXT("TC Packet %s \n"), *timecode);
		UE_LOG(LogTemp, Warning, TEXT("LOLED|Timecode Malformed"));
		UE_LOG(LogTemp, Warning, TEXT("LOLED|Got %i"), splitDataTC.Num());
	}
	else {
		TimeCode.Hours = FCString::Atoi(*splitDataTC[0]);
		TimeCode.Minutes = FCString::Atoi(*splitDataTC[1]);
		TimeCode.Seconds = FCString::Atoi(*splitDataTC[2]);
		TimeCode.Frames = FCString::Atoi(*splitDataTC[3]);
		TimeCode.bDropFrameFormat = dropFrame;
	}

    return FQualifiedFrameTime(TimeCode, FFrameRate(num, dem));
}
