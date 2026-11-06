@echo off
if not exist F:\Tporject\tmp mkdir F:\Tporject\tmp
if not exist F:\Tporject\cache\pip mkdir F:\Tporject\cache\pip
if not exist F:\Tporject\cache\nuget mkdir F:\Tporject\cache\nuget
set "PATH=C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0;C:\Program Files\Git\cmd;C:\Users\prime\AppData\Local\Programs\Python\Python311;C:\Users\prime\AppData\Local\Programs\Python\Python311\Scripts"
set VSLANG=1033
set GIT_CLONE_PROTECTION_ACTIVE=false
set TEMP=F:\Tporject\tmp
set TMP=F:\Tporject\tmp
set PIP_CACHE_DIR=F:\Tporject\cache\pip
set NUGET_PACKAGES=F:\Tporject\cache\nuget
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\atlmfc\include"
set "LIB=%LIB%;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\atlmfc\lib\x64"
cd /d F:\Tporject
call tdesktop\Telegram\build\prepare\win.bat >F:\Tporject\prepare.log 2>&1
echo EXIT %errorlevel% >> F:\Tporject\prepare.log
