@echo off
rem usage: build_telegram.bat configure | build
call F:\Tporject\secrets.local.bat
set GIT_CLONE_PROTECTION_ACTIVE=false
if not exist F:\Tporject\tmp mkdir F:\Tporject\tmp
set "PATH=C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0;C:\Program Files\Git\cmd;C:\Users\prime\AppData\Local\Programs\Python\Python311;C:\Users\prime\AppData\Local\Programs\Python\Python311\Scripts"
set TEMP=F:\Tporject\tmp
set TMP=F:\Tporject\tmp
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
cd /d F:\Tporject\tdesktop
if /i "%1"=="release" (
    cmake --build out --config Release --target Telegram --parallel > F:\Tporject\build_release.log 2>&1
    echo EXIT %errorlevel% >> F:\Tporject\build_release.log
) else if /i "%1"=="configure" (
    call Telegram\configure.bat x64 -D TDESKTOP_API_ID=%TG_API_ID% -D TDESKTOP_API_HASH=%TG_API_HASH% > F:\Tporject\configure.log 2>&1
    echo EXIT %errorlevel% >> F:\Tporject\configure.log
) else (
    cmake --build out --config Debug --target Telegram --parallel > F:\Tporject\build.log 2>&1
    echo EXIT %errorlevel% >> F:\Tporject\build.log
)
