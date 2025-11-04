@echo off
REM Generate Remote.h with git remote URL

git config --get remote.origin.url > temp_remote.txt 2>nul
IF %ERRORLEVEL% EQU 0 (
    SET /p remoteurl=<temp_remote.txt
) ELSE (
    SET remoteurl=unknown
)

IF NOT DEFINED remoteurl SET remoteurl=unknown

REM Remove .git suffix if present
SET remoteurl=%remoteurl:.git=%

ECHO #pragma once > src\Remote.h
ECHO #define REMOTE_URL "%remoteurl%" >> src\Remote.h

IF EXIST temp_remote.txt DEL temp_remote.txt

ECHO Generated Remote.h: %remoteurl%
