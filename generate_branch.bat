@echo off
REM Generate Branch.h with current git branch name

git branch --show-current > temp_file.h 2>nul
IF %ERRORLEVEL% EQU 0 (
    SET /p branchname=<temp_file.h
) ELSE (
    SET branchname=unknown
)

IF NOT DEFINED branchname SET branchname=unknown

SET branchmacro=#define BRANCH_NAME "%branchname%"
ECHO #pragma once > src/Branch.h
ECHO %branchmacro% >> src/Branch.h

IF EXIST temp_file.h DEL temp_file.h

ECHO Generated Branch.h: %branchname%
