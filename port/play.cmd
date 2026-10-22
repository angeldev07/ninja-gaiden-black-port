@echo off
rem Play: opens Ninja Gaiden Black in a window with the already built loader (port\build.cmd).
rem   port\play.cmd               with sound, PC gamepad and keyboard
rem   port\play.cmd --mute        any extra ngb-loader.exe option is passed through as is
rem The game is looked up at %NGB_DATA%\game\default.xbe (default %USERPROFILE%\ngb-data).
setlocal
if not defined NGB_DATA set "NGB_DATA=%USERPROFILE%\ngb-data"
set "LOADER=%~dp0..\build\port\ngb-loader.exe"
if not exist "%LOADER%" (echo play: %LOADER% is missing; build with port\build.cmd & exit /b 1)
if not exist "%NGB_DATA%\game\default.xbe" (echo play: cannot find %NGB_DATA%\game\default.xbe & exit /b 1)
"%LOADER%" "%NGB_DATA%\game\default.xbe" --window %*
endlocal
