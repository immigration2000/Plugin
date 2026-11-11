// TypePaste - 파싱 결과로 블루프린트 열거형(UserDefinedEnum) / 구조체(UserDefinedStruct) 에셋을 만든다

#pragma once

#include "TypePasteTypes.h"

class UObject;

struct FTPBlueprintOptions
{
	/** 에셋을 만들 콘텐츠 경로. 예) /Game/Types */
	FString PackagePath = TEXT("/Game/Types");

	/** 에셋 이름 규칙 */
	ETPAssetNaming Naming = ETPAssetNaming::KeepCppName;

	/** 생성 후 바로 저장 */
	bool bSaveAssets = true;
};

struct FTPBlueprintResult
{
	TArray<FTPMessage> Messages;
	TArray<UObject*> CreatedAssets;
	int32 NumCreatedEnums = 0;
	int32 NumCreatedStructs = 0;
	int32 NumReused = 0;
};

class FTypePasteBlueprintBuilder
{
public:
	/** 무엇이 만들어질지 보여주는 미리보기 텍스트 (에셋을 만들거나 로드하지 않음) */
	static FString BuildPreview(const FTPParseResult& Parsed, const FTPBlueprintOptions& Options, TArray<FTPMessage>& OutMessages);

	/** 에셋 생성 */
	static FTPBlueprintResult Build(const FTPParseResult& Parsed, const FTPBlueprintOptions& Options);

	/** 경로 문자열 정리 (/All/Game/... → /Game/..., 끝의 / 제거) */
	static FString NormalizePackagePath(const FString& InPath);
};
