// Copyright Arcane Engine. Arcane Demo game module.

using UnrealBuildTool;

public class ArcaneDemo : ModuleRules
{
	public ArcaneDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ArcaneClient"
		});
	}
}
