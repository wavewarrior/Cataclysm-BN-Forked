@echo off
rem Poll factory:ready GitHub tickets and run the factory on any that can be picked up.
rem Run in a herdr cmd pane from anywhere: tools\factory\watch.cmd [--interval 60] [--once]
rem Ctrl+C finishes the current pass and exits; a second Ctrl+C exits at once.
call "%~dp0env.cmd"
where deno >nul 2>&1
if errorlevel 1 (echo deno is not on PATH and not in the WinGet packages & exit /b 2)
rem The driver's implementer lanes set FACTORY_LANE themselves; the watcher must not inherit it.
set "FACTORY_LANE="
cd /d "%~dp0..\.."
deno run -A tools/factory/main.ts watch %*
exit /b %errorlevel%
