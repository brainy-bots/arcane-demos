// Copyright Arcane Engine. Shared types and protocol codec for Arcane plugin modules.

using UnrealBuildTool;

public class ArcaneCore : ModuleRules
{
	public ArcaneCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			System.IO.Path.Combine(ModuleDirectory, "ThirdParty/flatbuffers/include")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities"
		});
	}
}
