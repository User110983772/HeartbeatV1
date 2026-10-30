@echo off
rem Builds Heartbeat (Release x64) and its Windows installer: installer\out\Heartbeat-<version>-Windows-Setup.exe
rem Needs Visual Studio 2022 (Desktop C++) and Inno Setup 6 (https://jrsoftware.org/isdl.php, or
rem "winget install JRSoftware.InnoSetup"). Factory content is read from ..\heartbeat-factory and ..\Presets next
rem to the project folder, and samples from ..\Melodies; set HEARTBEAT_CONTENT / HEARTBEAT_PRESETS /
rem HEARTBEAT_SAMPLES to use other folders.
rem   build_installer.bat            build, then package
rem   build_installer.bat nobuild    package what is already built
setlocal enableextensions
set "VERSION=1.0.0"
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "REPO=%%~fI"
for %%I in ("%REPO%\..") do set "PROJECTS=%%~fI"
if not defined HEARTBEAT_CONTENT set "HEARTBEAT_CONTENT=%PROJECTS%\heartbeat-factory"
if not defined HEARTBEAT_PRESETS set "HEARTBEAT_PRESETS=%PROJECTS%\Presets"
if not defined HEARTBEAT_SAMPLES set "HEARTBEAT_SAMPLES=%PROJECTS%\Melodies"
set "PLUGIN_BUILD=%REPO%\plugin\builds\vs17\x64\Release"
set "APP_EXE=%REPO%\standalone\builds\vs17\x64\Release\App\Heartbeat.exe"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio 2022 was not found. & goto :fail)
for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSDIR=%%P"
set "MSBUILD=%VSDIR%\MSBuild\Current\Bin\MSBuild.exe"

if /i "%~1"=="nobuild" goto :package
echo === Building the VST3 plugin
"%MSBUILD%" "%REPO%\plugin\builds\vs17\Heartbeat_VST3.vcxproj" /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="%REPO%\plugin\builds\vs17\\" /p:PostBuildEventUseInBuild=false
if errorlevel 1 goto :fail
echo === Building the standalone application
"%MSBUILD%" "%REPO%\standalone\builds\vs17\Heartbeat_App.vcxproj" /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="%REPO%\standalone\builds\vs17\\" /p:PlatformToolset=v143
if errorlevel 1 goto :fail

:package
if not exist "%PLUGIN_BUILD%\VST3\Heartbeat.vst3" (echo Missing "%PLUGIN_BUILD%\VST3\Heartbeat.vst3" & goto :fail)
if not exist "%APP_EXE%" (echo Missing "%APP_EXE%" & goto :fail)
if not exist "%HEARTBEAT_CONTENT%" (echo Missing factory content "%HEARTBEAT_CONTENT%" & goto :fail)
if not exist "%HEARTBEAT_PRESETS%" (echo Missing factory presets "%HEARTBEAT_PRESETS%" & goto :fail)
if not exist "%HEARTBEAT_SAMPLES%" (echo Missing factory samples "%HEARTBEAT_SAMPLES%" & goto :fail)

rem The Visual C++ runtime installer: Visual Studio's own copy, or Microsoft's download.
set "VCREDIST=%HERE%vc_redist.x64.exe"
if not exist "%VCREDIST%" (
  for /f "delims=" %%R in ('dir /b /s "%VSDIR%\VC\Redist\MSVC\vc_redist.x64.exe" 2^>nul') do set "FOUND_REDIST=%%R"
)
if not exist "%VCREDIST%" if defined FOUND_REDIST copy /y "%FOUND_REDIST%" "%VCREDIST%" >nul
if not exist "%VCREDIST%" powershell -NoProfile -Command "Invoke-WebRequest https://aka.ms/vs/17/release/vc_redist.x64.exe -OutFile '%VCREDIST%'"
if not exist "%VCREDIST%" (echo Could not find or download vc_redist.x64.exe & goto :fail)

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (echo Inno Setup 6 was not found: winget install JRSoftware.InnoSetup & goto :fail)
echo === Packaging
"%ISCC%" /Q /DAppVersion=%VERSION% "/DPluginBuild=%PLUGIN_BUILD%" "/DAppExe=%APP_EXE%" "/DContentDir=%HEARTBEAT_CONTENT%" "/DPresetsDir=%HEARTBEAT_PRESETS%" "/DSamplesDir=%HEARTBEAT_SAMPLES%" "/DVcRedist=%VCREDIST%" "%HERE%Heartbeat.iss"
if errorlevel 1 goto :fail
echo.
echo Done: %REPO%\installer\out\Heartbeat-%VERSION%-Windows-Setup.exe
if not defined CI pause
exit /b 0

:fail
echo.
echo Installer build failed.
if not defined CI pause
exit /b 1
