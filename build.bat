@echo off
setlocal

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

set "BUILD_DIR=%ROOT%\build"
set "MAYA_LOCATION=C:\Program Files\Autodesk\Maya2026"
set "CONFIG=Release"

if not "%~1"=="" (
    set "CONFIG=%~1"
)

echo ============================================================
echo Build meshAssembler for Maya 2026
echo ROOT          : %ROOT%
echo BUILD_DIR     : %BUILD_DIR%
echo MAYA_LOCATION : %MAYA_LOCATION%
echo CONFIG        : %CONFIG%
echo ============================================================
echo.

if not exist "%MAYA_LOCATION%" (
    echo ERROR: Maya location not found:
    echo %MAYA_LOCATION%
    exit /b 1
)

cmake ^
    -S "%ROOT%" ^
    -B "%BUILD_DIR%" ^
    -DMAYA_LOCATION:PATH="%MAYA_LOCATION%"

if errorlevel 1 (
    echo.
    echo ERROR: CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config "%CONFIG%"

if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo ============================================================
echo Build succeeded.
echo Output:
echo %BUILD_DIR%\%CONFIG%\meshAssembler.mll
echo ============================================================

endlocal
