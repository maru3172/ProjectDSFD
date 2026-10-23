@echo off
setlocal
set "ProjectBotRoot=%~dp0..\.."
if not exist "%ProjectBotRoot%\Saved\AdaptiveBotChecks" mkdir "%ProjectBotRoot%\Saved\AdaptiveBotChecks"
pushd "%ProjectBotRoot%\Saved\AdaptiveBotChecks" || exit /b 1
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /DPROJECT_BOT_STANDALONE=1 /I"%ProjectBotRoot%\Source\ProjectProject01\Public" "%ProjectBotRoot%\Source\ProjectProject01\Private\ProjectAdaptiveBotPolicyTests.cpp" /Fe"AdaptiveBotChecks.exe" /Fo"AdaptiveBotChecks.obj"
if errorlevel 1 exit /b 1
AdaptiveBotChecks.exe
set "ProjectBotResult=%errorlevel%"
popd
exit /b %ProjectBotResult%
