@echo off
set START_TIME=%TIME%

REM Set Windows CMD variables
set INST_DIR=%CD%
set MSYS_SH=C:\MinGW\msys\1.0\bin\sh.exe
set PATH_TOOLCHAIN=C:\mingw32\bin

REM Set MSYS variables
set MSYS_BIN=/c/MinGW/msys/1.0/bin
set MINGW_BIN=/c/MinGW/bin
set TOOLCHAIN_BIN=/c/mingw32/bin
set MSYS_PATH_DEPS=/c/deps
set PATH_BITCOIN=/c/Development/BitcoinUnlimited

REM Perform build steps that require Windows CMD
set PATH=%PATH_TOOLCHAIN%;%PATH%

REM Now Build Bitcoin
echo Building bitcoin
%MSYS_SH% %INST_DIR%\make-bitcoin.sh --path-bitcoin=%PATH_BITCOIN% --path-deps=%MSYS_PATH_DEPS% --path-msys=%MSYS_BIN% --path-toolchain=%TOOLCHAIN_BIN% --path-mingw=%MINGW_BIN%

echo Bitcoin Build stated at %START_TIME% and ended at %TIME%
pause