@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /W4 /WX /std:c++17 /Fe:artifacts\history_service_test.exe /Fo:artifacts\ tests\history_service_test.cpp Notepad3\src\Workspace\HistoryCore.cpp Notepad3\src\Workspace\HistoryService.cpp
if errorlevel 1 exit /b 1
artifacts\history_service_test.exe
exit /b %errorlevel%
