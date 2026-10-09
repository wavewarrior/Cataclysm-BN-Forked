@echo off
rem Open the factory herdr workspace: operator shell, watcher and status panes. Safe to re-run.
rem Run from the checkout whose tools\factory is the one to drive.
call "%~dp0env.cmd"
where deno >nul 2>&1
if errorlevel 1 (echo deno is not on PATH and not in the WinGet packages & exit /b 2)
where herdr >nul 2>&1
if errorlevel 1 (echo herdr is not on PATH and not under %USERPROFILE%\.herdr & exit /b 2)
cd /d "%~dp0..\.."
deno run -A tools/factory/main.ts boot %*
if errorlevel 1 exit /b %errorlevel%
rem The herdr server is headless: open a client window so the workspace is visible.
start "" herdr
exit /b 0
