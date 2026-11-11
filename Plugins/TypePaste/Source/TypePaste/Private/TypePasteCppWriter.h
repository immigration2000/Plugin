// TypePaste - 파싱 결과를 언리얼 C++ 헤더(.h) 텍스트로 만드는 생성기

#pragma once

#include "TypePasteTypes.h"

/** 엔진/프로젝트에 이미 있는 타입의 헤더 위치를 알려주는 인터페이스 (에디터에서는 리플렉션으로 구현) */
class ITPExternalTypeInfo
{
public:
	virtual ~ITPExternalTypeInfo() = default;

	/**
	 * TypeName (예: FHitResult) 이 정의된 헤더의 include 경로와 모듈 이름을 찾는다.
	 * @return 찾았으면 true. OutIncludePath 가 비어 있으면 include 가 필요 없는 타입(CoreMinimal 포함 등)
	 */
	virtual bool FindTypeHeader(const FString& TypeName, FString& OutIncludePath, FString& OutModuleName) const = 0;
};

struct FTPCppWriteOptions
{
	/** 예) "MYGAME_API". 비우면 생략 */
	FString ApiMacro;

	/** UPROPERTY 가 없는 멤버에 기본 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=...) 추가 */
	bool bAddMissingUProperty = true;

	/** 초기값이 없는 기본 타입 멤버에 기본값(0, false, nullptr, 첫 열거 항목) 추가 */
	bool bAddDefaultInitializers = true;

	/** UPROPERTY 의 원시 UObject 포인터(UTexture2D*)를 TObjectPtr<> 로 변환 */
	bool bConvertRawObjectPointers = true;

	/** 줄바꿈 문자 */
	FString LineEnding = TEXT("\n");
};

struct FTPCppFile
{
	/** 예) "WeaponTypes.h" */
	FString FileName;
	FString Content;
};

class FTypePasteCppWriter
{
public:
	/** 모든 타입을 한 파일에 담는다 */
	static FTPCppFile WriteSingleFile(const FTPParseResult& Parsed, const FString& FileBaseName, const FTPCppWriteOptions& Options,
		const ITPExternalTypeInfo* TypeInfo, TArray<FTPMessage>& OutMessages);

	/** 타입마다 파일 하나 (파일 이름 = 접두사를 뺀 타입 이름) */
	static TArray<FTPCppFile> WritePerType(const FTPParseResult& Parsed, const FTPCppWriteOptions& Options,
		const ITPExternalTypeInfo* TypeInfo, TArray<FTPMessage>& OutMessages);

	/** 한 파일로 묶을 때 기본 파일 이름 제안 */
	static FString SuggestFileBaseName(const FTPParseResult& Parsed);

	/** 파일 이름으로 쓸 수 있게 정리 (확장자 제거, 영문/숫자/_ 만 유지) */
	static FString SanitizeFileBaseName(const FString& In);
};
