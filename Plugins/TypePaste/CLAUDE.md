# TypePaste — Claude Code 작업 안내

AI 가 추천한 UENUM/USTRUCT C++ 코드를 붙여넣으면 **블루프린트 열거형/구조체 에셋** 또는 **C++ 헤더**를 만들어 주는
UE 5.7 에디터 전용 플러그인. 사용자 기능 설명은 `README.md`, 남은 일은 `TASKS.md`, 설계 결정은 `DECISIONS.md`,
작업 기록은 `WORKLOG.md`.

> 상위 프로젝트(또는 저장소 루트)에 공용 규칙 문서(AGENTS.md 등)가 있으면 그 규칙이 우선한다.

## 현재 상태 (중요)

- 순수 로직(파서/C++ 생성기)은 엔진 밖에서 테스트 완료 (`Tests/`).
- 에디터 연동 코드(에셋 생성, Slate UI, 모듈)는 **아직 실제 UE 5.7 에서 한 번도 빌드하지 않았다.**
  → 첫 작업은 빌드 후 오류 수정. 의심 지점 목록은 `TASKS.md` 의 "첫 빌드 체크리스트".

## 구조

```
TypePaste/
  TypePaste.uplugin              Editor 모듈 1개 (TypePaste)
  Source/TypePaste/
    TypePaste.Build.cs
    Public/TypePasteModule.h     모듈, LogTypePaste
    Private/
      TypePasteTypes.h/.cpp      [순수] 데이터 모델 + 유틸 (기본값 변환, 이름 규칙, 의존 정렬)
      TypePasteParser.h/.cpp     [순수] 붙여넣은 텍스트 → FTPParseResult
      TypePasteCppWriter.h/.cpp  [순수] FTPParseResult → .h 텍스트 (ITPExternalTypeInfo 로 외부 타입 헤더 조회)
      TypePasteCppExporter.*     [에디터] 모듈 폴더/리플렉션 include 조회/파일 쓰기
      TypePasteBlueprintBuilder.* [에디터] UserDefinedEnum/Struct 생성, 미리보기
      STypePastePanel.*          [에디터] Slate 탭 UI, 설정 저장(EditorPerProjectIni [TypePaste])
      TypePastePrompt.h          AI 요청 프롬프트, 도움말 문구
      TypePasteModule.cpp        Tools 메뉴 노매드 탭 등록
  Tests/                         엔진 없이 도는 순수 로직 테스트 (mock CoreMinimal.h)
  Samples/Example_AI_Answer.txt  수동 검증용 입력
```

데이터 흐름: `Parse(text)` → `FTPParseResult` → (BP) `FTypePasteBlueprintBuilder::Build` / (C++) `FTypePasteCppExporter::Export`
→ 둘 다 `FTPMessage`(Info/Warning/Error) 목록을 돌려주고 패널이 미리보기 칸에 표시.

## 빌드

Windows, 엔진 경로는 환경에 맞게 바꾼다.

```bat
:: 플러그인 단독 컴파일 (호스트 프로젝트 없이 가장 빠른 확인)
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
  -Plugin="<경로>\TypePaste\TypePaste.uplugin" -Package="%TEMP%\TypePastePackage" -TargetPlatforms=Win64

:: 호스트 프로젝트(Plugins\TypePaste 에 넣은 상태)의 에디터 타깃 빌드
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" <프로젝트명>Editor Win64 Development ^
  -Project="<경로>\<프로젝트명>.uproject" -WaitMutex
```

Git Bash 에서 실행할 때는 `cmd //c "..."` 로 감싸거나 PowerShell 을 쓴다.

## 테스트 (파서/생성기를 고쳤으면 반드시)

```bash
cd Tests
./run_tests.sh                 # g++ (WSL/MinGW). clang 은 CXX=clang++
UPDATE=1 ./run_tests.sh        # 출력이 '의도대로' 바뀐 경우에만 골든 갱신 → git diff 로 확인
```

- MSVC 로 돌릴 때: Developer Command Prompt 에서
  `cl /std:c++20 /EHsc /utf-8 /I mock /I ..\Source\TypePaste\Private test_main.cpp ..\Source\TypePaste\Private\TypePaste{Types,Parser,CppWriter}.cpp`
  (와일드카드는 cmd 에서 안 되므로 세 파일을 각각 적는다)
- 새 입력 사례는 `Tests/inputs/NN_이름.txt` 로 추가하고 `UPDATE=1` 로 골든 생성 후 출력 내용을 직접 검토한다.

## 코딩 규칙

- **순수 로직 3개 파일**(Types/Parser/CppWriter)에는 `CoreMinimal.h` 외 엔진 헤더를 넣지 않는다. 그래야 mock 테스트가 유지된다.
  mock 에 없는 FString/TArray API 를 쓰면 `Tests/mock/CoreMinimal.h` 에 **UE 와 같은 시그니처·의미**로 추가한다.
- `FString ==` 은 대소문자 무시. 식별자 비교는 `Equals(X, ESearchCase::CaseSensitive)`.
  `StartsWith/EndsWith/Find/Contains/Replace` 기본값도 IgnoreCase 라는 점 주의.
- UE 는 섀도잉 경고를 오류로 처리한다. 지역 변수·람다 매개변수 이름을 바깥 변수나 SWidget 멤버(`ToolTip`, `Cursor`, `Tag` 등)와 겹치지 않게.
- 유니티 빌드 대비: 파일별 private 네임스페이스(`TypePaste<파일>Private`) 유지, `LOCTEXT_NAMESPACE` 는 파일 끝에서 `#undef`.
- 소스 파일은 **UTF-8 BOM** 유지 (한글 문자열 리터럴이 MSVC 에서 깨지지 않도록). 새 파일도 BOM 으로 만든다.
- UE 코딩 표준, 탭 들여쓰기. 사용자에게 보이는 문구와 주석은 한국어.
- 기존 에셋/파일을 조용히 덮어쓰지 않는다 (DECISIONS D6, D12).

## 마무리할 때

- `WORKLOG.md` 에 날짜·작업자(에이전트)·한 일·테스트 결과·다음 할 일을 추가.
- 새 설계 결정은 `DECISIONS.md`, 할 일 변화는 `TASKS.md` 에 반영.
