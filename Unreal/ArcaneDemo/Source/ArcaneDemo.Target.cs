// Copyright Arcane Engine. Game target.

using UnrealBuildTool;
using System.Collections.Generic;

public class ArcaneDemoTarget : TargetRules
{
	public ArcaneDemoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ArcaneDemo");
	}
}
