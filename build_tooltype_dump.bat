@echo off

set AMIGA_INC=C:\amiga-dev\targets\m68k-amigaos\include
set AMIGA_POSIX_INC=C:\amiga-dev\targets\m68k-amigaos\posix\include
set AMIGA_NDK_INC=C:\amiga-dev\targets\m68k-amigaos\ndk\include_h

echo.
echo ****************************************************************************
echo  tooltype_dump Build Script
echo ****************************************************************************

echo  Building tooltype_dump...
vc -I"%AMIGA_INC%" -I"%AMIGA_POSIX_INC%" -I"%AMIGA_NDK_INC%" -o tooltype_dump tooltype_dump.c
if errorlevel 1 goto fail

echo.
echo  Output:
for %%F in (tooltype_dump) do echo    %%F  %%~zF bytes
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
