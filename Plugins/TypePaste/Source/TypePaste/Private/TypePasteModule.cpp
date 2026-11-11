// TypePaste - module startup/shutdown and tab registration

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
	// Tab opened from the Tools menu > TypePaste
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FTypePasteModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "TypePaste"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Paste AI-suggested enum/struct code to create Blueprint assets or C++ headers."))
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
