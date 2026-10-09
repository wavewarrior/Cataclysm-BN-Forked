@rem Put deno and herdr on PATH when installed but missing from PATH.
@rem Use from another launcher or an interactive pane: call "%~dp0env.cmd"
@rem No @echo off: it persists in the caller's pane and hides the prompt. Every line is @-prefixed instead.
@where deno >nul 2>&1
@if errorlevel 1 call :add_newest "%LOCALAPPDATA%\Microsoft\WinGet\Packages" "DenoLand.Deno*" "deno.exe"
@where herdr >nul 2>&1
@if errorlevel 1 call :add_newest "%USERPROFILE%\.herdr\packages\standalone\releases" "*" "herdr.EXE"
@goto :eof

@rem Prepend only the newest version directory, so an old herdr release cannot shadow a new one.
:add_newest
@for /f "delims=" %%D in ('dir /b /ad /o-d "%~1\%~2" 2^>nul') do @if exist "%~1\%%D\%~3" @(set "PATH=%~1\%%D;%PATH%" & goto :eof)
@goto :eof
