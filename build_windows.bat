@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
python tools\prepare_windows.py
if errorlevel 1 goto :failed
cd src
call msvcbuild.bat gc64
set RESULT=%errorlevel%
popd
exit /b %RESULT%
:failed
popd
exit /b 1
