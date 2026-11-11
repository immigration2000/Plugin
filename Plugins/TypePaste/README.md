# TypePaste — AI가 추천한 열거형/구조체를 붙여넣어 바로 만드는 에디터 플러그인

대상 엔진: **Unreal Engine 5.7** (에디터 전용 플러그인)

AI(Claude, ChatGPT 등)에게 "이런 기능에 필요한 열거형/구조체 설계해 줘" 라고 물어서 받은 답변을 그대로 붙여넣으면,

- **블루프린트 에셋** (UserDefinedEnum / UserDefinedStruct) 으로 콘텐츠 브라우저에 만들거나
- **C++ 헤더** (UENUM / USTRUCT 가 담긴 `.h`) 로 프로젝트 모듈 소스 폴더에 만들어 줍니다.

생성 전에 무엇이 어떻게 만들어질지 미리보기로 보여 줍니다.

---

## 설치

1. 이 `TypePaste` 폴더를 프로젝트의 `Plugins/` 아래에 복사합니다.
   ```
   MyProject/
     Plugins/
       TypePaste/
         TypePaste.uplugin
         Source/...
   ```
2. 에디터를 닫은 상태에서 플러그인의 `Scripts/Build.bat` 을 프로젝트 루트에 복사해 실행합니다 (솔루션 파일 필요 없음, 빌드 후 프로젝트가 열림).
   또는 `.uproject` 우클릭 → **Generate Visual Studio project files** → IDE 에서 빌드, 혹은 에디터를 켜고 "모듈을 다시 빌드할까요?" 에서 **예**
   - 블루프린트 전용 프로젝트라면 먼저 C++ 클래스를 하나 추가해 C++ 프로젝트로 만들어야 플러그인을 빌드할 수 있습니다.
3. 에디터 메뉴 **Tools → TypePaste (타입 붙여넣기)** 로 창을 엽니다.

## 사용 방법

1. **[AI 요청 프롬프트 복사]** 를 누르고, 복사된 요청문의 `[설계할 기능]` 부분을 채워 AI 에게 보냅니다.
   프롬프트 없이 평소처럼 물어본 답변도 대부분 그대로 읽습니다.
2. AI 답변을 왼쪽 칸에 붙여넣습니다. 코드 블록만 복사해도 되고, 설명 문장·마크다운이 섞인 답변 전체를 붙여넣어도 됩니다.
3. 아래 **생성 대상** 을 고릅니다.
   - **블루프린트 에셋**: 저장 경로(`/Game/Types` 등), 에셋 이름 규칙(그대로 / `E_`·`S_` / `E_`·`F_` / 접두사 없음)
   - **C++ 헤더**: 모듈, 하위 폴더, 한 파일로 묶을지 타입마다 나눌지
4. 오른쪽 미리보기에서 결과와 경고를 확인하고 **[생성]** 을 누릅니다.
5. (C++ 헤더) 생성 후 "지금 빌드할까요?" 에서 **예** 또는 **[빌드 후 다시 열기]** 를 누르면 에디터가 닫히고, 새 콘솔 창에서 빌드한 뒤 프로젝트를 다시 엽니다.

## 생성 결과는 영어만

개발용 코드·에셋에는 영어만 씁니다. 붙여넣은 답변에 한글 등 영어가 아닌 텍스트가 있으면 **빼고** 만들고 미리보기에 경고로 알려 줍니다.

- 주석, `UMETA(DisplayName/ToolTip)`, `meta = (...)` 안의 값, `Category` → 뺌 (`Category` 는 구조체 이름으로 채움)
- 기본값(`TEXT("검")` 등) → 기본값 없이 생성, 함수 같은 원문 구문 → 뺌
- **[AI 요청 프롬프트 복사]** 의 요청문은 AI 에게 처음부터 영어로만 답하도록 요구합니다.

## 빌드 후 다시 열기 (Build.bat)

새 UENUM/USTRUCT 는 에디터를 닫고 빌드해야 반영됩니다. TypePaste 는 이 과정을 버튼 하나로 합니다.

- 에디터에서: C++ 헤더 생성 직후 확인 창 또는 **[빌드 후 다시 열기]** 버튼 → 저장 여부 확인 → 에디터 종료 → 콘솔 창에서 빌드 → 성공하면 프로젝트가 다시 열림. 실패하면 콘솔 창에 오류가 남습니다.
- 직접 실행: C++ 헤더를 처음 만들 때 프로젝트 루트에 `Build.bat` 이 복사됩니다 (이미 다른 `Build.bat` 이 있으면 건드리지 않음). 에디터를 닫고 더블클릭하세요.
  - 옵션: `-noopen`(빌드만), `-config DebugGame`, `-engine "<엔진 폴더>"`, `-project "<x.uproject>"`
  - 엔진 위치는 `.uproject` 의 `EngineAssociation` 으로 찾습니다 (레지스트리, 런처 설치 목록, `Program Files\Epic Games\UE_<버전>` 순).
- 솔루션(.sln)·Visual Studio 프로젝트 파일 생성이 필요 없습니다. 내부적으로 엔진의 `Build.bat <프로젝트>Editor Win64 Development` 를 실행합니다.

옵션(경로, 이름 규칙, 모듈 등)은 프로젝트별로 기억됩니다.

## 읽을 수 있는 코드

- `UENUM(...) enum class EName : uint8 { ... }`, 매크로 없는 `enum class` / `enum`
- `USTRUCT(...) struct FName : public FTableRowBase { GENERATED_BODY() ... }`, 매크로 없는 `struct`
- `UMETA(DisplayName = "...")`, `UMETA(Hidden)`, `UMETA(ToolTip = "...")`
- `UPROPERTY(...)` 지정자, 기본값 `= 10.f`, `{ 5 }`, `float X, Y, Z;`, 비트필드 `uint8 bFlag : 1;`
- `//` 와 `/** */` 주석 → 툴팁 / 설명
- 함수, 생성자, 연산자, static 멤버처럼 해석할 수 없는 구문은 **원문 그대로 보존** (C++ 생성 시에만 유지)
- `#include`, `#pragma`, 마크다운 코드 펜스(```), 설명 문장은 무시

## 블루프린트 에셋 변환 규칙

| C++ | 블루프린트 |
|---|---|
| `bool` | Boolean |
| `int32`, `int` | Integer |
| `int8`, `int16`, `uint16` | Integer (안내 메시지) |
| `int64` | Integer64 |
| `uint32`, `uint64` | Integer64 (안내 메시지) |
| `uint8` | Byte |
| `float`, `double` | Float (UE5 블루프린트는 double 정밀도) |
| `FString` / `FName` / `FText` | String / Name / Text |
| 같이 붙여넣은 `EXxx` / `FXxx` | 이번에 만드는 블루프린트 열거형 / 구조체 |
| 엔진·프로젝트 C++ 타입 (`FVector`, `FGameplayTag`, `ECollisionChannel` …) | 해당 타입 (BlueprintType 이어야 함) |
| 이미 있는 블루프린트 열거형/구조체 에셋 | 이름(또는 `E_`/`S_`/`F_` 변형)으로 찾아 연결 |
| `UClass*`, `TObjectPtr<UClass>` | Object Reference |
| `TSubclassOf<>` | Class Reference |
| `TSoftObjectPtr<>` / `TSoftClassPtr<>` | Soft Object / Soft Class Reference |
| `TWeakObjectPtr<>` | Object Reference (안내 메시지) |
| `TArray<>` / `TSet<>` / `TMap<>` | Array / Set / Map |
| `uint8 bFlag : 1` | Boolean |
| `int32 Grid[4]` | Array (안내 메시지) |

- 기본값: 숫자 · bool · 문자열 · Name · Text · 열거형은 적용, 구조체(`FVector(…)` 등)와 컨테이너 기본값은 적용하지 않고 안내합니다.
- 열거형은 `UMETA(DisplayName)` 이 (영어로) 있으면 그것을, 없으면 항목 이름을 블루프린트 표시 이름으로 씁니다. `UMETA(Hidden)` 항목(MAX 등)은 뺍니다.
- 구조체를 서로 값으로 포함하면 의존 순서대로 만들고, 순환 참조는 오류로 알려 줍니다.
- 같은 이름의 에셋이 이미 있으면 **덮어쓰지 않고** 기존 에셋을 참조합니다.

## C++ 헤더 생성

- 위치: `Source/<모듈>/Public/<하위 폴더>/` (Public 폴더가 없는 모듈은 모듈 루트 아래)
- 자동으로 해 주는 것
  - `#pragma once`, `CoreMinimal.h`, `<파일명>.generated.h`, `MODULENAME_API`
  - 누락된 `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "...")` 추가 (블루프린트 비호환 타입은 BlueprintReadWrite 없이)
  - 초기값 없는 숫자/bool/포인터/열거형 멤버에 기본값
  - 원시 UObject 포인터 → `TObjectPtr<>`, `TEnumAsByte<enum class>` → enum class
  - 구조체를 의존 순서로 정렬, 필요한 `#include` 와 전방 선언 (엔진 타입은 리플렉션 정보로 헤더 위치를 찾음)
  - Build.cs 에 필요한 모듈(GameplayTags 등) 안내, 이미 같은 이름의 C++ 타입이 있으면 경고
- 내용은 영어(ASCII)만 들어갑니다. 주석과 `ToolTip` 지정자가 함께 있으면 주석은 넣지 않습니다 (UHT 의 ToolTip 중복 오류 방지).
- 솔루션 파일이 없으면 "생성 후 IDE 에서 열기" 는 건너뜁니다 (프로젝트 파일 생성이 오래 걸리므로).
- 새 UENUM/USTRUCT 는 **에디터를 닫고 IDE 에서 빌드**해야 반영됩니다. (라이브 코딩으로는 새 리플렉션 타입이 잘 반영되지 않습니다)

## 제한 사항

- 블루프린트 구조체는 상속·함수가 없으므로 부모 구조체와 함수는 무시됩니다. (데이터 테이블 행 구조체로는 그대로 쓸 수 있음)
- 블루프린트 열거형은 값이 0부터 순서대로라 `= 5` 같은 지정값은 무시되고, Bitflags 는 일반 열거형으로 만듭니다.
- 클래스(UCLASS)는 만들지 않습니다.
- 중첩 컨테이너(`TArray<TArray<int32>>`)는 블루프린트/리플렉션 모두 지원하지 않습니다.

## 빌드 상태 참고

- 파서와 C++ 생성기는 엔진 밖에서 단위 테스트(샘플 입력, CRLF, 2만 회 무작위 입력 퍼징)를 거쳤습니다.
- 에디터 연동 코드(에셋 생성, UI)는 UE 5.7 에서 오류·경고 없이 빌드됩니다 (Win64, 2026-11-11).
  에디터 안에서의 실제 동작(에셋 생성, 메뉴, 설정 저장)은 아직 수동 검증 중입니다. 이상한 점이 있으면 메시지와 함께 알려 주세요.

## 소스 구조

| 파일 | 역할 |
|---|---|
| `TypePasteTypes.h/.cpp` | 파싱 결과 데이터 모델, 공용 유틸 (기본값 변환, 이름 규칙, 의존 정렬) |
| `TypePasteParser.h/.cpp` | 붙여넣은 텍스트 → 열거형/구조체 정의 (관대한 토크나이저 + 파서) |
| `TypePasteCppWriter.h/.cpp` | 정의 → C++ 헤더 텍스트 |
| `TypePasteCppExporter.h/.cpp` | 모듈 폴더 찾기, 리플렉션으로 include 경로 찾기, 파일 쓰기 |
| `TypePasteBlueprintBuilder.h/.cpp` | 정의 → UserDefinedEnum / UserDefinedStruct 에셋 |
| `STypePastePanel.h/.cpp` | 에디터 탭 UI |
| `TypePastePrompt.h` | AI 요청 프롬프트, 도움말 |
| `TypePasteModule.h/.cpp` | 모듈 시작, Tools 메뉴 탭 등록 |
| `TypePasteBuildLauncher.h/.cpp` | 빌드 후 다시 열기 (Scripts/Build.bat 실행, 에디터 종료 요청) |
| `Scripts/Build.bat` | 에디터 종료 대기 → UBT 빌드 → 프로젝트 열기 (솔루션 불필요) |

파서·C++ 생성기(`TypePasteTypes`, `TypePasteParser`, `TypePasteCppWriter`)는 에디터 API 에 의존하지 않는 순수 로직이라 단독으로 테스트할 수 있습니다.

`Samples/Example_AI_Answer.txt` 를 붙여넣어 바로 동작을 확인해 볼 수 있습니다.
