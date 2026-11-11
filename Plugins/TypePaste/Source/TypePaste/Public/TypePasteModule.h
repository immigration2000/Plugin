// TypePaste - AI 가 추천한 UENUM/USTRUCT 코드를 붙여넣어 블루프린트 에셋이나 C++ 헤더로 만드는 에디터 플러그인

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

	/** Tools 메뉴에 등록되는 탭 이름 */
	static const FName TabName;

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
