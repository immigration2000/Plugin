// TypePaste - 붙여넣은 C++ 코드에서 UENUM/USTRUCT 정의를 찾아 해석하는 파서

#pragma once

#include "TypePasteTypes.h"

/**
 * AI 가 준 답변을 통째로 붙여넣어도 동작하도록 관대하게 해석한다.
 *  - 마크다운 코드 펜스(```), 설명 문장, #include / #pragma 줄은 무시
 *  - UENUM / USTRUCT 매크로가 없는 일반 enum class / struct 도 인식
 *  - 함수, 생성자, static 멤버 등 해석할 수 없는 구문은 원문 그대로 보존 (C++ 생성 시 유지)
 */
class FTypePasteParser
{
public:
	static FTPParseResult Parse(const FString& Source);
};
