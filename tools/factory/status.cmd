@echo off
rem Redraw `deno task factory status` every 60 seconds in a herdr cmd pane. Ctrl+C stops it.
call "%~dp0env.cmd"
cd /d "%~dp0..\.."
:loop
cls
deno run -A tools/factory/main.ts status
ping -n 61 127.0.0.1 >nul
goto loop
