// Copyright Arcane Engine. Editor target.

using UnrealBuildTool;
using System.Collections.Generic;

public class ArcaneDemoEditorTarget : TargetRules
{
	public ArcaneDemoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ArcaneDemo");
	}
}
