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
| D8 | BP 열거형 표시 이름 = `UMETA(DisplayName)`, 없으면 항목 이름. `UMETA(Hidden)` 항목은 제외. 표시 이름이 거부되면 뒤에 `_` 붙여 재시도 | BP 열거형은 내부 이름(NewEnumeratorN)을 못 바꾸고 표시 이름이 사실상 이름 |
| D9 | `float`/`double` → `PC_Real` + `PC_Double` | UE5 블루프린트 Float 는 double 정밀도 |
| D10 | BP 에서 지원 안 하는 정수(int8/int16/uint16 → Integer, uint32/uint64 → Integer64), 고정 배열 → Array, 비트필드 1비트 → Boolean 으로 **변환 + 안내** | 실패보다 가까운 타입으로 만들고 알려 주는 편이 실용적 |
| D11 | C++ 생성 시 정규화: unscoped enum → enum class(uint8), 원시 UObject 포인터 → `TObjectPtr`, `TEnumAsByte<enum class>` 해제, 누락 UPROPERTY·기본값 추가. 해석 못 한 구문(함수 등)은 원문 보존 | UE5 권장 방식에 맞춰 바로 빌드되게 하면서 사용자 코드는 잃지 않기 |
| D12 | C++ 내보내기는 **전부 아니면 전무**: 덮어쓰기 꺼진 상태에서 하나라도 기존 파일이 있으면 아무것도 쓰지 않음 | 일부 파일만 바뀌어 빌드가 꼬이는 상황 방지 |
| D13 | 생성 파일은 UTF-8 **BOM**, 줄바꿈 `LINE_TERMINATOR`. 플러그인 소스도 BOM | 한국어 Windows(MSVC)에서 한글 주석/리터럴 깨짐 방지 |
| D14 | 외부 타입 include 는 리플렉션 메타데이터 `ModuleRelativePath` 로 찾고, 자주 쓰는 타입은 고정 표로 보완. 못 찾으면 안내만 | 엔진 헤더 경로를 하드코딩하지 않기 위해 |
| D15 | 패널 옵션은 `EditorPerProjectIni` 의 `[TypePaste]` 섹션에 프로젝트별 저장 | 프로젝트마다 경로·모듈이 다름 |
