@echo off
rem usage: build_telegram.bat configure | build | release | package | sign
call F:\Tporject\secrets.local.bat
set GIT_CLONE_PROTECTION_ACTIVE=false
if not exist F:\Tporject\tmp mkdir F:\Tporject\tmp
set "PATH=C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0;C:\Program Files\Git\cmd;C:\Users\prime\AppData\Local\Programs\Python\Python311;C:\Users\prime\AppData\Local\Programs\Python\Python311\Scripts"
set TEMP=F:\Tporject\tmp
set TMP=F:\Tporject\tmp
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
cd /d F:\Tporject\tdesktop

if /i "%1"=="configure" (
    echo [PrimeGram] Running configure...
    call Telegram\configure.bat x64 qt5 -D TDESKTOP_API_ID=%TG_API_ID% -D TDESKTOP_API_HASH=%TG_API_HASH% > F:\Tporject\configure.log 2>&1
    echo EXIT %errorlevel% >> F:\Tporject\configure.log
    type F:\Tporject\configure.log
    exit /b %errorlevel%
)

if /i "%1"=="release" (
    echo [PrimeGram] Building Release...
    cmake --build out --config Release --target Telegram --parallel > F:\Tporject\build_release.log 2>&1
    echo EXIT %errorlevel% >> F:\Tporject\build_release.log
    type F:\Tporject\build_release.log
    exit /b %errorlevel%
)

if /i "%1"=="sign" (
    echo [PrimeGram] Signing Telegram.exe...
    if exist out\Release\Telegram.exe (
        call F:\Tporject\sign_exe.bat out\Release\Telegram.exe
    ) else if exist out\Debug\Telegram.exe (
        call F:\Tporject\sign_exe.bat out\Debug\Telegram.exe
    ) else (
        echo [ERROR] No Telegram.exe found to sign!
        exit /b 1
    )
    exit /b 0
)

if /i "%1"=="package" (
    echo [PrimeGram] Building and packaging Release for GitHub Releases...
    cmake --build out --config Release --target Telegram --parallel
    if errorlevel 1 (
        echo [ERROR] Release build failed!
        exit /b 1
    )
    if not exist out\Release\Telegram.exe (
        echo [ERROR] out\Release\Telegram.exe not found!
        exit /b 1
    )
    echo [PrimeGram] Signing executable...
    call F:\Tporject\sign_exe.bat out\Release\Telegram.exe
    if not exist F:\Tporject\releases mkdir F:\Tporject\releases
    for /f "tokens=2 delims= " %%v in ('findstr "AppVersionStrSmall" Telegram\build\version') do set "VER=%%v"
    if "%VER%"=="" set "VER=7.4"
    set "OUT_ZIP=F:\Tporject\releases\PrimeGram-v%VER%-x64-portable.zip"
    echo [PrimeGram] Packaging to %OUT_ZIP%...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path 'out\Release\Telegram.exe' -DestinationPath '%OUT_ZIP%' -Force"
    echo [PrimeGram] Generating SHA256 checksum...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-FileHash -Algorithm SHA256 '%OUT_ZIP%' | Format-List | Out-File -FilePath 'F:\Tporject\releases\SHA256SUMS.txt' -Encoding utf8"
    echo =========================================================
    echo [SUCCESS] Release package ready: %OUT_ZIP%
    echo Checksums saved to F:\Tporject\releases\SHA256SUMS.txt
    echo =========================================================
    exit /b 0
)

echo [PrimeGram] Building Debug...
cmake --build out --config Debug --target Telegram --parallel > F:\Tporject\build.log 2>&1
echo EXIT %errorlevel% >> F:\Tporject\build.log
type F:\Tporject\build.log
exit /b %errorlevel%
