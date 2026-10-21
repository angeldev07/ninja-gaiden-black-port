@echo off
rem Compila el loader nativo (x86, MSVC) en build\port\ del repo.
rem   port\build.cmd           compilación normal (/O2 /Zi)
rem Requiere Visual Studio 2022 (C++ x86). VCVARSALL puede apuntar a otro vcvarsall.bat.
setlocal
set "PORT=%~dp0"
set "SRC=%PORT%src"
set "OUT=%PORT%..\build\port"
if not defined VCVARSALL set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
call "%VCVARSALL%" x86 >nul 2>nul
where cl >nul 2>nul || (echo build: no se encuentra cl.exe; revisa VCVARSALL & exit /b 1)

set "CFLAGS=/nologo /W4 /WX /O2 /Zi /MT /GS /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"%SRC%""

rem 1) ngb-core.dll: el loader, el kernel emulado y los stubs generados.
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\core.pdb" /c ^
  "%SRC%\main.c" "%SRC%\symbols.c" "%SRC%\xbe.c" "%SRC%\memory.c" "%SRC%\log.c" "%SRC%\thread.c" "%SRC%\crash.c" ^
  "%SRC%\kernel\stubs.c" "%SRC%\kernel\dispatch.c" "%SRC%\kernel\data.c" ^
  "%SRC%\kernel\impl_mm.c" "%SRC%\kernel\impl_rtl.c" "%SRC%\kernel\impl_ke.c" ^
  "%SRC%\kernel\impl_ps.c" "%SRC%\kernel\impl_io.c" "%SRC%\kernel\impl_misc.c" "%SRC%\kernel\hostpath.c" ^
  "%SRC%\kernel\sha1.c" "%SRC%\apu\vp.c" "%SRC%\gpu\nv2a.c" "%SRC%\gpu\pgraph.c" "%SRC%\gpu\pushbuf.c" "%SRC%\gpu\png.c" ^
  "%SRC%\gpu\nv097_names.c" "%SRC%\gpu\raster.c" ^
  "%SRC%\input\input.c" "%SRC%\input\xapi_hle.c" ^
  "%SRC%\gfx\present.c" || exit /b 1
link /nologo /DLL /DEBUG /OUT:"%OUT%\ngb-core.dll" /PDB:"%OUT%\ngb-core.pdb" ^
  /BASE:0x60000000 /LARGEADDRESSAWARE ^
  "%OUT%\obj\main.obj" "%OUT%\obj\symbols.obj" "%OUT%\obj\xbe.obj" "%OUT%\obj\memory.obj" "%OUT%\obj\log.obj" ^
  "%OUT%\obj\thread.obj" "%OUT%\obj\crash.obj" "%OUT%\obj\stubs.obj" "%OUT%\obj\dispatch.obj" ^
  "%OUT%\obj\data.obj" "%OUT%\obj\impl_mm.obj" "%OUT%\obj\impl_rtl.obj" "%OUT%\obj\impl_ke.obj" ^
  "%OUT%\obj\impl_ps.obj" "%OUT%\obj\impl_io.obj" "%OUT%\obj\impl_misc.obj" "%OUT%\obj\hostpath.obj" ^
  "%OUT%\obj\sha1.obj" "%OUT%\obj\vp.obj" "%OUT%\obj\nv2a.obj" "%OUT%\obj\pgraph.obj" "%OUT%\obj\pushbuf.obj" "%OUT%\obj\png.obj" ^
  "%OUT%\obj\nv097_names.obj" "%OUT%\obj\raster.obj" ^
  "%OUT%\obj\input.obj" "%OUT%\obj\xapi_hle.obj" ^
  "%OUT%\obj\present.obj" kernel32.lib shell32.lib user32.lib gdi32.lib || exit /b 1

rem 2) ngb-loader.exe: base fija 0x10000 + BSS que reserva la imagen del XBE. Sin CRT.
cl /nologo /W4 /WX /O1 /GS- /Zi /utf-8 /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\stub.pdb" /c "%SRC%\loader_stub.c" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\ngb-loader.exe" /PDB:"%OUT%\ngb-loader.pdb" ^
  /NODEFAULTLIB /ENTRY:ngb_stub_entry /SUBSYSTEM:CONSOLE ^
  /BASE:0x10000 /FIXED /DYNAMICBASE:NO /NXCOMPAT:NO /SAFESEH:NO /LARGEADDRESSAWARE ^
  /SECTION:.xbox,RWE ^
  "%OUT%\obj\loader_stub.obj" kernel32.lib || exit /b 1

rem 3a) Prueba del confinamiento de rutas (hostpath.c), sin datos del juego.
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\hostpath_test.pdb" /Fe"%OUT%\hostpath_test.exe" "%PORT%tests\hostpath_test.c" "%SRC%\kernel\hostpath.c" || exit /b 1
"%OUT%\hostpath_test.exe" || exit /b 1
rem 3b) Prueba del decodificador del push buffer (buffers hechos a mano).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\pushbuf_test.pdb" /Fe"%OUT%\pushbuf_test.exe" "%PORT%tests\pushbuf_test.c" "%SRC%\gpu\pushbuf.c" || exit /b 1
"%OUT%\pushbuf_test.exe" || exit /b 1
rem 3c) Prueba del mapeo de la entrada (mando de PC y teclado -> XINPUT_STATE, guion).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\input_test.pdb" /Fe"%OUT%\input_test.exe" "%PORT%tests\input_test.c" "%SRC%\input\input.c" || exit /b 1
"%OUT%\input_test.exe" || exit /b 1
rem 3d) SHA-1 y HMAC de XcSHA*/XcHMAC (vectores de FIPS 180 y RFC 2202).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\sha1_test.pdb" /Fe"%OUT%\sha1_test.exe" "%PORT%tests\sha1_test.c" "%SRC%\kernel\sha1.c" || exit /b 1
"%OUT%\sha1_test.exe" || exit /b 1
rem 3e) Procesador de voces de la APU (tabla de voces sintética).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\vp_test.pdb" /Fe"%OUT%\vp_test.exe" "%PORT%tests\vp_test.c" "%SRC%\apu\vp.c" || exit /b 1
"%OUT%\vp_test.exe" || exit /b 1

cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\shaders_test.pdb" /Fe"%OUT%\shaders_test.exe" "%PORT%tests\shaders_test.c" "%SRC%\shaders\vsh.c" "%SRC%\shaders\psh.c" "%SRC%\shaders\emit.c" && "%OUT%\shaders_test.exe" || exit /b 1
rem 3c) Prueba del rasterizador por software (triángulo y textura 2x2 a mano).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\raster_test.pdb" /Fe"%OUT%\raster_test.exe" "%PORT%tests\raster_test.c" "%SRC%\gpu\raster.c" || exit /b 1
"%OUT%\raster_test.exe" || exit /b 1

rem 4) Prueba del presentador de frames (gfx\present.c). Abre una ventana: no se ejecuta
rem    aquí (tests\port\test_gfx.py la lanza oculta con --frames 3 --hidden).
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\gfx_test.pdb" /Fe"%OUT%\gfx_test.exe" "%PORT%tests\gfx_test.c" "%SRC%\gfx\present.c" user32.lib gdi32.lib || exit /b 1

cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\gl_test.pdb" /Fe"%OUT%\gl_test.exe" "%PORT%tests\gl_test.c" "%SRC%\gl\glbackend.c" "%SRC%\gl\glload.c" "%SRC%\gl\glpng.c" "%SRC%\gl\texconv.c" "%SRC%\gl\glstate.c" "%SRC%\shaders\vsh.c" "%SRC%\shaders\psh.c" "%SRC%\shaders\emit.c" opengl32.lib user32.lib gdi32.lib || exit /b 1
cl %CFLAGS% /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\texconv_test.pdb" /Fe"%OUT%\texconv_test.exe" "%PORT%tests\texconv_test.c" "%SRC%\gl\texconv.c" "%SRC%\gl\glstate.c" "%SRC%\gl\glbackend.c" "%SRC%\gl\glload.c" opengl32.lib user32.lib gdi32.lib && "%OUT%\texconv_test.exe" || exit /b 1
echo build: %OUT%\ngb-loader.exe + ngb-core.dll
endlocal
