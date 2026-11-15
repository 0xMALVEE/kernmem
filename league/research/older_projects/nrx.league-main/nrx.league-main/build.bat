@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: nrx.league Build Script
:: Usage: build.bat [target]
::
:: Targets:
::   release      - Build Release|x64 DLL (default)
::   debug        - Build Debug|x64 DLL
::   release-x86  - Build Release|Win32 EXE
::   debug-x86    - Build Debug|Win32 EXE
::   all          - Build all 4 configurations
::   clean        - Clean Release|x64
::   clean-all    - Clean all configurations
::   rebuild      - Rebuild Release|x64
::   info         - Show build info
:: ============================================================================

:: --- Auto-detect MSBuild ---
set "MSBUILD="

:: VS 2025 (v18) Community
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    goto :found
)
:: VS 2025 (v18) Professional
if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
    goto :found
)
:: VS 2025 (v18) Enterprise
if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
    goto :found
)
:: VS 2022 (v17) Community
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    goto :found
)
:: VS 2022 (v17) Professional
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
    goto :found
)
:: VS 2022 (v17) Enterprise
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe" (
    set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
    goto :found
)
:: Fallback: try PATH
where MSBuild.exe >nul 2>&1
if %errorlevel% equ 0 (
    set "MSBUILD=MSBuild.exe"
    goto :found
)

echo [ERROR] MSBuild.exe not found. Install Visual Studio 2022+ with C++ workload.
exit /b 1

:found
set "SLN=%~dp0leagueone.sln"

:: Default to release if no argument
if "%~1"=="" goto :release
goto :%~1

:: ============================================================================
:: Build Targets
:: ============================================================================

:release
echo [BUILD] Release x64
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x64 /t:Build /m /nologo /v:minimal
goto :done

:debug
echo [BUILD] Debug x64
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=x64 /t:Build /m /nologo /v:minimal
goto :done

:release-x86
echo [BUILD] Release Win32
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /t:Build /m /nologo /v:minimal
goto :done

:debug-x86
echo [BUILD] Debug Win32
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /t:Build /m /nologo /v:minimal
goto :done

:all
echo [BUILD] All configurations
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x64 /t:Build /m /nologo /v:minimal
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=x64 /t:Build /m /nologo /v:minimal
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /t:Build /m /nologo /v:minimal
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /t:Build /m /nologo /v:minimal
goto :done

:clean
echo [CLEAN] Release x64
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x64 /t:Clean /m /nologo /v:quiet
goto :done

:clean-all
echo [CLEAN] All configurations
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x64 /t:Clean /m /nologo /v:quiet
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=x64 /t:Clean /m /nologo /v:quiet
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=Win32 /t:Clean /m /nologo /v:quiet
"%MSBUILD%" "%SLN%" /p:Configuration=Debug /p:Platform=Win32 /t:Clean /m /nologo /v:quiet
if exist x64 rmdir /s /q x64
if exist Debug rmdir /s /q Debug
if exist Release rmdir /s /q Release
echo [CLEAN] Done
goto :done

:rebuild
echo [REBUILD] Release x64
"%MSBUILD%" "%SLN%" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m /nologo /v:minimal
goto :done

:info
echo =============================================
echo  nrx.league Build Info
echo =============================================
echo  MSBuild: %MSBUILD%
echo  Solution: %SLN%
echo =============================================
echo.
echo  Usage: build.bat [target]
echo.
echo  Targets:
echo    release      Build Release x64 DLL (default)
echo    debug        Build Debug x64 DLL
echo    release-x86  Build Release Win32 EXE
echo    debug-x86    Build Debug Win32 EXE
echo    all          Build all configurations
echo    clean        Clean Release x64
echo    clean-all    Clean everything
echo    rebuild      Rebuild Release x64
echo    info         Show this info
echo =============================================
goto :done

:done
endlocal
