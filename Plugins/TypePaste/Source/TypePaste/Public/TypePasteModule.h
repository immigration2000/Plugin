// TypePaste - editor plugin that turns pasted AI-suggested UENUM/USTRUCT code into Blueprint assets or C++ headers

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTypePaste, Log, All);

class SDockTab;
class FSpawnTabArgs;

class FTypePasteModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Name of the tab registered in the Tools menu */
	static const FName TabName;

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
