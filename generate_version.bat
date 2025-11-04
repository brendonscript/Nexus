@echo off
REM Generate Version.h with current UTC time

REM Try WMIC first (older Windows)
WMIC.EXE Alias /? >NUL 2>&1
IF %ERRORLEVEL% EQU 0 (
    FOR /F "skip=1 tokens=1-6" %%G IN ('WMIC Path win32_utctime Get Day^,Hour^,Minute^,Month^,Second^,Year /Format:table') DO (
        IF "%%~L"=="" GOTO wmic_done
        SET _yyyy=%%L
        SET _mm=%%J
        SET _dd=%%G
        SET _hour=%%H
        SET _minute=%%I
    )
    :wmic_done
) ELSE (
    REM Fallback to PowerShell for newer Windows where WMIC is removed
    FOR /F "tokens=1-6" %%A IN ('powershell -NoProfile -Command "$d=[DateTime]::UtcNow; Write-Output \"$($d.Year) $($d.Month) $($d.Day) $($d.Hour) $($d.Minute) $($d.Second)\""') DO (
        SET _yyyy=%%A
        SET _mm=%%B
        SET _dd=%%C
        SET _hour=%%D
        SET _minute=%%E
    )
)

REM Ensure variables are set (fallback to defaults if something went wrong)
IF NOT DEFINED _yyyy SET _yyyy=2025
IF NOT DEFINED _mm SET _mm=11
IF NOT DEFINED _dd SET _dd=4
IF NOT DEFINED _hour SET _hour=18
IF NOT DEFINED _minute SET _minute=0

ECHO #pragma once > src/Version.h
ECHO #define V_MAJOR %_yyyy% >> src/Version.h
ECHO #define V_MINOR %_mm% >> src/Version.h
ECHO #define V_BUILD %_dd% >> src/Version.h
SET /A var_res = %_hour% * 60 + %_minute%
ECHO #define V_REVISION %var_res% >> src/Version.h

ECHO Generated Version.h: %_yyyy%.%_mm%.%_dd%.%var_res%
