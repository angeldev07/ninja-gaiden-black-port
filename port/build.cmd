@echo off
rem Builds the native loader (x86, MSVC) into build\port\ of the repo.
rem   port\build.cmd           normal build (/O2 /Zi)
rem Requires Visual Studio 2022 (C++ x86). VCVARSALL can point to another vcvarsall.bat.
setlocal
set "PORT=%~dp0"
set "SRC=%PORT%src"
set "OUT=%PORT%..\build\port"
if not defined VCVARSALL set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
call "%VCVARSALL%" x86 >nul 2>nul
where cl >nul 2>nul || (echo build: cl.exe not found; check VCVARSALL & exit /b 1)

set "CFLAGS=/nologo /W4 /WX /O2 /Zi /MT /GS /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"%SRC%""

rem 0) Resources (icon and version) of ngb-core.dll and ngb-loader.exe: res\ngb.rc, see res\make_icon.py.
where rc >nul 2>nul || (echo build: rc.exe not found; check VCVARSALL & exit /b 1)
rc /nologo /DNGB_RC_CORE /I"%PORT%res" /fo"%OUT%\obj\core.res" "%PORT%res\ngb.rc" || exit /b 1
rc /nologo /DNGB_RC_LOADER /I"%PORT%res" /fo"%OUT%\obj\loader.res" "%PORT%res\ngb.rc" || exit /b 1

rem 1) ngb-core.dll: the loader, the emulated kernel and the generated stubs.
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\core.pdb" /c ^
  "%SRC%\main.c" "%SRC%\symbols.c" "%SRC%\xbe.c" "%SRC%\memory.c" "%SRC%\log.c" "%SRC%\watch.c" "%SRC%\thread.c" "%SRC%\crash.c" ^
  "%SRC%\kernel\stubs.c" "%SRC%\kernel\dispatch.c" "%SRC%\kernel\data.c" ^
  "%SRC%\kernel\impl_mm.c" "%SRC%\kernel\impl_rtl.c" "%SRC%\kernel\impl_ke.c" ^
  "%SRC%\kernel\impl_ps.c" "%SRC%\kernel\impl_io.c" "%SRC%\kernel\impl_misc.c" "%SRC%\kernel\hostpath.c" ^
  "%SRC%\kernel\sha1.c" "%SRC%\kernel\eeprom.c" "%SRC%\apu\vp.c" "%SRC%\apu\mix.c" "%SRC%\apu\audio_out.c" "%SRC%\gpu\nv2a.c" "%SRC%\gpu\pgraph.c" "%SRC%\gpu\rdoc.c" "%SRC%\gpu\pushbuf.c" "%SRC%\gpu\png.c" ^
  "%SRC%\gpu\nv097_names.c" "%SRC%\gpu\raster.c" "%SRC%\gpu\rcache.c" "%SRC%\gpu\pgraph_gl.c" ^
  "%SRC%\gl\glbackend.c" "%SRC%\gl\glload.c" "%SRC%\gl\glstate.c" "%SRC%\gl\texconv.c" ^
  "%SRC%\shaders\vsh.c" "%SRC%\shaders\psh.c" "%SRC%\shaders\emit.c" ^
  "%SRC%\input\input.c" "%SRC%\input\xapi_hle.c" ^
  "%SRC%\gfx\present.c" || exit /b 1
link /nologo /DLL /DEBUG /OUT:"%OUT%\ngb-core.dll" /PDB:"%OUT%\ngb-core.pdb" ^
  /BASE:0x60000000 /LARGEADDRESSAWARE ^
  "%OUT%\obj\main.obj" "%OUT%\obj\symbols.obj" "%OUT%\obj\xbe.obj" "%OUT%\obj\memory.obj" "%OUT%\obj\log.obj" "%OUT%\obj\watch.obj" ^
  "%OUT%\obj\thread.obj" "%OUT%\obj\crash.obj" "%OUT%\obj\stubs.obj" "%OUT%\obj\dispatch.obj" ^
  "%OUT%\obj\data.obj" "%OUT%\obj\impl_mm.obj" "%OUT%\obj\impl_rtl.obj" "%OUT%\obj\impl_ke.obj" ^
  "%OUT%\obj\impl_ps.obj" "%OUT%\obj\impl_io.obj" "%OUT%\obj\impl_misc.obj" "%OUT%\obj\hostpath.obj" ^
  "%OUT%\obj\sha1.obj" "%OUT%\obj\eeprom.obj" "%OUT%\obj\vp.obj" "%OUT%\obj\mix.obj" "%OUT%\obj\audio_out.obj" "%OUT%\obj\nv2a.obj" "%OUT%\obj\pgraph.obj" "%OUT%\obj\rdoc.obj" "%OUT%\obj\pushbuf.obj" "%OUT%\obj\png.obj" ^
  "%OUT%\obj\nv097_names.obj" "%OUT%\obj\raster.obj" "%OUT%\obj\rcache.obj" "%OUT%\obj\pgraph_gl.obj" ^
  "%OUT%\obj\glbackend.obj" "%OUT%\obj\glload.obj" "%OUT%\obj\glstate.obj" "%OUT%\obj\texconv.obj" ^
  "%OUT%\obj\vsh.obj" "%OUT%\obj\psh.obj" "%OUT%\obj\emit.obj" ^
  "%OUT%\obj\input.obj" "%OUT%\obj\xapi_hle.obj" ^
  "%OUT%\obj\present.obj" "%OUT%\obj\core.res" kernel32.lib shell32.lib user32.lib gdi32.lib winmm.lib opengl32.lib || exit /b 1

rem 2) ngb-loader.exe: fixed base 0x10000 + BSS that reserves the XBE image. No CRT.
cl /nologo /W4 /WX /O1 /GS- /Zi /utf-8 /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\stub.pdb" /c "%SRC%\loader_stub.c" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\ngb-loader.exe" /PDB:"%OUT%\ngb-loader.pdb" ^
  /NODEFAULTLIB /ENTRY:ngb_stub_entry /SUBSYSTEM:CONSOLE ^
  /BASE:0x10000 /FIXED /DYNAMICBASE:NO /NXCOMPAT:NO /SAFESEH:NO /LARGEADDRESSAWARE ^
  /SECTION:.xbox,RWE ^
  "%OUT%\obj\loader_stub.obj" "%OUT%\obj\loader.res" kernel32.lib || exit /b 1

rem 3a) Path confinement test (hostpath.c), no game data.
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\hostpath_test.pdb" /Fe"%OUT%\hostpath_test.exe" "%PORT%tests\hostpath_test.c" "%SRC%\kernel\hostpath.c" || exit /b 1
"%OUT%\hostpath_test.exe" || exit /b 1
rem 3b) Push buffer decoder test (hand-made buffers).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\pushbuf_test.pdb" /Fe"%OUT%\pushbuf_test.exe" "%PORT%tests\pushbuf_test.c" "%SRC%\gpu\pushbuf.c" || exit /b 1
"%OUT%\pushbuf_test.exe" || exit /b 1
rem 3c) Input mapping test (PC gamepad and keyboard -> XINPUT_STATE, script).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\input_test.pdb" /Fe"%OUT%\input_test.exe" "%PORT%tests\input_test.c" "%SRC%\input\input.c" || exit /b 1
"%OUT%\input_test.exe" || exit /b 1
rem 3d) SHA-1 and HMAC of XcSHA*/XcHMAC (FIPS 180 and RFC 2202 vectors).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\sha1_test.pdb" /Fe"%OUT%\sha1_test.exe" "%PORT%tests\sha1_test.c" "%SRC%\kernel\sha1.c" || exit /b 1
"%OUT%\sha1_test.exe" || exit /b 1
rem 3d2) Deterministic EEPROM image (ExQueryNonVolatileSetting XC_MAX_*).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\eeprom_test.pdb" /Fe"%OUT%\eeprom_test.exe" "%PORT%tests\eeprom_test.c" "%SRC%\kernel\eeprom.c" || exit /b 1
"%OUT%\eeprom_test.exe" || exit /b 1
rem 3e) APU voice processor (synthetic voice table).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\vp_test.pdb" /Fe"%OUT%\vp_test.exe" "%PORT%tests\vp_test.c" "%SRC%\apu\vp.c" "%SRC%\apu\mix.c" || exit /b 1
"%OUT%\vp_test.exe" || exit /b 1

cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\shaders_test.pdb" /Fe"%OUT%\shaders_test.exe" "%PORT%tests\shaders_test.c" "%SRC%\shaders\vsh.c" "%SRC%\shaders\psh.c" "%SRC%\shaders\emit.c" && "%OUT%\shaders_test.exe" || exit /b 1
rem 3c) Software rasterizer test (hand-made triangle and 2x2 texture).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\raster_test.pdb" /Fe"%OUT%\raster_test.exe" "%PORT%tests\raster_test.c" "%SRC%\gpu\raster.c" || exit /b 1
"%OUT%\raster_test.exe" || exit /b 1
rem 3e) GL render surface and texture caches (rcache.c, no GL).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\rcache_test.pdb" /Fe"%OUT%\rcache_test.exe" "%PORT%tests\rcache_test.c" "%SRC%\gpu\rcache.c" || exit /b 1
"%OUT%\rcache_test.exe" || exit /b 1

rem 4) Frame presenter test (gfx\present.c). It opens a window: it is not run
rem    here (tests\port\test_gfx.py launches it hidden with --frames 3 --hidden).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\gfx_test.pdb" /Fe"%OUT%\gfx_test.exe" "%PORT%tests\gfx_test.c" "%SRC%\gfx\present.c" user32.lib gdi32.lib || exit /b 1

cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\gl_test.pdb" /Fe"%OUT%\gl_test.exe" "%PORT%tests\gl_test.c" "%SRC%\gl\glbackend.c" "%SRC%\gl\glload.c" "%SRC%\gl\glpng.c" "%SRC%\gl\texconv.c" "%SRC%\gl\glstate.c" "%SRC%\shaders\vsh.c" "%SRC%\shaders\psh.c" "%SRC%\shaders\emit.c" opengl32.lib user32.lib gdi32.lib || exit /b 1
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\texconv_test.pdb" /Fe"%OUT%\texconv_test.exe" "%PORT%tests\texconv_test.c" "%SRC%\gl\texconv.c" "%SRC%\gl\glstate.c" "%SRC%\gl\glbackend.c" "%SRC%\gl\glload.c" opengl32.lib user32.lib gdi32.lib && "%OUT%\texconv_test.exe" || exit /b 1

rem 5a) Launcher XDVDFS reader test (synthetic images, no game data).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\xdvdfs_test.pdb" /Fe"%OUT%\xdvdfs_test.exe" "%PORT%tests\xdvdfs_test.c" "%PORT%launcher\xdvdfs.c" || exit /b 1
"%OUT%\xdvdfs_test.exe" || exit /b 1

rem 5) ngb-launcher.exe: the graphical launcher (Win32 + GDI, no dependencies). See launcher\launcher.c.
rc /nologo /I"%PORT%res" /I"%PORT%launcher" /fo"%OUT%\obj\launcher.res" "%PORT%launcher\launcher.rc" || exit /b 1
cl %CFLAGS% /I"%PORT%res" /I"%PORT%launcher" /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\launcher.pdb" /Fe"%OUT%\ngb-launcher.exe" ^
  "%PORT%launcher\launcher.c" "%PORT%launcher\ui.c" "%PORT%launcher\art.c" "%PORT%launcher\import.c" "%PORT%launcher\xdvdfs.c" "%OUT%\obj\launcher.res" ^
  /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib comdlg32.lib advapi32.lib ole32.lib || exit /b 1
echo build: %OUT%\ngb-loader.exe + ngb-core.dll + ngb-launcher.exe
endlocal
