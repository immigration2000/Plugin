// TypePaste - "close editor -> build -> reopen project" workflow after creating C++ headers (runs Scripts/Build.bat)

#pragma once

#include "TypePasteTypes.h"

class FTypePasteBuildLauncher
{
public:
	/** Build script shipped with the plugin (<plugin>/Scripts/Build.bat). Empty if missing */
	static FString GetPluginScriptPath();

	/** Path of the build script in the project root (<project>/Build.bat) */
	static FString GetProjectScriptPath();

	/**
	 * Copies the plugin's script to the project root if there is no Build.bat there (for running it outside the editor).
	 * A different file with the same name is never overwritten.
	 */
	static void EnsureProjectScript(TArray<FTPMessage>& OutMessages);

	/**
	 * Runs the build script in a new console window (it waits for this editor process to exit, builds and reopens the project),
	 * then asks the editor to close. If there are unsaved assets, the editor asks whether to save them.
	 * @return true if the script was started
	 */
	static bool LaunchBuildAndReopen(TArray<FTPMessage>& OutMessages);
};
