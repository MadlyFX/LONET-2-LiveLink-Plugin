///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

using UnrealBuildTool;

public class LONET2LiveLink : ModuleRules
{
	public LONET2LiveLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.Default;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"LiveLinkInterface",
				"Messaging",
				"LiveLinkCamera",
                "LiveLinkLens"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"InputCore",
				"Json",
				"JsonUtilities",
				"Networking",
				"Slate",
				"SlateCore",
				"Sockets",
				"LiveLinkLens"
			});
	}
}
