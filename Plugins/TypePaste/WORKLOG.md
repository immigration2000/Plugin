# WORKLOG

## 2026-11-11 — Claude (claude.ai 채팅 세션) — 초기 구현

**요청**: 구조 설계 때 AI 가 추천한 열거형/구조체를 복사·붙여넣기로 간단히 생성하고 싶다.
**사용자 결정**: 블루프린트 에셋·C++ 헤더 둘 다(생성 시 선택), UE 5.7 이상, 새 플러그인.

**한 일**
- 플러그인 골격: `TypePaste.uplugin`, Editor 모듈, Tools 메뉴 노매드 탭
- 관대한 토크나이저/파서: 코드 펜스·설명 문장·`#include` 무시, UENUM/USTRUCT 유무 무관,
  UMETA/UPROPERTY/주석/기본값/비트필드/다중 선언/함수·생성자·연산자(원문 보존)/UCLASS 건너뛰기
- C++ 헤더 생성기: 단일 파일/타입별 파일, 의존 순서 정렬, include·전방 선언, 정규화(D11), 경고 메시지
- 블루프린트 빌더: UserDefinedEnum/Struct 생성, 핀 타입 해석(기본 타입·로컬/기존 타입·오브젝트 참조·컨테이너),
  기본값·툴팁, 기존 에셋 재사용, 저장, 미리보기
- Slate 패널: 입력/미리보기 분할, 프롬프트 복사, 대상·옵션 UI, 결과 알림, 설정 저장
- README, 샘플 입력, 테스트 하네스(`Tests/`)

**테스트**
- `Tests/run_tests.sh`: 입력 5종 골든 비교 통과 (일반 AI 답변, 까다로운 구문, 설명뿐인 입력, 의존 순서, CRLF)
- 퍼징 2만 회(ASan/UBSan) 크래시·무한루프 없음, `-Wshadow` 경고 없음
- **에디터 연동 코드는 엔진에서 빌드하지 못함** (작업 환경에 UE 없음)

**다음**: TASKS.md P0 — 첫 빌드와 에디터 수동 검증

## 2026-11-11 — Claude Code (Opus 5.5) — 저장소 배치, 첫 UE 5.7 빌드

**한 일**
- 저장소 규칙(루트 `README.md`: `master` 는 빈 템플릿, 플러그인마다 브랜치)에 따라 `master` 에서 `feature/TypePaste` 생성,
  압축본을 `Plugins/TypePaste` 에 원본 그대로 커밋
- 골든 출력 `Tests/expected/*.out` 이 루트 `.gitignore` 의 `*.out` 규칙에 걸려 커밋에서 빠져 있던 것을 발견 →
  플러그인 `.gitignore` 에 `!Tests/expected/*.out` 예외 추가 후 커밋
- Windows 용 테스트 스크립트 `Tests/run_tests.bat` 추가 (MSVC, vswhere 로 VS 탐색, UPDATE/FUZZ_ITERS 지원),
  `test_main.cpp` 가 Windows 에서 stdout 을 바이너리 모드로 쓰도록 수정 (텍스트 모드의 \n→\r\n 변환으로 골든 비교가 깨지던 문제).
  `.gitattributes` 에 `*.bat text eol=crlf`
- `RunUAT BuildPlugin` 이 만든 기본 `Config/FilterPlugin.ini` 커밋

**테스트**
- `RunUAT BuildPlugin -TargetPlatforms=Win64`: **수정 없이 성공**, 오류·경고 0 (MSVC 14.44, 적응형 빌드로 파일별 컴파일)
- `Build.bat PluginEditor Win64 Development -DisableAdaptiveUnity`: 성공 (유니티 빌드 `Module.TypePaste.cpp`)
- `Tests/run_tests.bat` (MSVC /W4): 골든 5종 바이트 일치, 퍼징 5,000회 + 별도 20,000회 통과.
  골든 파일을 일부러 바꿔 FAIL 감지·종료 코드 1 확인
- 에디터 실행·수동 검증은 하지 않음

**다음**: TASKS.md P0 — 에디터에서 Tools 메뉴/블루프린트 생성/C++ 생성/설정 유지 수동 검증

## 2026-11-11 — Claude Code (Opus 5.5) — 영어만 규칙, UHT ToolTip 오류 수정, 빌드 후 다시 열기

**요청**: 한글 때문에 빌드가 안 됨 → 개발에서는 한글을 안 쓰니 무조건 영어로. 솔루션이 없으면 생성·빌드가 너무 오래 걸리니
C++ 생성 후 에디터 닫고 배치 파일로 빌드하고 바로 프로젝트로 들어가는 워크플로.

**원인 조사**: 사용자가 생성한 `Source/Plugin/Types/WeaponStatsTypes.h` 로 재현 →
`UHT Error: Metadata key 'ToolTip' first seen with value '마법 무기' then '마나를 소모'`.
한글 자체가 아니라 `/** 주석 */` 과 `UMETA(ToolTip=...)` 가 같은 항목에 같이 나간 생성기 버그 (UHT 는 주석을 ToolTip 으로 씀).

**한 일**
- `TypePasteUtils::EnforceEnglishOnly` / `RemoveNonAsciiSpecifiers` / `IsAsciiText` 추가, 파서 끝에서 호출 (D16)
- C++ 생성기: ToolTip 지정자가 있으면 주석 생략 (D17), 머리 주석 영어, 결과에 ASCII 외 문자가 있으면 오류
- 내보내기: 생성기 오류가 있으면 파일을 쓰지 않음, 솔루션이 없으면 IDE 열기 생략, 루트에 Build.bat 복사 (D19)
- AI 요청 프롬프트를 영어로, 영어만 쓰도록 요구. 도움말에 규칙·빌드 안내 추가
- `Scripts/Build.bat` + `TypePasteBuildLauncher` + 패널 [빌드 후 다시 열기] 버튼, C++ 생성 직후 확인 창 (D18).
  Build.cs 에 `MainFrame`, `Config/FilterPlugin.ini` 에 `/Scripts/...`
- 테스트 입력 `06_english_only.txt` (meta 안 한글, Category, 문자열 기본값, 원문 구문, ToolTip+주석) 추가, 골든 갱신
  (01~05 는 한글 주석/DisplayName 제거와 머리 주석만 바뀜 — diff 확인함)

**테스트**
- `Tests/run_tests.bat`: 골든 6종 일치, 퍼징 20,000회 통과
- 사용자 헤더를 새 생성기 출력(영어)으로 바꿔 `PluginEditor` 빌드 → UHT 통과, 빌드 성공. 강제 유니티 빌드도 성공
- 루트 `Build.bat -noopen`: 엔진 자동 탐색(런처 설치 목록 `LauncherInstalled.dat` — 이 PC 에는 HKLM 레지스트리 키가 없음),
  빌드 성공 약 30초. `-waitpid` 로 더미 프로세스 종료 대기 후 빌드 확인
- **확인 못 함**: 에디터 안에서 버튼/확인 창 → 에디터 종료 → 다시 열기 전체 흐름, Build.bat 의 에디터 실행(`-noopen` 없이)

**다음**: TASKS.md P0 수동 검증 (특히 빌드 후 다시 열기)

## 2026-11-11 — Claude Code (Opus 5.5) — 플러그인 UI·주석 영어화

**요청**: 플러그인 자체의 한글 UI 문구와 주석도 영어로 (D20).

**한 일**
- 소스 22개 파일의 한글 약 475줄을 줄 단위로 영어로 바꿈 (주석, LOCTEXT, 메시지, 프롬프트 도움말, Build.cs, 테스트 스크립트, .gitattributes/.gitignore)
  - 주요 UI 이름: Copy AI Prompt / Paste from Clipboard / Clear / Generate / Blueprint assets / C++ headers /
    Create Blueprint Assets / Create C++ Headers / Build and Reopen, Tools 메뉴는 "TypePaste"
  - 미리보기의 `✗ → — ■` 기호도 ASCII(`[x] -> - *`)로
- 테스트 데이터의 한글(test_main, fuzz)은 `\uXXXX` 이스케이프로 바꿔 소스는 BOM 외 ASCII 만
- `Scripts/Build.bat`: 실행 중 에디터 확인을 "이 프로젝트를 연 에디터" 로 좁힘 (다른 프로젝트/엔진 버전 에디터는 무시).
  이 PC 에서 UE 5.8 의 다른 프로젝트가 열려 있어 경고가 잘못 뜨던 것을 발견해 수정
- README/TASKS/CLAUDE.md 의 UI 이름을 영어 이름으로, CLAUDE.md 코딩 규칙을 "소스는 영어" 로, DECISIONS D13 갱신·D20 추가

**테스트**
- `Tests/run_tests.bat`: 골든 6종 갱신 — 메시지 줄만 1:1 로 바뀌었고 지워진 줄은 전부 한글 줄임을 확인. 퍼징 20,000회 통과
- `PluginEditor` 강제 유니티 빌드 성공, 루트 `Build.bat -noopen`(적응형, 바뀐 8개 파일 개별 컴파일) 성공
- 가짜 `UnrealEditor.exe`(명령줄에 Plugin.uproject) 로 실행 중 감지 확인, 다른 프로젝트 에디터는 무시하는 것 확인
- **확인 못 함**: 에디터 안에서 바뀐 UI 표시, 빌드 후 다시 열기 전체 흐름

**다음**: TASKS.md P0 수동 검증
