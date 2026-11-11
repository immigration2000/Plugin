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
