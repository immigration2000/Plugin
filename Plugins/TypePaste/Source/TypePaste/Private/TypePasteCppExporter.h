// TypePaste - editor-side writing of generated C++ headers into a project module's source folder

#pragma once

#include "TypePasteCppWriter.h"

struct FTPCppExportOptions
{
	/** Target project module name (Modules in the .uproject) */
	FString ModuleName;

	/** Subfolder under the module's Public folder (module root if there is none). e.g. Types */
	FString SubFolder = TEXT("Types");

	/** true: one file, false: one file per type */
	bool bSingleFile = true;

	/** File name when writing a single file (automatic if empty) */
	FString FileBaseName;

	/** Overwrite files with the same name */
	bool bOverwrite = false;

	/** Open in the IDE after creating */
	bool bOpenInIDE = true;
};

struct FTPCppExportResult
{
	TArray<FTPMessage> Messages;
	TArray<FString> WrittenFiles;
};

class FTypePasteCppExporter
{
public:
	/** C++ module names of the current project (Runtime modules first) */
	static TArray<FString> GetProjectModuleNames();

	/** Folder the files are written to (empty if the module is not found) */
	static FString GetTargetDirectory(const FTPCppExportOptions& Options);

	/** Generates file contents (also used for the preview, does not write to disk) */
	static TArray<FTPCppFile> GenerateFiles(const FTPParseResult& Parsed, const FTPCppExportOptions& Options, TArray<FTPMessage>& OutMessages);

	/** Generates and writes to disk */
	static FTPCppExportResult Export(const FTPParseResult& Parsed, const FTPCppExportOptions& Options);
};
