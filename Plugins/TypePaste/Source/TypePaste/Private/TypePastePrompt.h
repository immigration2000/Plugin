// TypePaste - AI 에게 설계를 요청할 때 붙여 쓰는 프롬프트 (TypePaste 가 잘 읽는 형식으로 답하도록 유도)

#pragma once

#include "CoreMinimal.h"

namespace TypePastePrompt
{
	inline const TCHAR* GetRequestPrompt()
	{
		return TEXT(
			"Design the enums and structs needed for the feature below, for Unreal Engine 5 C++.\n"
			"Put all of the code in a single code block and follow these rules.\n"
			"\n"
			"[Rules]\n"
			"- ENGLISH ONLY: every identifier, comment, DisplayName, ToolTip, Category and string value must be plain English (ASCII).\n"
			"  Do not use any other language in the code, even if the feature description below is written in another language.\n"
			"- Enums: UENUM(BlueprintType) + enum class EName : uint8. Add UMETA(DisplayName = \"Readable Name\") to each entry\n"
			"- Structs: USTRUCT(BlueprintType) + struct FName { GENERATED_BODY() ... }\n"
			"- Members: add UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"...\") and initialize numeric/bool/enum members with a default value\n"
			"- Describe each enum entry/member with a // comment on the line directly above it (it becomes the tooltip).\n"
			"  Do not also add a ToolTip specifier when there is a comment.\n"
			"- Allowed types: bool, int32, int64, uint8, float, double, FString, FName, FText,\n"
			"  FVector, FRotator, FTransform, FLinearColor, FGameplayTag, other enums/structs defined here,\n"
			"  TArray<>, TSet<>, TMap<> (no nested containers), TObjectPtr<UClass>, TSubclassOf<>, TSoftObjectPtr<>, TSoftClassPtr<>\n"
			"- Do not use functions, constructors, static members, bitfields or fixed-size arrays\n"
			"- Structs must not contain each other by value in a cycle\n"
			"- Structs used as data table rows inherit from FTableRowBase\n"
			"\n"
			"[Feature to design]\n"
			"(Describe the feature here. Example: RPG inventory - item types, rarity, item data, slots)\n"
		);
	}

	inline const TCHAR* GetHelpText()
	{
		return TEXT(
			"사용 방법\n"
			"\n"
			"1) [AI 요청 프롬프트 복사] 를 누르고 AI(Claude, ChatGPT 등)에게 붙여넣어 설계를 요청합니다.\n"
			"   (프롬프트 없이 평소처럼 물어본 답변도 대부분 그대로 읽을 수 있습니다)\n"
			"2) AI 답변을 왼쪽 칸에 붙여넣습니다. 코드 블록만 복사해도 되고, 설명이 섞인 답변 전체를 붙여넣어도 됩니다.\n"
			"3) 아래에서 생성 대상을 고릅니다.\n"
			"   - 블루프린트 에셋: 콘텐츠 브라우저에 열거형(UserDefinedEnum)/구조체(UserDefinedStruct) 에셋 생성\n"
			"   - C++ 헤더: 프로젝트 모듈 소스 폴더에 UENUM/USTRUCT 헤더(.h) 생성\n"
			"4) 이 미리보기를 확인한 뒤 [생성] 을 누릅니다.\n"
			"\n"
			"읽을 수 있는 것\n"
			"- UENUM / enum class / enum, USTRUCT / struct (매크로가 없어도 인식)\n"
			"- UMETA(DisplayName, Hidden, ToolTip), UPROPERTY(...), 기본값(= 10.f, { 5 }), // 와 /** */ 주석 → 툴팁\n"
			"- 함수/생성자 등 해석할 수 없는 구문은 C++ 생성 시 원문 그대로 보존합니다\n"
			"\n"
			"생성 결과는 영어만 씁니다\n"
			"- 한글 등 영어가 아닌 주석·DisplayName·ToolTip·Category·기본값은 빼고 만듭니다 (미리보기에 경고로 표시)\n"
			"- 프롬프트 복사 버튼으로 요청하면 AI 가 처음부터 영어로 답합니다\n"
			"\n"
			"C++ 헤더를 만든 뒤에는 [빌드 후 다시 열기] 로 에디터를 닫고 빌드한 다음 프로젝트를 다시 엽니다 (솔루션 파일 필요 없음)\n"
		);
	}
}
