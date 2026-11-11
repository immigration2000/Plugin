// TypePaste - AI 에게 설계를 요청할 때 붙여 쓰는 프롬프트 (TypePaste 가 잘 읽는 형식으로 답하도록 유도)

#pragma once

#include "CoreMinimal.h"

namespace TypePastePrompt
{
	inline const TCHAR* GetRequestPrompt()
	{
		return TEXT(
			"언리얼 엔진 5 C++ 기준으로 아래 기능에 필요한 열거형과 구조체를 설계해 주세요.\n"
			"결과 코드는 하나의 코드 블록에 모아 주고, 아래 규칙을 지켜 주세요.\n"
			"\n"
			"[규칙]\n"
			"- 열거형: UENUM(BlueprintType) + enum class EName : uint8 형식. 항목마다 UMETA(DisplayName = \"한글 표시 이름\") 를 붙이기\n"
			"- 구조체: USTRUCT(BlueprintType) + struct FName { GENERATED_BODY() ... } 형식\n"
			"- 멤버: UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"...\") 를 붙이고, 숫자/bool/열거형 멤버는 기본값으로 초기화\n"
			"- 설명은 각 열거형 항목/멤버 바로 위 줄에 // 주석으로 (툴팁이 됩니다)\n"
			"- 사용할 수 있는 타입: bool, int32, int64, uint8, float, double, FString, FName, FText,\n"
			"  FVector, FRotator, FTransform, FLinearColor, FGameplayTag, 이번에 정의한 다른 열거형/구조체,\n"
			"  TArray<>, TSet<>, TMap<> (컨테이너 중첩 금지), TObjectPtr<UClass>, TSubclassOf<>, TSoftObjectPtr<>, TSoftClassPtr<>\n"
			"- 함수, 생성자, static 멤버, 비트필드, 고정 크기 배열은 쓰지 않기\n"
			"- 구조체가 다른 구조체를 값으로 포함할 때 서로를 순환 참조하지 않기\n"
			"- 데이터 테이블 행으로 쓸 구조체는 FTableRowBase 를 상속\n"
			"\n"
			"[설계할 기능]\n"
			"(여기에 원하는 기능을 적어 주세요. 예: RPG 인벤토리 - 아이템 종류, 등급, 아이템 데이터, 슬롯)\n"
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
		);
	}
}
