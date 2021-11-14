///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#include "LONET2LiveLink.h"

#define LOCTEXT_NAMESPACE "FLONET2LiveLink"

void FLONET2LiveLinkModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FLONET2LiveLinkModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLONET2LiveLinkModule, LONET2LiveLink)