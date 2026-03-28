@echo off

set AMIGA_INC=C:\amiga-dev\targets\m68k-amigaos\include
set AMIGA_POSIX_INC=C:\amiga-dev\targets\m68k-amigaos\posix\include
set AMIGA_NDK_INC=C:\amiga-dev\targets\m68k-amigaos\ndk\include_h
set BUILD_ARCHIVE_DIR=archive
set ARBFILES_VERSION=

for /f "tokens=3" %%V in ('findstr /c:"#define ARBFILES_VERSION" door_version.h') do set ARBFILES_VERSION=%%V
set ARBFILES_VERSION=%ARBFILES_VERSION:"=%
if not defined ARBFILES_VERSION set ARBFILES_VERSION=unknown

echo.
echo ****************************************************************************
echo  arbfiles Build Script
echo ****************************************************************************

echo  Building arbfiles...
vc -I"%AMIGA_INC%" -I"%AMIGA_POSIX_INC%" -I"%AMIGA_NDK_INC%" -o arbfiles arbfiles.c doorlog.c door_config.c aedoor_bridge.c ae_config_scan.c dirlist.c file_ops.c ui.c
if errorlevel 1 goto fail

echo.
echo  Output:
for %%F in (arbfiles) do echo    %%F  %%~zF bytes
if not exist "%BUILD_ARCHIVE_DIR%" mkdir "%BUILD_ARCHIVE_DIR%"
copy /y arbfiles "%BUILD_ARCHIVE_DIR%\arbfiles_%ARBFILES_VERSION%" >nul
if exist "%BUILD_ARCHIVE_DIR%\arbfiles_%ARBFILES_VERSION%" echo    archived copy  %BUILD_ARCHIVE_DIR%\arbfiles_%ARBFILES_VERSION%
echo.
echo ****************************************************************************
echo  Build OK
echo ****************************************************************************
exit /b 0

:fail
echo.
echo  *** BUILD FAILED ***
echo ****************************************************************************
exit /b 1
