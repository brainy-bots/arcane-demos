// Copyright Arcane Engine. Plugin: Arcane server-side cluster node integration.

using UnrealBuildTool;

public class ArcaneServer : ModuleRules
{
	public ArcaneServer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ArcaneCore"
		});

		// TODO: FFI bridge for arcane_native shared library will be added in a future phase.
	}
}
