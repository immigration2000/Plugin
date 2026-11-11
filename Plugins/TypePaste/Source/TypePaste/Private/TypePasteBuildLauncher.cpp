// TypePaste - 빌드 후 다시 열기 구현

#include "TypePasteBuildLauncher.h"
#include "TypePasteModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IMainFrameModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace TypePasteBuildLauncherPrivate
{
	/** 스크립트 첫 부분에 있는 표식. 프로젝트 루트의 Build.bat 이 TypePaste 가 만든 것인지 구분한다 */
	static const TCHAR* ScriptMarker = TEXT("TYPEPASTE_BUILD_SCRIPT");

	static bool IsTypePasteScript(const FString& Path)
	{
		FString Content;
		return FFileHelper::LoadFileToString(Content, *Path) && Content.Contains(ScriptMarker, ESearchCase::CaseSensitive);
	}

	static FString Quote(const FString& In)
	{
		return FString(TEXT("\"")) + In + TEXT("\"");
	}
}

FString FTypePasteBuildLauncher::GetPluginScriptPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TypePaste"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Scripts"), TEXT("Build.bat")));
	return FPaths::FileExists(Path) ? Path : FString();
}

FString FTypePasteBuildLauncher::GetProjectScriptPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Build.bat")));
}

void FTypePasteBuildLauncher::EnsureProjectScript(TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteBuildLauncherPrivate;

	const FString Source = GetPluginScriptPath();
	const FString Target = GetProjectScriptPath();
	if (Source.IsEmpty())
	{
		return;
	}
	if (FPaths::FileExists(Target))
	{
		if (!IsTypePasteScript(Target))
		{
			OutMessages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("프로젝트 루트에 다른 Build.bat 이 있어 복사하지 않았습니다. 직접 빌드하려면 ")) + Source
				+ TEXT(" 를 쓰세요.")));
		}
		return;
	}
	if (IFileManager::Get().Copy(*Target, *Source) == COPY_OK)
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("빌드 스크립트를 만들었습니다: ")) + Target
			+ TEXT("  (에디터를 닫고 더블클릭하면 빌드 후 프로젝트를 엽니다. 솔루션 파일 필요 없음)")));
	}
}

bool FTypePasteBuildLauncher::LaunchBuildAndReopen(TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteBuildLauncherPrivate;

	const FString Script = GetPluginScriptPath();
	if (Script.IsEmpty())
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Error, TEXT("플러그인의 Scripts/Build.bat 을 찾지 못했습니다.")));
		return false;
	}

	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	FString EngineRoot = FPaths::ConvertRelativePathToFull(FPaths::RootDir());
	FPaths::NormalizeDirectoryName(EngineRoot);
	FPaths::MakePlatformFilename(EngineRoot);
	FString ScriptPath = Script;
	FPaths::MakePlatformFilename(ScriptPath);
	FString ProjectPath = ProjectFile;
	FPaths::MakePlatformFilename(ProjectPath);

	// cmd /c start "제목" "Build.bat" ... : start 가 새 콘솔 창을 띄우므로 에디터가 꺼져도 빌드 창은 남는다
	const FString Params = FString::Printf(TEXT("/c start \"TypePaste Build\" %s -project %s -engine %s -waitpid %u"),
		*Quote(ScriptPath), *Quote(ProjectPath), *Quote(EngineRoot), FPlatformProcess::GetCurrentProcessId());

	FProcHandle Handle = FPlatformProcess::CreateProc(TEXT("cmd.exe"), *Params, true, false, false, nullptr, 0, nullptr, nullptr);
	if (!Handle.IsValid())
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("빌드 스크립트를 실행하지 못했습니다: ")) + ScriptPath));
		return false;
	}
	FPlatformProcess::CloseProc(Handle);
	UE_LOG(LogTypePaste, Log, TEXT("빌드 스크립트 실행: cmd.exe %s"), *Params);

	// 저장 안 한 에셋이 있으면 에디터가 저장 여부를 묻는다. 취소하면 스크립트는 에디터가 꺼질 때까지 기다린다
	IMainFrameModule::Get().RequestCloseEditor();
	return true;
}
