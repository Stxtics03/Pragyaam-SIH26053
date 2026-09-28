using UnrealBuildTool;

public class VRgridViewer : ModuleRules
{
	public VRgridViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",          // scene.json manifest
			"JsonUtilities",
			"RenderCore",
			"ProceduralMeshComponent",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UMG",           // the HUD reads stats.jsonl
		});
	}
}
