# DECISIONS

| # | 결정 | 이유 |
|---|---|---|
| D1 | 입력은 **AI 가 준 C++ 코드 그대로** (JSON 같은 전용 형식 아님). 마크다운·설명 문장이 섞여도 관대하게 파싱 | AI 는 원래 UENUM/USTRUCT 코드로 답함. 사용자 요구가 "복사·붙여넣기로 간단하게" |
| D2 | 생성 대상은 **블루프린트 에셋 / C++ 헤더 둘 다**, 생성할 때 선택 | 사용자 선택 (2026-11-11). 파서는 하나로 공유 |
| D3 | 대상 엔진 **UE 5.7 이상** | 사용자 선택. 5.5+ 경로(`StructUtils/UserDefinedStruct.h`) 사용 |
| D4 | **새 독립 플러그인** (기존 플러그인에 넣지 않음) | 사용자 선택. 나중에 합치기 쉽게 모듈 하나로 구성 |
| D5 | 파서·C++ 생성기는 에디터 API 없는 **순수 로직**으로 분리, 외부 타입 조회는 `ITPExternalTypeInfo` 인터페이스로 주입 | 엔진 없이 mock 으로 테스트하기 위해 |
| D6 | 블루프린트: 같은 경로·이름의 에셋이 있으면 **덮어쓰지 않고 재사용**(참조만 연결) | 이미 다른 곳에서 쓰는 구조체를 바꾸면 블루프린트가 깨질 수 있음 |
| D7 | `UEnumFactory`/`UStructureFactory` 를 직접 링크하지 않고 `TObjectIterator<UClass>` 로 팩토리를 찾음 | 두 클래스의 DLL export 여부가 불확실해 링크 오류 위험 회피 |
| D8 | (D16 이후 DisplayName 은 영어일 때만 남음) BP 열거형 표시 이름 = `UMETA(DisplayName)`, 없으면 항목 이름. `UMETA(Hidden)` 항목은 제외. 표시 이름이 거부되면 뒤에 `_` 붙여 재시도 | BP 열거형은 내부 이름(NewEnumeratorN)을 못 바꾸고 표시 이름이 사실상 이름 |
| D9 | `float`/`double` → `PC_Real` + `PC_Double` | UE5 블루프린트 Float 는 double 정밀도 |
| D10 | BP 에서 지원 안 하는 정수(int8/int16/uint16 → Integer, uint32/uint64 → Integer64), 고정 배열 → Array, 비트필드 1비트 → Boolean 으로 **변환 + 안내** | 실패보다 가까운 타입으로 만들고 알려 주는 편이 실용적 |
| D11 | C++ 생성 시 정규화: unscoped enum → enum class(uint8), 원시 UObject 포인터 → `TObjectPtr`, `TEnumAsByte<enum class>` 해제, 누락 UPROPERTY·기본값 추가. 해석 못 한 구문(함수 등)은 원문 보존 | UE5 권장 방식에 맞춰 바로 빌드되게 하면서 사용자 코드는 잃지 않기 |
| D12 | C++ 내보내기는 **전부 아니면 전무**: 덮어쓰기 꺼진 상태에서 하나라도 기존 파일이 있으면 아무것도 쓰지 않음 | 일부 파일만 바뀌어 빌드가 꼬이는 상황 방지 |
| D13 | 생성 파일은 UTF-8 **BOM**, 줄바꿈 `LINE_TERMINATOR`. 플러그인 소스도 BOM | 원래는 한글 주석/리터럴 깨짐 방지용. D16/D20 이후 내용은 ASCII 라 필수는 아니지만 통일을 위해 유지 |
| D14 | 외부 타입 include 는 리플렉션 메타데이터 `ModuleRelativePath` 로 찾고, 자주 쓰는 타입은 고정 표로 보완. 못 찾으면 안내만 | 엔진 헤더 경로를 하드코딩하지 않기 위해 |
| D15 | 패널 옵션은 `EditorPerProjectIni` 의 `[TypePaste]` 섹션에 프로젝트별 저장 | 프로젝트마다 경로·모듈이 다름 |
| D16 | **생성 결과는 영어(ASCII)만** (2026-11-11 사용자 결정). 파싱 직후 `EnforceEnglishOnly` 가 ASCII 가 아닌 주석·지정자 값(DisplayName/ToolTip/Category …)·기본값·원문 구문을 빼고 경고. 빠진 Category 는 구조체 이름으로 채움. C++ 생성기는 결과에 ASCII 가 아닌 문자가 남으면 오류로 막음. AI 요청 프롬프트도 영어로만 답하게 요구 | 사용자는 개발할 때 한글을 쓰지 않음. 블루프린트·C++ 양쪽에 같은 규칙을 적용해 결과가 일관되게. D8 의 한글 표시 이름은 이 결정으로 대체 |
| D17 | 주석과 `ToolTip` 지정자가 같이 있으면 **주석을 생성하지 않음** | UHT 는 바로 위 주석을 ToolTip 으로 쓰므로 둘이 다르면 `Metadata key 'ToolTip' first seen with value A then B` 오류 (첫 사용자 빌드에서 실제로 발생) |
| D18 | C++ 생성 후 **빌드 후 다시 열기**: `Scripts/Build.bat` 을 새 콘솔에서 실행(에디터 PID 종료 대기 → `Build.bat <Target> Win64 Development` → `UnrealEditor.exe <uproject>`)하고 `IMainFrameModule::RequestCloseEditor` 로 에디터를 닫음. 스크립트는 프로젝트 루트에도 `Build.bat` 으로 복사(없을 때만)해 직접 실행 가능 | 솔루션 파일 생성 + IDE 빌드가 너무 오래 걸린다는 사용자 요청. UBT 를 직접 부르면 .sln 이 필요 없음. 에디터가 켜져 있으면 DLL 잠금/Live Coding 때문에 빌드가 실패하므로 종료를 기다림. 실행 명령은 `cmd /c start "TypePaste Build" cmd.exe /c ""<Build.bat>" -project ".." ..."` — `start` 에 .bat 을 바로 넘기면 `cmd /K` 가 앞뒤 따옴표를 지워 경로가 깨짐 (2026-11-12 사용자 영상에서 실제 발생) |
| D19 | 솔루션 파일이 없으면 "생성 후 IDE 에서 열기" 를 건너뜀. 생성기가 오류를 내면 C++ 파일을 쓰지 않음 | IDE 열기가 프로젝트 파일 생성을 유발해 느림. 빌드가 깨질 파일을 쓰지 않기 (D12 와 같은 취지) |
| D20 | **플러그인 자체(UI 문구·메시지·주석·스크립트)도 영어** (2026-11-11 사용자 결정). 소스는 BOM 외 ASCII 만. 문서(.md)는 한국어 유지 | 사용자는 개발할 때 한글을 쓰지 않음 (D16 과 같은 이유) |
