@echo off
rem Put deno and herdr on PATH when they are installed but missing from PATH.
rem Use from another launcher: call "%~dp0env.cmd"
where deno >nul 2>&1
if errorlevel 1 for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\DenoLand.Deno*") do set "PATH=%%D;%PATH%"
where herdr >nul 2>&1
if errorlevel 1 for /d %%D in ("%USERPROFILE%\.herdr\packages\standalone\releases\*") do set "PATH=%%D;%PATH%"
