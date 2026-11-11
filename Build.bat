@echo off
rem ===========================================================================
rem  TypePaste build helper (TYPEPASTE_BUILD_SCRIPT)
rem
rem  Builds the project's Editor target with UnrealBuildTool (no .sln needed)
rem  and opens the project in the editor when the build succeeds.
rem
rem  Usage (from the project root, or from anywhere with -project):
rem    Build.bat                        build, then open the project
rem    Build.bat -noopen                build only
rem    Build.bat -config DebugGame      build configuration (default Development)
rem    Build.bat -project <x.uproject>  project file (default: the .uproject next to this script)
rem    Build.bat -engine <EngineRoot>   engine root, the folder that contains "Engine"
rem                                     (default: found from EngineAssociation in the .uproject)
rem    Build.bat -waitpid <pid>         wait until that process (the editor) has exited
rem
rem  The TypePaste panel runs this with -waitpid after generating C++ headers,
rem  then closes the editor, so the build starts as soon as the editor is gone.
rem ===========================================================================
setlocal EnableExtensions EnableDelayedExpansion

rem Full paths: Git/MSYS tools on PATH (find, ...) would shadow the Windows ones
set "SYS32=%SystemRoot%\System32"
title TypePaste Build

set "UPROJECT="
set "ENGINE_ROOT="
set "CONFIG=Development"
set "WAITPID="
set "OPEN=1"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-project" ( set "UPROJECT=%~f2" & shift & shift & goto parse_args )
if /i "%~1"=="-engine"  ( set "ENGINE_ROOT=%~f2" & shift & shift & goto parse_args )
if /i "%~1"=="-config"  ( set "CONFIG=%~2" & shift & shift & goto parse_args )
if /i "%~1"=="-waitpid" ( set "WAITPID=%~2" & shift & shift & goto parse_args )
if /i "%~1"=="-noopen"  ( set "OPEN=0" & shift & goto parse_args )
echo [error] Unknown argument: %~1
goto fail
:args_done

rem --- Project ----------------------------------------------------------------
if not defined UPROJECT (
	for %%F in ("%~dp0*.uproject") do if not defined UPROJECT set "UPROJECT=%%~fF"
)
if not defined UPROJECT (
	echo [error] No .uproject next to this script. Copy it to the project root or pass -project.
	goto fail
)
if not exist "%UPROJECT%" (
	echo [error] Project file not found: !UPROJECT!
	goto fail
)
for %%F in ("%UPROJECT%") do (
	set "PROJECT_DIR=%%~dpF"
	set "PROJECT_NAME=%%~nF"
)

rem --- Editor target: <Name>Editor.Target.cs in Source, else <ProjectName>Editor --
set "TARGET="
for %%F in ("%PROJECT_DIR%Source\*Editor.Target.cs") do if not defined TARGET (
	set "TARGET=%%~nF"
	set "TARGET=!TARGET:.Target=!"
)
if not defined TARGET set "TARGET=%PROJECT_NAME%Editor"

rem --- Engine -------------------------------------------------------------------
if not defined ENGINE_ROOT call :find_engine
if not defined ENGINE_ROOT (
	echo [error] Could not find the engine. Pass -engine "C:\Program Files\Epic Games\UE_5.7".
	goto fail
)
if "%ENGINE_ROOT:~-1%"=="\" set "ENGINE_ROOT=%ENGINE_ROOT:~0,-1%"
set "UBT_BAT=%ENGINE_ROOT%\Engine\Build\BatchFiles\Build.bat"
set "EDITOR_EXE=%ENGINE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%UBT_BAT%" (
	echo [error] Not an engine folder: !ENGINE_ROOT!
	goto fail
)

echo Project : %UPROJECT%
echo Target  : %TARGET% Win64 %CONFIG%
echo Engine  : %ENGINE_ROOT%
echo.

rem --- Wait for the editor ----------------------------------------------------
if not defined WAITPID goto check_running
echo Waiting for the editor (PID %WAITPID%) to close...
echo If you cancelled closing the editor, close it yourself or close this window.
:wait_loop
"%SYS32%\tasklist.exe" /FI "PID eq %WAITPID%" /NH 2>nul | "%SYS32%\find.exe" " %WAITPID% " >nul
if errorlevel 1 goto editor_closed
"%SYS32%\PING.EXE" -n 2 127.0.0.1 >nul
goto wait_loop
:editor_closed
echo Editor closed.
echo.
goto build

:check_running
"%SYS32%\tasklist.exe" /FI "IMAGENAME eq UnrealEditor.exe" /NH 2>nul | "%SYS32%\find.exe" /i "UnrealEditor.exe" >nul
if errorlevel 1 goto build
echo [warning] UnrealEditor.exe is running. Building while the editor has this project open
echo           fails (Live Coding / locked DLLs). Close the editor first.
"%SYS32%\choice.exe" /C YN /M "Build anyway"
if errorlevel 2 goto fail

rem --- Build ----------------------------------------------------------------
:build
call "%UBT_BAT%" %TARGET% Win64 %CONFIG% -Project="%UPROJECT%" -WaitMutex
if errorlevel 1 (
	echo.
	echo [error] Build failed. Fix the errors above, then run this script again.
	goto fail
)
echo.
echo Build succeeded.

rem --- Open the project -----------------------------------------------------
if not "%OPEN%"=="1" goto done
echo Opening %PROJECT_NAME%...
if exist "%EDITOR_EXE%" (
	start "" "!EDITOR_EXE!" "!UPROJECT!"
) else (
	start "" "!UPROJECT!"
)
:done
rem Launched from the editor (cmd start keeps the window open): close it on success
if defined WAITPID exit 0
endlocal
exit /b 0

rem ===========================================================================
:find_engine
rem EngineAssociation is either a version ("5.7") for launcher installs or a
rem GUID / path for source builds registered under HKCU\...\Builds.
set "ASSOC="
for /f "usebackq delims=" %%A in (`powershell -NoProfile -Command "(Get-Content -Raw -LiteralPath '%UPROJECT%' | ConvertFrom-Json).EngineAssociation"`) do set "ASSOC=%%A"
if not defined ASSOC exit /b 0
if exist "%ASSOC%\Engine\Build\BatchFiles\Build.bat" ( set "ENGINE_ROOT=%ASSOC%" & exit /b 0 )
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\%ASSOC%" /v InstalledDirectory 2^>nul ^| findstr /i "InstalledDirectory"') do set "ENGINE_ROOT=%%B"
if defined ENGINE_ROOT exit /b 0
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Epic Games\Unreal Engine\Builds" /v "%ASSOC%" 2^>nul ^| findstr /i /c:"%ASSOC%"') do set "ENGINE_ROOT=%%B"
if defined ENGINE_ROOT exit /b 0
rem Epic Games Launcher install list (works for custom install folders)
set "LAUNCHER_DAT=%ProgramData%\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
if exist "%LAUNCHER_DAT%" for /f "usebackq delims=" %%A in (`powershell -NoProfile -Command "((Get-Content -Raw -LiteralPath $env:LAUNCHER_DAT | ConvertFrom-Json).InstallationList | Where-Object { $_.AppName -eq ('UE_' + $env:ASSOC) } | Select-Object -First 1).InstallLocation"`) do set "ENGINE_ROOT=%%A"
if defined ENGINE_ROOT exit /b 0
if exist "%ProgramFiles%\Epic Games\UE_%ASSOC%\Engine" set "ENGINE_ROOT=%ProgramFiles%\Epic Games\UE_%ASSOC%"
exit /b 0

rem ===========================================================================
:fail
echo.
pause
endlocal
exit /b 1
