@echo off
REM Generate Branch.h with current git branch name

git branch --show-current > temp_branch.txt 2>nul
IF %ERRORLEVEL% EQU 0 (
    SET /p branchname=<temp_branch.txt
) ELSE (
    SET branchname=unknown
)

IF NOT DEFINED branchname SET branchname=unknown

ECHO #pragma once > src\Branch.h
ECHO #define BRANCH_NAME "%branchname%" >> src\Branch.h

IF EXIST temp_branch.txt DEL temp_branch.txt

ECHO Generated Branch.h: %branchname%
