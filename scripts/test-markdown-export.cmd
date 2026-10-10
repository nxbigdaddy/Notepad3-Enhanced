@echo off
setlocal
cd /d "%~dp0.."
if not exist artifacts mkdir artifacts
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd artifacts
cl /nologo /utf-8 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS /Fe:markdown_export_test.exe ..\tests\markdown_export_test.cpp ..\Notepad3\src\Markdown\MarkdownExport.cpp ..\Notepad3\src\Markdown\MarkdownRender.cpp ..\Notepad3\src\Markdown\vendor\md4c\md4c.c ..\Notepad3\src\Markdown\vendor\md4c\md4c-html.c ..\Notepad3\src\Markdown\vendor\md4c\entity.c
if errorlevel 1 exit /b 1
markdown_export_test.exe
set "test_result=%errorlevel%"
popd
exit /b %test_result%
