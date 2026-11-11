// TypePaste - C++ 헤더 생성 후 "에디터 닫기 → 빌드 → 프로젝트 다시 열기" 워크플로 (Scripts/Build.bat 실행)

#pragma once

#include "TypePasteTypes.h"

class FTypePasteBuildLauncher
{
public:
	/** 플러그인에 들어 있는 빌드 스크립트 (<플러그인>/Scripts/Build.bat). 없으면 빈 문자열 */
	static FString GetPluginScriptPath();

	/** 프로젝트 루트의 빌드 스크립트 경로 (<프로젝트>/Build.bat) */
	static FString GetProjectScriptPath();

	/**
	 * 프로젝트 루트에 Build.bat 이 없으면 플러그인의 스크립트를 복사한다 (에디터 밖에서 직접 실행하는 용도).
	 * 같은 이름의 다른 파일이 있으면 덮어쓰지 않는다.
	 */
	static void EnsureProjectScript(TArray<FTPMessage>& OutMessages);

	/**
	 * 빌드 스크립트를 새 콘솔 창에서 실행한 뒤(이 에디터 프로세스가 끝나기를 기다렸다가 빌드하고 프로젝트를 다시 연다)
	 * 에디터 종료를 요청한다. 저장하지 않은 에셋이 있으면 에디터가 저장 여부를 묻는다.
	 * @return 스크립트를 실행했으면 true
	 */
	static bool LaunchBuildAndReopen(TArray<FTPMessage>& OutMessages);
};
