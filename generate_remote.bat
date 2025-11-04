@echo off
REM Generate Remote.h with git remote URL

git config --get remote.origin.url > temp_file.h 2>nul
IF %ERRORLEVEL% EQU 0 (
    SET /p remoteurl=<temp_file.h
) ELSE (
    SET remoteurl=unknown
)

IF NOT DEFINED remoteurl SET remoteurl=unknown

SETLOCAL enabledelayedexpansion
SET "remoteurl=!remoteurl:.git=!"
SET remotemacro=#define REMOTE_URL "!remoteurl!"

ECHO #pragma once > src/Remote.h
ECHO !remotemacro! >> src/Remote.h

ENDLOCAL

IF EXIST temp_file.h DEL temp_file.h

ECHO Generated Remote.h: %remoteurl%
