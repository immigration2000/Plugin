// TypePaste - C++ 헤더 내보내기 구현

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

	/** 네이티브(C++) 타입이면 모듈 이름을 돌려준다 */
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

	/** 리플렉션 메타데이터(ModuleRelativePath)로 엔진/프로젝트 타입의 헤더를 찾는다 */
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
				// FVector 같은 코어 타입: CoreMinimal 로 충분
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

	// 이미 있는 C++ 타입과 이름이 겹치면 UHT 에서 중복 오류가 난다
	for (const FTPEnumDef& Def : Parsed.Enums)
	{
		FString Module;
		if (GetNativeModuleName(FindNativeEnum(Def.Name), Module))
		{
			OutMessages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("C++ 열거형 '")) + Def.Name + TEXT("' 이 이미 ") + Module
				+ TEXT(" 모듈에 있습니다. 같은 이름으로 만들면 빌드 시 중복 오류가 납니다.")));
		}
	}
	for (const FTPStructDef& Def : Parsed.Structs)
	{
		FString Module;
		if (GetNativeModuleName(FindNativeStruct(Def.Name), Module))
		{
			OutMessages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("C++ 구조체 '")) + Def.Name + TEXT("' 이 이미 ") + Module
				+ TEXT(" 모듈에 있습니다. 같은 이름으로 만들면 빌드 시 중복 오류가 납니다.")));
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
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("C++ 모듈 폴더를 찾지 못했습니다. C++ 프로젝트인지, 모듈을 골랐는지 확인해 주세요.")));
		return Result;
	}

	TArray<FTPCppFile> Files = GenerateFiles(Parsed, Options, Result.Messages);
	if (Files.Num() == 0)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("만들 파일이 없습니다.")));
		return Result;
	}

	// 생성기가 오류를 냈으면(영어가 아닌 문자, 순환 참조 등) 빌드가 깨질 파일은 쓰지 않는다
	for (const FTPMessage& Message : Result.Messages)
	{
		if (Message.Severity == ETPSeverity::Error)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("오류가 있어 파일을 쓰지 않았습니다.")));
			return Result;
		}
	}

	// 하나라도 덮어쓰게 되면 아무것도 쓰지 않는다 (일부만 바뀌는 상황 방지)
	if (!Options.bOverwrite)
	{
		bool bConflict = false;
		for (const FTPCppFile& File : Files)
		{
			const FString Path = FPaths::Combine(Dir, File.FileName);
			if (FPaths::FileExists(Path))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("이미 있는 파일입니다: ")) + Path
					+ TEXT("  ('기존 파일 덮어쓰기' 를 켜거나 파일 이름을 바꿔 주세요)")));
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
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("폴더를 만들지 못했습니다: ")) + Dir));
		return Result;
	}

	for (const FTPCppFile& File : Files)
	{
		const FString Path = FPaths::Combine(Dir, File.FileName);
		// 내용은 ASCII 만 (D16). UTF-8 BOM 은 예전 방식과의 호환용으로 유지
		if (FFileHelper::SaveStringToFile(File.Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8))
		{
			Result.WrittenFiles.Add(Path);
			UE_LOG(LogTypePaste, Log, TEXT("C++ 헤더 생성: %s"), *Path);
		}
		else
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("파일을 쓰지 못했습니다: ")) + Path));
		}
	}

	if (Result.WrittenFiles.Num() > 0)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Info,
			TEXT("새 UENUM/USTRUCT 헤더는 에디터를 닫고 빌드해야 반영됩니다. [빌드 후 다시 열기] 를 누르면 에디터를 닫고 빌드한 뒤 프로젝트를 다시 엽니다.")));
		FTypePasteBuildLauncher::EnsureProjectScript(Result.Messages);

		if (Options.bOpenInIDE)
		{
			// 솔루션이 없으면 IDE 를 열 때 프로젝트 파일 생성부터 시작해 오래 걸리므로 건너뛴다
			if (FSourceCodeNavigation::DoesModuleSolutionExist())
			{
				for (const FString& Path : Result.WrittenFiles)
				{
					FSourceCodeNavigation::OpenSourceFile(Path);
				}
			}
			else
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, TEXT("솔루션 파일(.sln)이 없어 IDE 로 열지 않았습니다. 빌드는 솔루션 없이 됩니다.")));
			}
		}
	}
	return Result;
}
