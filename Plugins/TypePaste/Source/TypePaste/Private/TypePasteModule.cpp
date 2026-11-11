// TypePaste - 모듈 시작/종료 및 탭 등록

#include "TypePasteModule.h"
#include "STypePastePanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FTypePasteModule"

DEFINE_LOG_CATEGORY(LogTypePaste);

const FName FTypePasteModule::TabName(TEXT("TypePaste"));

void FTypePasteModule::StartupModule()
{
	// Tools 메뉴 > TypePaste 로 열 수 있는 탭
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FTypePasteModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "TypePaste (타입 붙여넣기)"))
		.SetTooltipText(LOCTEXT("TabTooltip", "AI 가 추천한 열거형/구조체 코드를 붙여넣어 블루프린트 에셋이나 C++ 헤더로 만듭니다."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.UserDefinedStruct"));
}

void FTypePasteModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
	}
}

TSharedRef<SDockTab> FTypePasteModule::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(STypePastePanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTypePasteModule, TypePaste)
