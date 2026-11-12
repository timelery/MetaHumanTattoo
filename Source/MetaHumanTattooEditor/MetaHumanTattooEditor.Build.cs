using UnrealBuildTool;

public class MetaHumanTattooEditor : ModuleRules
{
	public MetaHumanTattooEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MetaHumanCharacter",
			"MetaHumanSDKRuntime",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MetaHumanCharacterEditor",
			"MetaHumanCharacterPalette",
			"MetaHumanCharacterPaletteEditor",
			"MetaHumanCoreTechLib",
			"RigLogicModule",
			"RigLogicLib",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",
			"ToolMenus",
			"PropertyEditor",
			"EditorFramework",
			"EditorSubsystem",
			"DesktopPlatform",
			"AssetRegistry",
			"AssetTools",
			"ImageCore",
			"RenderCore",
			"RHI",
			"Projects",
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Nanosvg");
	}
}
