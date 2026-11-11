@echo off
rem TypePaste pure-logic tests (parser / C++ generator) - Windows MSVC version (same behavior as run_tests.sh)
rem
rem   run_tests.bat                 build + golden comparison + fuzzing
rem   set UPDATE=1 & run_tests.bat  overwrite expected\ with the current output (only for intended output changes)
rem   set FUZZ_ITERS=20000          fuzzing iterations (default 5000)
rem
rem Works outside a Developer Command Prompt too: finds Visual Studio with vswhere and loads vcvars64.
chcp 65001 >nul
setlocal EnableDelayedExpansion
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
	set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
	if not exist "!VSWHERE!" ( echo [error] vswhere.exe not found. Run this from a Developer Command Prompt. & exit /b 1 )
	for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
	if not defined VSDIR ( echo [error] No Visual Studio with the C++ tools is installed. & exit /b 1 )
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
	rem The golden output prints the input path, so pass it as 'inputs/<name>.txt' like run_tests.sh does
	build\tptest.exe inputs/%%~nxf > build\!NAME!.out
	set "INPUTS=!INPUTS! inputs/%%~nxf"
	if "%UPDATE%"=="1" (
		copy /y build\!NAME!.out expected\!NAME!.out >nul
		echo [update] expected\!NAME!.out
	) else (
		fc /b expected\!NAME!.out build\!NAME!.out >nul
		if errorlevel 1 (
			echo [FAIL] !NAME!  ^(see the difference with: git diff --no-index expected/!NAME!.out build/!NAME!.out^)
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
	echo Output differs from the golden files. If the change is intended, update them with UPDATE=1.
	exit /b 1
)
echo All tests passed
exit /b 0
