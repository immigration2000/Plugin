// TypePaste - 에디터 전용 모듈

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
			"ApplicationCore",        // 클립보드
			"AssetRegistry",          // 기존 블루프린트 열거형/구조체 검색
			"AssetTools",             // 에셋 생성
			"BlueprintGraph",         // UEdGraphSchema_K2 (핀 타입)
			"ContentBrowser",         // 경로 선택기, 생성 후 위치 표시
			"InputCore",
			"Projects",               // 프로젝트 모듈 목록
			"Slate",
			"SlateCore",
			"UnrealEd",               // FEnumEditorUtils, FStructureEditorUtils, 팩토리
			"WorkspaceMenuStructure", // Tools 메뉴 등록
		});
	}
}
