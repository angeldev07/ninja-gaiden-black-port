@echo off
rem Juega: abre Ninja Gaiden Black en una ventana con el loader ya compilado (port\build.cmd).
rem   port\play.cmd               con sonido, mando de PC y teclado
rem   port\play.cmd --mute        cualquier opción extra de ngb-loader.exe se pasa tal cual
rem El juego se busca en %NGB_DATA%\game\default.xbe (por defecto %USERPROFILE%\ngb-data).
setlocal
if not defined NGB_DATA set "NGB_DATA=%USERPROFILE%\ngb-data"
set "LOADER=%~dp0..\build\port\ngb-loader.exe"
if not exist "%LOADER%" (echo play: falta %LOADER%; compila con port\build.cmd & exit /b 1)
if not exist "%NGB_DATA%\game\default.xbe" (echo play: no encuentro %NGB_DATA%\game\default.xbe & exit /b 1)
"%LOADER%" "%NGB_DATA%\game\default.xbe" --window %*
endlocal
