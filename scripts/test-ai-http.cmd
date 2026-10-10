@echo off
setlocal
cd /d "%~dp0.."
if not exist artifacts mkdir artifacts
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd artifacts
cl /nologo /utf-8 /EHsc /W4 /WX /std:c++17 /Fe:ai_http_test.exe ..\tests\ai_http_test.cpp ..\Notepad3\src\AI\AiHttp.cpp ..\Notepad3\src\AI\AiCore.cpp ..\Notepad3\src\TextTools\JsonFormat.cpp
if errorlevel 1 exit /b 1
popd
python scripts\test-ai-http.py
exit /b %errorlevel%
