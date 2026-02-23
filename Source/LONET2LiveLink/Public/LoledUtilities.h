///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "CoreMinimal.h"
#include "Misc/QualifiedFrameTime.h"
#include "Misc/FrameRate.h"
#include "Misc/Timecode.h"
#include "Containers/UnrealString.h"

/**
 * 
 */
class LoledUtilities
{
public:
	LoledUtilities();
	~LoledUtilities();

	static FQualifiedFrameTime timeFromTimecodeString(FString timecode, float frameRate);

};
