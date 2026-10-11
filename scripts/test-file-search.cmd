@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /W4 /WX /std:c++17 /Fe:artifacts\file_search_test.exe /Fo:artifacts\ tests\file_search_test.cpp Notepad3\src\Workspace\FileSearchCore.cpp /link Notepad3\Bin\Release_x64_v143\obj\Scintilla.lib
if errorlevel 1 exit /b 1
artifacts\file_search_test.exe
exit /b %errorlevel%
