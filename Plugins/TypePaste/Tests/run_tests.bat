@echo off
rem TypePaste 순수 로직(파서 / C++ 생성기) 테스트 - Windows MSVC 용 (run_tests.sh 와 같은 동작)
rem
rem   run_tests.bat                 빌드 + 골든 비교 + 퍼징
rem   set UPDATE=1 & run_tests.bat  현재 출력으로 expected\ 갱신 (의도한 출력 변경일 때만)
rem   set FUZZ_ITERS=20000          퍼징 반복 횟수 (기본 5000)
rem
rem Developer Command Prompt 가 아니어도 vswhere 로 Visual Studio 를 찾아 vcvars64 를 불러온다.
chcp 65001 >nul
setlocal EnableDelayedExpansion
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
	set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
	if not exist "!VSWHERE!" ( echo [error] vswhere.exe 를 찾을 수 없습니다. Developer Command Prompt 에서 실행하세요. & exit /b 1 )
	for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
	if not defined VSDIR ( echo [error] C++ 도구가 설치된 Visual Studio 가 없습니다. & exit /b 1 )
	call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)

set "SRC=..\Source\TypePaste\Private"
set "PURE=%SRC%\TypePasteTypes.cpp %SRC%\TypePasteParser.cpp %SRC%\TypePasteCppWriter.cpp"
set "FLAGS=/nologo /std:c++20 /EHsc /utf-8 /W4 /wd4100 /D_CRT_SECURE_NO_WARNINGS /I mock /I %SRC%"

if not exist build mkdir build
echo [build] cl
cl %FLAGS% test_main.cpp %PURE% /Fobuild\ /Fe:build\tptest.exe >build\cl_tptest.log || ( type build\cl_tptest.log & exit /b 1 )
cl %FLAGS% fuzz.cpp %PURE% /Fobuild\ /Fe:build\fuzz.exe >build\cl_fuzz.log || ( type build\cl_fuzz.log & exit /b 1 )

set FAIL=0
set INPUTS=
for %%f in (inputs\*.txt) do (
	set "NAME=%%~nf"
	rem 골든 출력에 입력 경로가 그대로 찍히므로 run_tests.sh 와 같은 'inputs/이름.txt' 형식으로 넘긴다
	build\tptest.exe inputs/%%~nxf > build\!NAME!.out
	set "INPUTS=!INPUTS! inputs/%%~nxf"
	if "%UPDATE%"=="1" (
		copy /y build\!NAME!.out expected\!NAME!.out >nul
		echo [update] expected\!NAME!.out
	) else (
		fc /b expected\!NAME!.out build\!NAME!.out >nul
		if errorlevel 1 (
			echo [FAIL] !NAME!  ^(git diff --no-index expected/!NAME!.out build/!NAME!.out 로 차이 확인^)
			set FAIL=1
		) else (
			echo [ok] !NAME!
		)
	)
)

if not defined FUZZ_ITERS set FUZZ_ITERS=5000
echo [fuzz] %FUZZ_ITERS% iterations
build\fuzz.exe%INPUTS% || exit /b 1

if "%FAIL%"=="1" (
	echo 골든 출력과 다릅니다. 의도한 변경이면 UPDATE=1 로 갱신하세요.
	exit /b 1
)
echo 모든 테스트 통과
exit /b 0
