///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
class LONET2LIVELINK_API LoledUtilities
{
public:
	LoledUtilities();
	~LoledUtilities();

	static FQualifiedFrameTime timeFromTimecodeString(FString timecode, float frameRate);

};
