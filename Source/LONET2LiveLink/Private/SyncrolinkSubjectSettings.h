// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceSettings.h"
#include "SyncrolinkSubjectSettings.generated.h"

/**
 * 
 */

USTRUCT()
struct FSyncrolinkSensorData
{
	GENERATED_BODY()

		UPROPERTY(EditAnywhere, Category = "Sensor Size")
		FVector2D SensorSize = { 26.4,14.85 };
		UPROPERTY(EditAnywhere, Category = "Image Resolution")
		FVector2D Resolution = { 3840,2160 };
};

UCLASS()
class USyncrolinkSubjectSettings : public ULiveLinkSourceSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Source")
		FSyncrolinkSensorData SensorSize;
	
};
