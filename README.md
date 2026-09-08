# Plugin

Unreal Engine 5.7 기반의 C++ 플러그인/프로젝트 개발용 저장소입니다.

## 브랜치 운용 방식

이 저장소는 다음과 같은 규칙으로 관리됩니다.

- **`master` (main) 브랜치**: 아무 기능도 추가되지 않은 빈 C++ 프로젝트 상태를 유지합니다. 새 작업을 시작하기 위한 베이스 템플릿 역할만 합니다.
- **새 플러그인 작업**: 새로운 플러그인을 만들 때마다 `master`에서 새 브랜치를 파서 작업합니다.
- **작업 종료**: 해당 플러그인 개발이 완성되면 그 브랜치에서 작업을 마무리합니다. (`master`로 다시 병합하지 않고, 브랜치 자체를 결과물로 남겨둡니다.)

즉, `master`는 항상 깨끗한 시작점이고, 실제 플러그인 결과물들은 각각의 브랜치에 개별적으로 존재합니다.

```
master (빈 프로젝트)
 ├─ feature/plugin-a   → 완성 후 종료
 ├─ feature/plugin-b   → 완성 후 종료
 └─ feature/plugin-c   → 작업 중
```

## 프로젝트 정보

- **엔진 버전**: Unreal Engine 5.7
- **모듈**: `Plugin` (Runtime)
- **주요 의존 모듈**: `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`

## 시작하기

1. `master` 브랜치에서 새 브랜치를 생성합니다.
   ```
   git checkout master
   git checkout -b <새-플러그인-이름>
   ```
2. `Plugin.uproject`를 우클릭하여 Visual Studio 프로젝트 파일을 생성하거나, Unreal 에디터로 직접 엽니다.
3. `Source/Plugin` 하위에서 기능을 개발합니다.
4. 작업이 완성되면 해당 브랜치에 커밋/푸시하고 그대로 두어 결과물로 보존합니다.
