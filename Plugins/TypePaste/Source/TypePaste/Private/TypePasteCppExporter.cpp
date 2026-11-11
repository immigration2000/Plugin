// TypePaste - C++ header export implementation

#include "TypePasteCppExporter.h"
#include "TypePasteBuildLauncher.h"
#include "TypePasteModule.h"

#include "HAL/FileManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ModuleDescriptor.h"
#include "ProjectDescriptor.h"
#include "SourceCodeNavigation.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace TypePasteCppExporterPrivate
{
	static bool IsUpperAscii(TCHAR C)
	{
		return C >= TEXT('A') && C <= TEXT('Z');
	}

	/** Returns the module name if it is a native (C++) type */
	static bool GetNativeModuleName(const UObject* Object, FString& OutModule)
	{
		if (!Object)
		{
			return false;
		}
		const FString PackageName = Object->GetPackage()->GetName();
		if (!PackageName.StartsWith(TEXT("/Script/")))
		{
			return false;
		}
		OutModule = PackageName.RightChop(8);
		return true;
	}

	static UScriptStruct* FindNativeStruct(const FString& CppName)
	{
		if (CppName.Len() > 1 && CppName[0] == TEXT('F') && IsUpperAscii(CppName[1]))
		{
			UScriptStruct* Found = FindFirstObject<UScriptStruct>(*CppName.RightChop(1), EFindFirstObjectOptions::NativeFirst);
			FString Module;
			if (GetNativeModuleName(Found, Module))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static UEnum* FindNativeEnum(const FString& CppName)
	{
		UEnum* Found = FindFirstObject<UEnum>(*CppName, EFindFirstObjectOptions::NativeFirst);
		FString Module;
		return GetNativeModuleName(Found, Module) ? Found : nullptr;
	}

	/** Finds the header of an engine/project type through reflection metadata (ModuleRelativePath) */
	class FEditorTypeInfo : public ITPExternalTypeInfo
	{
	public:
		virtual bool FindTypeHeader(const FString& TypeName, FString& OutIncludePath, FString& OutModuleName) const override
		{
			UField* Field = FindNativeStruct(TypeName);
			if (!Field)
			{
				Field = FindNativeEnum(TypeName);
			}
			if (!Field || !GetNativeModuleName(Field, OutModuleName))
			{
				return false;
			}

			FString RelativePath = Field->GetMetaData(TEXT("ModuleRelativePath"));
			if (RelativePath.EndsWith(TEXT("NoExportTypes.h")))
			{
				// Core types such as FVector: CoreMinimal is enough
				OutIncludePath.Reset();
				return true;
			}
			if (RelativePath.StartsWith(TEXT("Public/")))
			{
				RelativePath.RightChopInline(7);
			}
			else if (RelativePath.StartsWith(TEXT("Classes/")))
			{
				RelativePath.RightChopInline(8);
			}
			else
			{
				return false;
			}
			OutIncludePath = RelativePath;
			return true;
		}
	};

	static FString SanitizeSubFolder(const FString& In)
	{
		TArray<FString> Parts;
		In.Replace(TEXT("\\"), TEXT("/")).ParseIntoArray(Parts, TEXT("/"), true);

		TArray<FString> Clean;
		for (const FString& Part : Parts)
		{
			const FString Trimmed = Part.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT(".")) || Trimmed.Equals(TEXT("..")) || Trimmed.Contains(TEXT(":")))
			{
				continue;
			}
			Clean.Add(Trimmed);
		}
		return FString::Join(Clean, TEXT("/"));
	}
}

TArray<FString> FTypePasteCppExporter::GetProjectModuleNames()
{
	TArray<FString> RuntimeModules;
	TArray<FString> OtherModules;

	if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
	{
		for (const FModuleDescriptor& Module : Project->Modules)
		{
			const FString Name = Module.Name.ToString();
			const FString Dir = FPaths::Combine(FPaths::GameSourceDir(), Name);
			if (!IFileManager::Get().DirectoryExists(*Dir))
			{
				continue;
			}
			if (Module.Type == EHostType::Runtime)
			{
				RuntimeModules.Add(Name);
			}
			else
			{
				OtherModules.Add(Name);
			}
		}
	}

	for (const FString& Name : OtherModules)
	{
		RuntimeModules.Add(Name);
	}
	return RuntimeModules;
}

FString FTypePasteCppExporter::GetTargetDirectory(const FTPCppExportOptions& Options)
{
	using namespace TypePasteCppExporterPrivate;

	if (Options.ModuleName.IsEmpty())
	{
		return FString();
	}
	const FString ModuleDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GameSourceDir(), Options.ModuleName));
	if (!IFileManager::Get().DirectoryExists(*ModuleDir))
	{
		return FString();
	}

	FString Dir = ModuleDir;
	const FString PublicDir = FPaths::Combine(ModuleDir, TEXT("Public"));
	if (IFileManager::Get().DirectoryExists(*PublicDir))
	{
		Dir = PublicDir;
	}
	const FString SubFolder = SanitizeSubFolder(Options.SubFolder);
	if (!SubFolder.IsEmpty())
	{
		Dir = FPaths::Combine(Dir, SubFolder);
	}
	return Dir;
}

TArray<FTPCppFile> FTypePasteCppExporter::GenerateFiles(const FTPParseResult& Parsed, const FTPCppExportOptions& Options, TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteCppExporterPrivate;

	FTPCppWriteOptions WriteOptions;
	WriteOptions.ApiMacro = Options.ModuleName.IsEmpty() ? FString() : Options.ModuleName.ToUpper() + TEXT("_API");
	WriteOptions.LineEnding = LINE_TERMINATOR;

	// A name that clashes with an existing C++ type makes UHT report a duplicate
	for (const FTPEnumDef& Def : Parsed.Enums)
	{
		FString Module;
		if (GetNativeModuleName(FindNativeEnum(Def.Name), Module))
		{
			OutMessages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("C++ enum '")) + Def.Name + TEXT("' already exists in the ") + Module
				+ TEXT(" module. Creating one with the same name fails the build with a duplicate error.")));
		}
	}
	for (const FTPStructDef& Def : Parsed.Structs)
	{
		FString Module;
		if (GetNativeModuleName(FindNativeStruct(Def.Name), Module))
		{
			OutMessages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("C++ struct '")) + Def.Name + TEXT("' already exists in the ") + Module
				+ TEXT(" module. Creating one with the same name fails the build with a duplicate error.")));
		}
	}

	FEditorTypeInfo TypeInfo;
	TArray<FTPCppFile> Files;
	if (Options.bSingleFile)
	{
		Files.Add(FTypePasteCppWriter::WriteSingleFile(Parsed, Options.FileBaseName, WriteOptions, &TypeInfo, OutMessages));
	}
	else
	{
		Files = FTypePasteCppWriter::WritePerType(Parsed, WriteOptions, &TypeInfo, OutMessages);
	}
	return Files;
}

FTPCppExportResult FTypePasteCppExporter::Export(const FTPParseResult& Parsed, const FTPCppExportOptions& Options)
{
	FTPCppExportResult Result;

	const FString Dir = GetTargetDirectory(Options);
	if (Dir.IsEmpty())
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("Could not find the C++ module folder. Check that this is a C++ project and that a module is selected.")));
		return Result;
	}

	TArray<FTPCppFile> Files = GenerateFiles(Parsed, Options, Result.Messages);
	if (Files.Num() == 0)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("There are no files to create.")));
		return Result;
	}

	// If the generator reported errors (non-English text, dependency cycles ...), do not write files that would break the build
	for (const FTPMessage& Message : Result.Messages)
	{
		if (Message.Severity == ETPSeverity::Error)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("No files were written because of the errors.")));
			return Result;
		}
	}

	// If even one file would be overwritten, write nothing (avoids partially updated files)
	if (!Options.bOverwrite)
	{
		bool bConflict = false;
		for (const FTPCppFile& File : Files)
		{
			const FString Path = FPaths::Combine(Dir, File.FileName);
			if (FPaths::FileExists(Path))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("File already exists: ")) + Path
					+ TEXT("  (turn on 'Overwrite existing files' or change the file name)")));
				bConflict = true;
			}
		}
		if (bConflict)
		{
			return Result;
		}
	}

	if (!IFileManager::Get().MakeDirectory(*Dir, true))
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Could not create the folder: ")) + Dir));
		return Result;
	}

	for (const FTPCppFile& File : Files)
	{
		const FString Path = FPaths::Combine(Dir, File.FileName);
		// Content is ASCII only (D16). The UTF-8 BOM is kept for compatibility with earlier output
		if (FFileHelper::SaveStringToFile(File.Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8))
		{
			Result.WrittenFiles.Add(Path);
			UE_LOG(LogTypePaste, Log, TEXT("Created C++ header: %s"), *Path);
		}
		else
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Could not write the file: ")) + Path));
		}
	}

	if (Result.WrittenFiles.Num() > 0)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Info,
			TEXT("New UENUM/USTRUCT headers only take effect after closing the editor and building. Press [Build and Reopen] to close the editor, build, and reopen the project.")));
		FTypePasteBuildLauncher::EnsureProjectScript(Result.Messages);

		if (Options.bOpenInIDE)
		{
			// Without a solution, opening the IDE starts by generating project files, which is slow, so skip it
			if (FSourceCodeNavigation::DoesModuleSolutionExist())
			{
				for (const FString& Path : Result.WrittenFiles)
				{
					FSourceCodeNavigation::OpenSourceFile(Path);
				}
			}
			else
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, TEXT("There is no solution file (.sln), so the file was not opened in the IDE. Building does not need a solution.")));
			}
		}
	}
	return Result;
}
