// Copyright Arcane Engine. Plugin: Arcane Client adapter for manager join + cluster WebSocket.

using UnrealBuildTool;

public class ArcaneClient : ModuleRules
{
	public ArcaneClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities"
		});

		// Load on demand so plugin can load even if WebSockets fails at engine startup
		DynamicallyLoadedModuleNames.Add("WebSockets");
		PrivateIncludePathModuleNames.Add("WebSockets");
	}
}
