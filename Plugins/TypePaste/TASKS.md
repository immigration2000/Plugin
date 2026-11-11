# TASKS

## P0 — 실제 엔진에서 동작 확인

- [x] **첫 빌드** (`RunUAT BuildPlugin`) 후 컴파일/링크 오류 수정 — 2026-11-11 수정 없이 통과 (오류·경고 0).
  `PluginEditor` 타깃도 `-DisableAdaptiveUnity` 로 유니티 빌드 통과. 아래 체크리스트는 이제 **런타임 동작** 확인용
- [ ] **블루프린트 생성 수동 검증** — `Samples/Example_AI_Answer.txt` 붙여넣기, 이름 규칙 `E_ / S_`
  - [ ] Tools 메뉴에 "TypePaste (타입 붙여넣기)" 가 보이고 탭이 열린다
  - [ ] 열거형 표시 이름이 한글(UMETA DisplayName)로 들어가고, `MAX UMETA(Hidden)` 은 빠진다
  - [ ] 항목 툴팁(주석)이 저장 후 에디터 재시작에도 남는다 (`UEnum::SetMetaData` 로 넣음 — 안 남으면 다른 방법 필요)
  - [ ] DisplayName 없는 `None` 항목 → `None_` 대체 동작 확인 (실제로 "None" 이 거부되는지도 확인)
  - [ ] 구조체: 기본 멤버 `MemberVar_0` 이 지워지고, 멤버 이름·타입·툴팁이 맞다
  - [ ] 기본값: `Damage = 10.0`, `WeaponType = 근접`(열거형), `bIsTwoHanded = false`
  - [ ] `TSoftObjectPtr<UStaticMesh>`, `TSubclassOf<AActor>`, `TArray<FGameplayTag>`, `TMap<FName, float>` 핀 타입
  - [ ] 같은 이름 에셋이 있을 때 재사용 경고만 나오고 덮어쓰지 않는다
  - [ ] "생성 후 바로 저장" 으로 .uasset 이 디스크에 저장된다, 콘텐츠 브라우저가 생성 위치로 이동한다
- [ ] **C++ 생성 수동 검증** — 같은 샘플로 헤더 생성 → 에디터 종료 → 빌드 → UHT 통과, 에디터에서 타입 노출 확인
  - [ ] include 자동 추가(`GameplayTagContainer.h`, `Engine/DataTable.h`), Build.cs 모듈 안내 메시지
  - [ ] 덮어쓰기 꺼진 상태에서 기존 파일 있으면 아무것도 쓰지 않는다
- [ ] 옵션(경로, 이름 규칙, 모듈, 모드)이 에디터 재시작 후에도 유지된다

### 첫 빌드 체크리스트 (문서만 보고 쓴 API)

컴파일·링크는 모두 통과했으므로 시그니처·export 문제는 없다. 남은 위험은 **실행 시 동작**
(메타데이터가 저장 후 남는지, 팩토리를 찾는지, 브러시/아이콘 이름이 맞는지 등)이며, 위 수동 검증에서 이상하면 여기부터 의심한다.

| 위치 | 의심 지점 |
|---|---|
| `TypePasteTypes.h` | `FTPTypeRef` 안의 `TArray<FTPTypeRef> Args` (자기 참조 TArray). 안 되면 `TArray<TSharedPtr<FTPTypeRef>>` 등으로 |
| `TypePasteBlueprintBuilder.cpp` | `#include "StructUtils/UserDefinedStruct.h"` (5.5+ 경로), `UserDefinedStructure/UserDefinedStructEditorData.h` |
| `TypePasteBlueprintBuilder.cpp` | `Enum->SetMetaData(Key, Value, Index)`, `Enum->EnumDescription`, `FEdGraphTerminalType::FromPinType` |
| `TypePasteBlueprintBuilder.cpp` | `UEditorLoadingAndSavingUtils::SavePackages` 링크(export) — 안 되면 `FEditorFileUtils::PromptForCheckoutAndSave` |
| `TypePasteBlueprintBuilder.cpp` | 팩토리를 `TObjectIterator<UClass>` 로 찾음 (`GetSupportedClass`, `CanCreateNew`) |
| `TypePasteCppExporter.cpp` | `UField::GetMetaData(TEXT("ModuleRelativePath"))`, `IProjectManager`, `FSourceCodeNavigation::OpenSourceFile`, `LINE_TERMINATOR` |
| `STypePastePanel.cpp` | `.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("RadioButton"))`, `Visibility_Lambda`, `HintText_Lambda`, `SyncBrowserToAssets(TArray<UObject*>)`, `GConfig`/`GEditorPerProjectIni` |
| 전체 | `FindFirstObject<T>(*FString, EFindFirstObjectOptions::NativeFirst)`, 섀도잉 경고(C4456/C4458) |
| 런타임만 | 브러시 이름 `ToolPanel.GroupBorder`, 아이콘 `ClassIcon.UserDefinedStruct` (없으면 표시만 안 됨) |

## P1 — 기능 보강

- [ ] 기존 블루프린트 에셋 **업데이트 모드** (현재는 재사용만): 멤버 추가/타입 변경을 미리보기로 보여 주고 확인 후 적용
- [ ] Bitflags 열거형 → `FEnumEditorUtils::SetEnumeratorBitflagsTypeState`
- [ ] 구조체 타입 기본값(`FVector(1,2,3)`, `FLinearColor::White` 등)을 블루프린트 기본값 형식으로 변환
- [ ] C++ 생성 대상에 프로젝트 플러그인 모듈도 포함
- [ ] 미리보기 갱신 디바운스 (긴 입력에서 타이핑마다 재파싱)

## P2 — 아이디어

- [ ] 콘텐츠 브라우저 폴더 우클릭 → "여기에 TypePaste" (경로 자동 지정)
- [ ] 생성한 구조체로 데이터 테이블 에셋도 같이 만들기 (FTableRowBase 상속일 때)
- [ ] UE 자동화 테스트(Session Frontend)로 파서 테스트 이식
- [ ] 인터페이스(`TScriptInterface<>`) 타입 지원
