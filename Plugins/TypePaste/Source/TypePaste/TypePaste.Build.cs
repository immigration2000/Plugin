// TypePaste - editor-only module

using UnrealBuildTool;

public class TypePaste : ModuleRules
{
	public TypePaste(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",        // clipboard
			"AssetRegistry",          // search existing Blueprint enums/structs
			"AssetTools",             // asset creation
			"BlueprintGraph",         // UEdGraphSchema_K2 (pin types)
			"ContentBrowser",         // path picker, show created assets
			"InputCore",
			"MainFrame",              // Build and Reopen: request editor close
			"Projects",               // project module list, plugin folder (build script)
			"Slate",
			"SlateCore",
			"UnrealEd",               // FEnumEditorUtils, FStructureEditorUtils, factories
			"WorkspaceMenuStructure", // Tools menu registration
		});
	}
}
