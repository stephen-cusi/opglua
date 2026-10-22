@rem Script to build LuaJIT with MSVC.
@rem Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
@rem
@rem Either open a "Visual Studio .NET Command Prompt"
@rem (Note that the Express Edition does not contain an x64 compiler)
@rem -or-
@rem Open a "Windows SDK Command Shell" and set the compiler environment:
@rem     setenv /release /x86
@rem   -or-
@rem     setenv /release /x64
@rem
@rem Then cd to this directory and run this script.

@if not defined INCLUDE goto :FAIL

@setlocal
@set LJCOMPILE=cl /nologo /c /O2 /W3 /D_CRT_SECURE_NO_DEPRECATE /D_CRT_STDIO_INLINE=__declspec(dllexport)__inline /DLUAJIT_DISABLE_FFI
@set CXX=cl
@set SRCSDK=%~dp0..\vendor\source-sdk
@set GMODSDK=%~dp0..\vendor\gmodcommon\include
@set HOSTLIB=%~dp0..\build\windows\imports
@set LJLINK=link /nologo /incremental:no /opt:ref /opt:icf
@set LJMT=mt /nologo
@set LJLIB=lib /nologo /nodefaultlib
@set DASMDIR=..\dynasm
@set DASM=%DASMDIR%\dynasm.lua
@set DASC=vm_x86.dasc
@set LJDLLNAME=lua51.dll
@set LJLIBNAME=lua51.lib
@set ALL_LIB=lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c

%LJCOMPILE% host\minilua.c
@if errorlevel 1 goto :BAD
%LJLINK% /out:minilua.exe minilua.obj
@if errorlevel 1 goto :BAD
if exist minilua.exe.manifest^
  %LJMT% -manifest minilua.exe.manifest -outputresource:minilua.exe

@set DASMFLAGS=-D WIN -D JIT -D P64
@set LJARCH=x64
@minilua
@if errorlevel 8 goto :X64
@set DASMFLAGS=-D WIN -D JIT
@set LJARCH=x86
@set LJCOMPILE=%LJCOMPILE% /arch:SSE2
:X64
@if "%1" neq "gc64" goto :NOGC64
@shift
@set DASC=vm_x64.dasc
@set LJCOMPILE=%LJCOMPILE% /DLUAJIT_ENABLE_GC64
:NOGC64
minilua %DASM% -LN %DASMFLAGS% -o host\buildvm_arch.h %DASC%
@if errorlevel 1 goto :BAD

%LJCOMPILE% /I "." /I %DASMDIR% host\buildvm*.c
@if errorlevel 1 goto :BAD
%LJLINK% /out:buildvm.exe buildvm*.obj
@if errorlevel 1 goto :BAD
if exist buildvm.exe.manifest^
  %LJMT% -manifest buildvm.exe.manifest -outputresource:buildvm.exe

buildvm -m peobj -o lj_vm.obj
@if errorlevel 1 goto :BAD
buildvm -m bcdef -o lj_bcdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m ffdef -o lj_ffdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m libdef -o lj_libdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m recdef -o lj_recdef.h %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m vmdef -o jit\vmdef.lua %ALL_LIB%
@if errorlevel 1 goto :BAD
buildvm -m folddef -o lj_folddef.h lj_opt_fold.c
@if errorlevel 1 goto :BAD

@if "%1" neq "debug" goto :NODEBUG
@shift
@set LJCOMPILE=%LJCOMPILE% /Zi
@set LJLINK=%LJLINK% /debug /opt:ref /opt:icf /incremental:no
:NODEBUG
@if "%1"=="amalg" goto :AMALGDLL
@if "%1"=="static" goto :STATIC
%LJCOMPILE% /Zi /MT /DOPGLUA_LOG /DLUA_BUILD_AS_DLL lj_*.c lib_*.c
@if errorlevel 1 goto :BAD
rem Build the console support and host bridges from local sources.
@set CPPCOMPILE=%CXX% /nologo /c /O2 /Gy /Gw /MT /Zi /TP /EHsc /W3 /DNDEBUG /D_WIN32 /DCOMPILER_MSVC /DCOMPILER_MSVC64 /DPLATFORM_64BITS /DPLATFORM_WINDOWS_PC /D_CRT_SECURE_NO_DEPRECATE /DTIER1_STATIC_LIB /D_ALLOW_RUNTIME_LIBRARY_MISMATCH /D_ALLOW_MSC_VER_MISMATCH /D_ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH /I "%SRCSDK%\public" /I "%SRCSDK%\public\tier1" /I "%SRCSDK%\public\tier0" /I "%SRCSDK%" /I "%GMODSDK%"
%CPPCOMPILE% /Fo:lib_convar.obj lib_convar.cpp
@if errorlevel 1 goto :BAD
%CPPCOMPILE% /Fo:convar_impl.obj "%SRCSDK%\tier1\convar.cpp"
@if errorlevel 1 goto :BAD
%CPPCOMPILE% /DLUA_LIB /Fo:lib_luashared.obj lib_luashared.cpp
@if errorlevel 1 goto :BAD
%CPPCOMPILE% /Fo:lib_mem.obj lib_mem.cpp
@if errorlevel 1 goto :BAD
@for %%S in (strtools characterset utlbuffer utlstring strtools_unicode byteswap generichash mempool utlsymbol) do (
  %CPPCOMPILE% /Fo:support_%%S.obj "%SRCSDK%\tier1\%%S.cpp"
  if errorlevel 1 goto :BAD
)
%LJLINK% /DLL /MAP /DEBUG /PDB:%LJDLLNAME%.pdb /out:%LJDLLNAME% lj_*.obj lib_*.obj convar_impl.obj support_*.obj "%HOSTLIB%\tier0.lib" "%HOSTLIB%\vstdlib.lib" shell32.lib shlwapi.lib user32.lib advapi32.lib
@if errorlevel 1 goto :BAD
@goto :MTDLL
:STATIC
%LJCOMPILE% lj_*.c lib_*.c
@if errorlevel 1 goto :BAD
%LJLIB% /OUT:%LJLIBNAME% lj_*.obj lib_*.obj
@if errorlevel 1 goto :BAD
@goto :MTDLL
:AMALGDLL
%LJCOMPILE% /MT /DOPGLUA_LOG /DLUA_BUILD_AS_DLL ljamalg.c
@if errorlevel 1 goto :BAD
%LJLINK% /DLL /out:%LJDLLNAME% ljamalg.obj lj_vm.obj
@if errorlevel 1 goto :BAD
:MTDLL
if exist %LJDLLNAME%.manifest^
  %LJMT% -manifest %LJDLLNAME%.manifest -outputresource:%LJDLLNAME%;2

%LJCOMPILE% luajit.c
@if errorlevel 1 goto :BAD
%LJLINK% /out:luajit.exe luajit.obj %LJLIBNAME%
@if errorlevel 1 goto :BAD
if exist luajit.exe.manifest^
  %LJMT% -manifest luajit.exe.manifest -outputresource:luajit.exe

@del *.obj *.manifest minilua.exe buildvm.exe
@del host\buildvm_arch.h
@del lj_bcdef.h lj_ffdef.h lj_libdef.h lj_recdef.h lj_folddef.h
@echo.
@echo === Successfully built LuaJIT for Windows/%LJARCH% ===

@exit /b 0
:BAD
@echo.
@echo *******************************************************
@echo *** Build FAILED -- Please check the error messages ***
@echo *******************************************************
@goto :END
@exit /b 1
:FAIL
@echo You must open a "Visual Studio .NET Command Prompt" to run this script
:END
@exit /b 1
