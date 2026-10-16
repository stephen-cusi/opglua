@echo off
rem Build and run the Garry's Mod binary-module ABI end-to-end test.
rem
rem   testmod.dll  a minimal module built against the vendored GarrysMod/Lua
rem                headers; exports gmod13_open / gmod13_close.
rem   host.exe     drives CreateInterface -> CreateLuaInterface ->
rem                GMOD_LoadBinaryModule -> read the module's globals.
rem
rem Both files are C++ (the ABI headers use namespaces and classes), so they
rem are compiled with /TP.  The module links nothing; the host links lua51.lib.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

set HERE=%~dp0
set BIN=%~dp0..\..\src

echo === building testmod.dll ===
cl /nologo /TP /LD /O2 /W3 /I "%HERE%include" "%HERE%mod.c" ^
   /Fe:"%HERE%testmod.dll" /Fo:"%HERE%mod.obj"
if errorlevel 1 goto :BAD

echo === building host.exe ===
cl /nologo /TP /O2 /W3 /I "%HERE%include" "%HERE%host.c" ^
   /Fe:"%HERE%host.exe" /Fo:"%HERE%host.obj" /link "%BIN%\lua51.lib"
if errorlevel 1 goto :BAD

copy /y "%BIN%\lua51.dll" "%HERE%lua51.dll" >nul

rem Lay out a game dir so Require can resolve the module by name:
rem   <gamedir>/lua/bin/gmsv_opglua_reqmod_win64.dll
if not exist "%HERE%gamedir\lua\bin" mkdir "%HERE%gamedir\lua\bin"
copy /y "%HERE%testmod.dll" "%HERE%gamedir\lua\bin\gmsv_opglua_reqmod_win64.dll" >nul
set OPGLUA_GAMEDIR=%HERE%gamedir

echo === running host.exe testmod.dll ===
"%HERE%host.exe" testmod.dll
set RC=%errorlevel%
echo host exit code: %RC%
if "%RC%"=="0" (echo === ABI test PASSED ===) else (echo === ABI test FAILED ===)
goto :END

:BAD
echo BUILD FAILED
:END
endlocal
