using UnrealBuildTool;

public class VRgridViewerTarget : TargetRules
{
	public VRgridViewerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("VRgridViewer");
	}
}
