@echo off
REM Default Boost build to exclude tests
set BOOST_ENABLE_TESTS=""

REM ##################################################################################################
REM These paths need to be customized to match your system
REM ##################################################################################################

REM If you want to run tests ("make check" and "rpc-tests" you must uncomment below line).
REM set BOOST_ENABLE_TESTS="--with-test"

REM All paths defined below must be absolute paths.  Relative paths will break the scripts.

REM Set MinGW root installation path (CMD path format)
REM This should be where you installed mingw-get-setup.exe
set MINGW_ROOT_WIN=C:\MinGW
set MINGW_ROOT_NIX=/c/MinGW

REM Set 7-Zip installation path (Linux path format)
REM This should be where you installed 7-Zip
set CMD_7ZIP="/c/Program Files/7-Zip/7z.exe"

REM Set the toolchain path.  This is where the mingw64 tool chain will be installed to by this script
set TOOLCHAIN_ROOT_WIN=C:\mingw32
set TOOLCHAIN_ROOT_NIX=/c/mingw32

REM Set the dependency path.  This is where all of the Bitcoin dependencies will be downloaded and built
set DEPS_ROOT_WIN=C:\deps
set DEPS_ROOT_NIX=/c/deps

REM Set the path to your Bitcoin git checkout.  We will build Bitcoin at the end of this script to
REM ensure all dependencies link up correctly.
set BITCOIN_GIT_ROOT_NIX=/c/Development/BitcoinUnlimited


REM ##################################################################################################
REM Automated Area (You shouldn't need to edit below this line)
REM ##################################################################################################
set INST_DIR=%CD%
set MINGW_GET_WIN=%MINGW_ROOT_WIN%\bin\mingw-get.exe
set MSYS_SH_WIN=%MINGW_ROOT_WIN%\msys\1.0\bin\sh.exe
set MINGW_BIN_NIX=%MINGW_ROOT_NIX%/bin
set MSYS_BIN_NIX=%MINGW_ROOT_NIX%/msys/1.0/bin
set TOOLCHAIN_BIN_WIN=%TOOLCHAIN_ROOT_WIN%\bin
set TOOLCHAIN_BIN_NIX=%TOOLCHAIN_ROOT_NIX%/bin
set INCLUDE_TESTS=""
if %BOOST_ENABLE_TESTS% NEQ "" set INCLUDE_TESTS="--check"


REM Capture timing metrics
set START_TIME=%TIME%

REM Install required msys base package (provide access to msys sh shell)
%MINGW_GET_WIN% update
%MINGW_GET_WIN% install msys-base-bin

REM Install remaining toolchain components using msys sh.exe shell
echo Installing toolchain...
%MSYS_SH_WIN% %INST_DIR%\install-toolchain.sh --path-7zip=%CMD_7ZIP% --path-deps=%DEPS_ROOT_NIX% --path-msys=%MSYS_BIN_NIX% --path-toolchain=%TOOLCHAIN_BIN_NIX% --path-mingw=%MINGW_BIN_NIX%


REM Download and unpack all dependencies. Build those that aren't required to be build from Windows CMD (msys native)
echo Installing dependencies...
%MSYS_SH_WIN% %INST_DIR%\install-deps.sh --path-7zip=%CMD_7ZIP% --path-deps=%DEPS_ROOT_NIX% --path-msys=%MSYS_BIN_NIX% --path-toolchain=%TOOLCHAIN_BIN_NIX% --path-mingw=%MINGW_BIN_NIX%

REM Perform build steps that require Windows CMD
set PATH=%TOOLCHAIN_BIN_WIN%;%PATH%

REM Boost (NOTE: Due to bootstrap.bat giving an error exit code, build it last)
echo Building Boost
cd %DEPS_ROOT_WIN%\boost_1_61_0
call bootstrap.bat gcc
b2 --build-type=complete --with-chrono --with-filesystem --with-program_options --with-system --with-thread %BOOST_ENABLE_TESTS% toolset=gcc variant=release link=static threading=multi runtime-link=static stage

REM Miniunpuc
echo Building Miniunpuc...
cd %DEPS_ROOT_WIN%\miniupnpc
mingw32-make -f Makefile.mingw init upnpc-static

REM Qt 5
echo Building Qt 5.3.2...
cd %DEPS_ROOT_WIN%\Qt\5.3.2
set INCLUDE=%DEPS_ROOT_WIN%\libpng-1.6.16;%DEPS_ROOT_WIN%\openssl-1.0.1k\include
set LIB=%DEPS_ROOT_WIN%\libpng-1.6.16\.libs;%DEPS_ROOT_WIN%\openssl-1.0.1k
call configure.bat -release -opensource -confirm-license -static -make libs -no-sql-sqlite -no-opengl -system-zlib -qt-pcre -no-icu -no-gif -system-libpng -no-libjpeg -no-freetype -no-angle -no-vcproj -openssl -no-dbus -no-audio-backend -no-wmf-backend -no-qml-debug
mingw32-make -j4

echo Building Qt Tools...
set PATH=%PATH%;%DEPS_ROOT_WIN%\Qt\5.3.2\bin
set PATH=%PATH%;%DEPS_ROOT_WIN%\Qt\qttools-opensource-src-5.3.2
cd %DEPS_ROOT_WIN%\Qt\qttools-opensource-src-5.3.2
qmake qttools.pro
mingw32-make -j4


REM ##################################################################################################
REM Time to build Bitcoin
REM ##################################################################################################
echo Building bitcoin
%MSYS_SH_WIN% %INST_DIR%\make-bitcoin.sh --path-bitcoin=%BITCOIN_GIT_ROOT_NIX% --path-deps=%DEPS_ROOT_NIX% --path-msys=%MSYS_BIN_NIX% --path-toolchain=%TOOLCHAIN_BIN_NIX% --path-mingw=%MINGW_BIN_NIX% %INCLUDE_TESTS%

REM ##################################################################################################
REM Get end time so we can output execution duration metrics
REM ##################################################################################################
set END_TIME=%TIME%

REM Change formatting for the start and end times
for /F "tokens=1-4 delims=:.," %%a in ("%START_TIME%") do (
   set /A "start=(((%%a*60)+1%%b %% 100)*60+1%%c %% 100)*100+1%%d %% 100"
)

for /F "tokens=1-4 delims=:.," %%a in ("%END_TIME%") do (
   set /A "end=(((%%a*60)+1%%b %% 100)*60+1%%c %% 100)*100+1%%d %% 100"
)

rem Calculate the elapsed time by subtracting values
set /A elapsed=end-start

rem Format the results for output
set /A hh=elapsed/(60*60*100), rest=elapsed%%(60*60*100), mm=rest/(60*100), rest%%=60*100, ss=rest/100, cc=rest%%100
if %hh% lss 10 set hh=0%hh%
if %mm% lss 10 set mm=0%mm%
if %ss% lss 10 set ss=0%ss%
if %cc% lss 10 set cc=0%cc%

set DURATION=%hh%:%mm%:%ss%.%cc%

echo Start    : %START_TIME%
echo Finish   : %END_TIME%
echo          ---------------
echo Duration : %DURATION% 
pause