@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set TESTOUT=%CD%\build\windows\tests
if not exist "%TESTOUT%" mkdir "%TESTOUT%"
set OPGLUA_HOST_BIN=D:\Program Files (x86)\Steam\steamapps\common\GarrysMod\bin\win64
set PATH=%OPGLUA_HOST_BIN%;%PATH%
cl /nologo /EHsc /std:c++17 /LD /O2 /I vendor\gmodcommon\include tests\gmod_abi\native_contract.cpp /Fo:"%TESTOUT%\native_contract.obj" /Fe:"%TESTOUT%\native_contract.dll" /link /IMPLIB:"%TESTOUT%\native_contract.lib"
if not "%errorlevel%"=="0" goto :failed
cl /nologo /EHsc /O2 /I vendor\gmodcommon\include tests\gmod_abi\filesystem_contract.cpp /Fo:"%TESTOUT%\filesystem_contract.obj" /Fe:"%TESTOUT%\filesystem_contract.exe"
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\filesystem_contract.exe" "%OPGLUA_HOST_BIN%\lua_shared,.dll" reference
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\filesystem_contract.exe" "%CD%\src\lua51.dll" replacement
if not "%errorlevel%"=="0" goto :failed
cl /nologo /EHsc /O2 /DCOMPILER_MSVC /DCOMPILER_MSVC64 /DPLATFORM_64BITS /DPLATFORM_WINDOWS_PC /D_WIN32 /I vendor\source-sdk\public /I vendor\source-sdk\public\tier1 /I vendor\source-sdk\public\tier0 tests\gmod_abi\console_contract.cpp /Fo:"%TESTOUT%\console_contract.obj" /Fe:"%TESTOUT%\console_contract.exe"
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\console_contract.exe" "%CD%\src\lua51.dll"
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\console_contract.exe" "%CD%\src\lua51.dll" realms
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\console_contract.exe" "%OPGLUA_HOST_BIN%\lua_shared,.dll" commands-only
if not "%errorlevel%"=="0" goto :failed
cl /nologo /O2 tests\gmod_abi\opglua_diff.c /Fo:"%TESTOUT%\opglua_diff.obj" /Fe:"%TESTOUT%\opglua_diff.exe"
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\opglua_diff.exe" "%CD%\src\lua51.dll" skip-shared-init "%TESTOUT%\native_contract.dll"
if not "%errorlevel%"=="0" goto :failed
"%TESTOUT%\opglua_diff.exe" "%OPGLUA_HOST_BIN%\lua_shared,.dll" skip-shared-init "%TESTOUT%\native_contract.dll"
if not "%errorlevel%"=="0" goto :failed
powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_logging.ps1
if not "%errorlevel%"=="0" goto :failed
popd
exit /b 0
:failed
popd
exit /b 1
