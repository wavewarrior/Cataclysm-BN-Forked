@echo off
rem Put deno and herdr on PATH when they are installed but missing from PATH.
rem Use from another launcher: call "%~dp0env.cmd"
rem Keep this file free of echo output: it runs inside interactive panes.
where deno >nul 2>&1
if errorlevel 1 call :add_newest "%LOCALAPPDATA%\Microsoft\WinGet\Packages" "DenoLand.Deno*" "deno.exe"
where herdr >nul 2>&1
if errorlevel 1 call :add_newest "%USERPROFILE%\.herdr\packages\standalone\releases" "*" "herdr.EXE"
goto :eof

rem Prepend the newest version directory only, so an old herdr release cannot shadow a new one.
:add_newest
for /f "delims=" %%D in ('dir /b /ad /o-d "%~1\%~2" 2^>nul') do (
  if exist "%~1\%%D\%~3" (
    set "PATH=%~1\%%D;%PATH%"
    goto :eof
  )
)
goto :eof
