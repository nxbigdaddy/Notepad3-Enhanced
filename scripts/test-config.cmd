@echo off
setlocal
cd /d "%~dp0.."
if not exist artifacts mkdir artifacts
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd artifacts
cl /nologo /utf-8 /EHsc /W4 /WX /std:c++17 /Fe:config_validate_test.exe ..\tests\config_validate_test.cpp ..\Notepad3\src\TextTools\ConfigValidate.cpp ..\Notepad3\src\TextTools\JsonFormat.cpp
if errorlevel 1 exit /b 1
config_validate_test.exe
set "test_result=%errorlevel%"
popd
exit /b %test_result%
