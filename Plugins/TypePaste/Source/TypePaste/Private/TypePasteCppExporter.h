// TypePaste - 생성된 C++ 헤더를 프로젝트 모듈 소스 폴더에 쓰는 에디터 측 기능

#pragma once

#include "TypePasteCppWriter.h"

struct FTPCppExportOptions
{
	/** 대상 프로젝트 모듈 이름 (.uproject 의 Modules) */
	FString ModuleName;

	/** 모듈의 Public 폴더(없으면 모듈 루트) 아래 하위 폴더. 예) Types */
	FString SubFolder = TEXT("Types");

	/** true: 한 파일로, false: 타입마다 파일 하나 */
	bool bSingleFile = true;

	/** 한 파일로 묶을 때 파일 이름 (비우면 자동) */
	FString FileBaseName;

	/** 같은 이름의 파일이 있으면 덮어쓰기 */
	bool bOverwrite = false;

	/** 생성 후 IDE 에서 열기 */
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
	/** 현재 프로젝트의 C++ 모듈 이름들 (Runtime 모듈 우선) */
	static TArray<FString> GetProjectModuleNames();

	/** 파일이 써질 폴더 (모듈을 찾지 못하면 빈 문자열) */
	static FString GetTargetDirectory(const FTPCppExportOptions& Options);

	/** 파일 내용 생성 (미리보기에도 사용, 디스크에 쓰지 않음) */
	static TArray<FTPCppFile> GenerateFiles(const FTPParseResult& Parsed, const FTPCppExportOptions& Options, TArray<FTPMessage>& OutMessages);

	/** 생성 후 디스크에 쓰기 */
	static FTPCppExportResult Export(const FTPParseResult& Parsed, const FTPCppExportOptions& Options);
};
