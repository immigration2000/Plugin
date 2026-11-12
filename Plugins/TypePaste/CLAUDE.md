# TypePaste — Claude Code 작업 안내

AI 가 추천한 UENUM/USTRUCT C++ 코드를 붙여넣으면 **블루프린트 열거형/구조체 에셋** 또는 **C++ 헤더**를 만들어 주는
UE 5.7 에디터 전용 플러그인. 사용자 기능 설명은 `README.md`, 남은 일은 `TASKS.md`, 설계 결정은 `DECISIONS.md`,
작업 기록은 `WORKLOG.md`.

> 상위 프로젝트(또는 저장소 루트)에 공용 규칙 문서(AGENTS.md 등)가 있으면 그 규칙이 우선한다.

## 현재 상태 (중요)

- 순수 로직(파서/C++ 생성기)은 엔진 밖에서 테스트 완료 (`Tests/`).
- 에디터 연동 코드(에셋 생성, Slate UI, 모듈)는 UE 5.7 에서 **빌드 통과** (2026-11-11, 오류·경고 없음, 유니티/비유니티 모두).
  → **에디터에서 실제로 동작하는지는 아직 확인하지 않았다.** 다음 작업은 `TASKS.md` P0 의 에디터 수동 검증.
- 저장소: `immigration2000/Plugin` 의 `feature/TypePaste` 브랜치, 호스트 프로젝트 `Plugin.uproject` 의 `Plugins/TypePaste`.
  저장소 규칙상 `master` 에는 병합하지 않는다 (루트 `README.md`).

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
      TypePastePrompt.h          AI 요청 프롬프트(영어로만 답하게 요구), 도움말 문구
      TypePasteBuildLauncher.*   [에디터] 빌드 후 다시 열기: Scripts/Build.bat 실행 + 에디터 종료 요청, 루트에 Build.bat 복사
      TypePasteModule.cpp        Tools 메뉴 노매드 탭 등록
  Scripts/Build.bat              에디터 PID 종료 대기 → UBT 빌드 → UnrealEditor 로 프로젝트 열기 (ASCII 만, CRLF)
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
따옴표가 들어간 긴 명령은 `cmd //c` 에서 깨지기 쉬우므로 임시 `.bat` 에 적어 실행하는 편이 안전하다.

- 이 저장소의 호스트 타깃 이름은 `PluginEditor` (`-Project=<저장소>\Plugin.uproject`).
- UBT 의 적응형 빌드는 git 에서 바뀐 파일을 유니티 빌드에서 빼므로, 유니티 빌드 문제까지 보려면 `-DisableAdaptiveUnity` 를 붙인다.
- `BuildPlugin` 은 `Config/FilterPlugin.ini` 가 없으면 만들어 둔다 (커밋되어 있음).

## 테스트 (파서/생성기를 고쳤으면 반드시)

```bash
cd Tests
./run_tests.sh                 # g++ (WSL/MinGW). clang 은 CXX=clang++
UPDATE=1 ./run_tests.sh        # 출력이 '의도대로' 바뀐 경우에만 골든 갱신 → git diff 로 확인
```

- Windows(g++/WSL 없음)에서는 `Tests\run_tests.bat` — MSVC 로 같은 빌드·골든 비교·퍼징을 한다.
  vswhere 로 Visual Studio 를 찾으므로 일반 명령 프롬프트·Git Bash(`cmd //c "<절대경로>\run_tests.bat"`)에서도 된다.
  갱신은 `set UPDATE=1` 후 실행. (`test_main.cpp` 는 Windows 에서 stdout 을 바이너리 모드로 바꿔 골든과 바이트 단위로 같게 출력한다)
- 저장소 루트 `.gitignore` 가 `*.out` 을 무시하므로 플러그인 `.gitignore` 에 `!Tests/expected/*.out` 예외가 있다. 새 골든 파일이 커밋에 들어갔는지 확인할 것.
- 새 입력 사례는 `Tests/inputs/NN_이름.txt` 로 추가하고 `UPDATE=1` 로 골든 생성 후 출력 내용을 직접 검토한다.

## 코딩 규칙

- **생성 결과(C++ 헤더, 블루프린트 에셋)는 영어(ASCII)만** — 사용자 결정 (DECISIONS D16). 파서 끝의 `EnforceEnglishOnly` 가
  한글 등을 빼고, C++ 생성기는 결과에 ASCII 가 아닌 문자가 남으면 오류를 낸다. 생성기에 새 고정 문구를 넣을 때도 영어로.
- 생성기는 주석과 `ToolTip` 지정자를 동시에 내보내지 않는다 (UHT ToolTip 중복 오류, D17).
- `Scripts/*.bat` 은 ASCII 만, CRLF. Git Bash 의 `sed -i` 는 CR 을 지우므로 bat 수정 후 줄바꿈을 확인할 것.
  Windows 도구는 `%SYS32%\find.exe` 처럼 전체 경로로 (PATH 에 Git 의 find 가 먼저 있을 수 있음). `for /f ('...')` 안은 따옴표로 시작하지 않게.
  `.bat` 을 `start` 로 띄울 때는 `start "제목" cmd.exe /c ""<bat>" 인자..."` 형태로 (`start "제목" "<bat>" "인자"` 는 cmd 가 앞뒤 따옴표를 지워 경로가 깨진다).

- **순수 로직 3개 파일**(Types/Parser/CppWriter)에는 `CoreMinimal.h` 외 엔진 헤더를 넣지 않는다. 그래야 mock 테스트가 유지된다.
  mock 에 없는 FString/TArray API 를 쓰면 `Tests/mock/CoreMinimal.h` 에 **UE 와 같은 시그니처·의미**로 추가한다.
- `FString ==` 은 대소문자 무시. 식별자 비교는 `Equals(X, ESearchCase::CaseSensitive)`.
  `StartsWith/EndsWith/Find/Contains/Replace` 기본값도 IgnoreCase 라는 점 주의.
- UE 는 섀도잉 경고를 오류로 처리한다. 지역 변수·람다 매개변수 이름을 바깥 변수나 SWidget 멤버(`ToolTip`, `Cursor`, `Tag` 등)와 겹치지 않게.
- 유니티 빌드 대비: 파일별 private 네임스페이스(`TypePaste<파일>Private`) 유지, `LOCTEXT_NAMESPACE` 는 파일 끝에서 `#undef`.
- **플러그인 소스의 UI 문구·메시지·주석·스크립트도 영어(ASCII)만** (D20). 테스트 데이터로 한글이 필요하면 `L"\uAC80"` 처럼 이스케이프로 쓴다.
  (`.md` 문서는 한국어 유지. 사용자와의 대화도 한국어)
- 소스 파일은 UTF-8 BOM 유지 (기존 파일과 통일). 새 파일도 BOM 으로 만든다.
- UE 코딩 표준, 탭 들여쓰기.
- 기존 에셋/파일을 조용히 덮어쓰지 않는다 (DECISIONS D6, D12).

## 마무리할 때

- `WORKLOG.md` 에 날짜·작업자(에이전트)·한 일·테스트 결과·다음 할 일을 추가.
- 새 설계 결정은 `DECISIONS.md`, 할 일 변화는 `TASKS.md` 에 반영.
