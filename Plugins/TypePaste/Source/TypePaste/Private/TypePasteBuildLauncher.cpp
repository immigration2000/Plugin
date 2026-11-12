// TypePaste - Build and Reopen implementation

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
	/** Marker near the top of the script. Tells whether the project root Build.bat was made by TypePaste */
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
			OutMessages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("The project root already has a different Build.bat, so it was not copied. To build by hand, use ")) + Source
				+ TEXT(".")));
		}
		return;
	}
	if (IFileManager::Get().Copy(*Target, *Source) == COPY_OK)
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("Created the build script: ")) + Target
			+ TEXT("  (close the editor and double-click it to build and open the project; no solution file needed)")));
	}
}

bool FTypePasteBuildLauncher::LaunchBuildAndReopen(TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteBuildLauncherPrivate;

	const FString Script = GetPluginScriptPath();
	if (Script.IsEmpty())
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Error, TEXT("Could not find the plugin's Scripts/Build.bat.")));
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

	// start opens a new console window, so the build window stays after the editor exits.
	// Do NOT pass the .bat to start directly: start runs it as `cmd /K "Build.bat" ... "last"` and cmd strips the first
	// and last quote of that line, which breaks the script path ("The filename, directory name, or volume label syntax
	// is incorrect"). Run an explicit `cmd /c ""Build.bat" args"` instead: the outer quote pair is what cmd strips.
	const FString ScriptCommand = FString::Printf(TEXT("%s -project %s -engine %s -waitpid %u"),
		*Quote(ScriptPath), *Quote(ProjectPath), *Quote(EngineRoot), FPlatformProcess::GetCurrentProcessId());
	const FString Params = FString::Printf(TEXT("/c start \"TypePaste Build\" cmd.exe /c %s"), *Quote(ScriptCommand));

	FString WorkingDir = FPaths::GetPath(ProjectFile);
	FPaths::MakePlatformFilename(WorkingDir);

	FProcHandle Handle = FPlatformProcess::CreateProc(TEXT("cmd.exe"), *Params, true, false, false, nullptr, 0, *WorkingDir, nullptr);
	if (!Handle.IsValid())
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Could not run the build script: ")) + ScriptPath));
		return false;
	}
	FPlatformProcess::CloseProc(Handle);
	UE_LOG(LogTypePaste, Log, TEXT("Started the build script: cmd.exe %s"), *Params);

	// With unsaved assets the editor asks whether to save. If that is cancelled, the script waits until the editor exits
	IMainFrameModule::Get().RequestCloseEditor();
	return true;
}
