@echo off
rem OPGLua build wrapper: sets up the MSVC x64 environment, then runs the
rem stock LuaJIT MSVC build script.  Optional argument: "gc64".
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0src"
call msvcbuild.bat %1
endlocal
