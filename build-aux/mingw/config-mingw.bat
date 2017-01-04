@echo off

REM ##################################################################################################
REM These paths need to be customized to match your system
REM ##################################################################################################

REM If you want to run tests ("make check" and "rpc-tests" you must uncomment below line).
REM set "BOOST_ENABLE_TESTS=--with-test"

REM Following will set up the tool-chain and build dependencies with 32-bit outputs
REM If you don't want to perform 32-bit builds, comment out the line below.
SET BUILD_FOR_x86=TRUE

REM Following will set up the tool-chain and build dependencies with 64-bit outputs
REM If you don't want to perform 64-bit builds, comment out the line below.
SET BUILD_FOR_x64=TRUE

REM All paths defined below must be absolute paths.  Relative paths will break the scripts.

REM Set MinGW root installation path (CMD path format)
REM This should be where you installed mingw-get-setup.exe
REM IMPORTANT: DO NOT USE PATHS WITH SPACES for the MinGW root!  If you do, you WILL run into build errors
set "MINGW_ROOT_WIN=C:\MinGW"
set "MINGW_ROOT_NIX=/c/MinGW"

REM Set 7-Zip installation path (Linux path format)
REM This should be where you installed 7-Zip
set CMD_7ZIP="/c/Program Files/7-Zip/7z.exe"

REM Set the toolchain path.  This is where the mingw64 tool chain will be installed to by this script
REM Default location is to install inside of the MinGW root directory so everything is in one place
REM NOTE: Do not include the "mingw32" or "mingw64" directory in the path as it will automatically be appeneded
REM IMPORTANT: DO NOT USE PATHS WITH SPACES for the Toolchain root!  If you do, you WILL run into build errors
set "TOOLCHAIN_ROOT_WIN=%MINGW_ROOT_WIN%"
set "TOOLCHAIN_ROOT_NIX=%MINGW_ROOT_NIX%"

REM Set the dependency path.  This is where all of the Bitcoin dependencies will be downloaded and built
REM IMPORTANT: DO NOT USE PATHS WITH SPACES for the dependencies root!  If you do, you WILL run into build errors
set "DEPS_ROOT_WIN=C:\deps"
set "DEPS_ROOT_NIX=/c/deps"

REM Set the path to your Bitcoin git checkout.  We will build Bitcoin at the end of this script to
REM ensure all dependencies link up correctly.
REM IMPORTANT: It is HIGHLY RECOMMENDED you do not use paths with spaces for the Bitcoin checkout location.
REM            While I have written these scripts to be able to handle spaces, other scripts called
REM            indirectly (i.e. through .\autogen.sh or .\configure) may not be space tolerant
set "BITCOIN_GIT_ROOT_WIN=C:\Development\BitcoinUnlimited"
set "BITCOIN_GIT_ROOT_NIX=/c/Development/BitcoinUnlimited"

REM ##################################################################################################
REM Automated Area (You shouldn't need to edit below this line)
REM ##################################################################################################
set INST_DIR=%CD%
set MINGW_GET_WIN="%MINGW_ROOT_WIN%\bin\mingw-get.exe"
set MSYS_SH_WIN="%MINGW_ROOT_WIN%\msys\1.0\bin\sh.exe"
set "MINGW_BIN_NIX=%MINGW_ROOT_NIX%/bin"
set "MSYS_BIN_NIX=%MINGW_ROOT_NIX%/msys/1.0/bin"

REM If including tests, set up the flag to pass to make-bitcoin.sh
if "%BOOST_ENABLE_TESTS%" NEQ "" set INCLUDE_TESTS=--check

REM Capture timing metrics
set START_TIME=%TIME%

REM Install required msys base package (provide access to msys sh shell)
echo Updating base MinGW
%MINGW_GET_WIN% update
%MINGW_GET_WIN% install msys-base-bin

REM Remember pre-modification PATH since we need it clean for case where we build both 32 and 64 bit
set "OLD_PATH=%PATH%"

REM Since the build procedure is the same for x86 and x64 except for the toolchain and deps path
REM Just set the variable for these paths here based on build mode
REM This allows building both 32-bit and 64-bit in a single run
if "%BUILD_FOR_x86%" NEQ "" (
	echo Installing for 32-bit
	
	REM The way the toolchain is installed, the \mingw32 subdirectory will always be created
	set "TOOLCHAIN_BIN_WIN=%TOOLCHAIN_ROOT_WIN%\mingw32\bin"
	set "TOOLCHAIN_BIN_NIX=%TOOLCHAIN_ROOT_NIX%/mingw32/bin"
	set "DEPS_WIN=%DEPS_ROOT_WIN%\x86"
	set "DEPS_NIX=%DEPS_ROOT_NIX%/x86"
	set "TOOLCHAIN_PARAM=--path-toolchain-32"
	
	GOTO BUILD_START
)
REM else fall-through

:BUILD_START_64
REM If 32-bit was built, copy output to different directory
if "%BUILD_FOR_x86%" NEQ "" (
	REM Make sure output directory exists
	mkdir "%BITCOIN_GIT_ROOT_WIN%\build-output\x86\"
	
	REM cd to src to copy bitcoin-tx.exe, bitcoin-cli.exe, and bitcoind.exe
	cd "%BITCOIN_GIT_ROOT_WIN%\src"
	copy bitcoin-tx.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x86\bitcoin-tx.exe"
	copy bitcoin-cli.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x86\bitcoin-cli.exe"
	copy bitcoind.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x86\bitcoind.exe"
	
	REM cd to src\qt to copy bitcoin-qt.exe
	cd qt
	copy bitcoin-qt.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x86\bitcoin-qt.exe"
	
	REM return to install directory
	cd "%INST_DIR%"
	
	REM Clear the x86 build flag so we don't re-enter this if statement
	set BUILD_FOR_x86=
)
REM If 64-bit 
if "%BUILD_FOR_x64%" NEQ "" (
	echo Installing for 64-bit
	
	REM The way the toolchain is installed, the \mingw64 subdirectory will always be created
	set "TOOLCHAIN_BIN_WIN=%TOOLCHAIN_ROOT_WIN%\mingw64\bin"
	set "TOOLCHAIN_BIN_NIX=%TOOLCHAIN_ROOT_NIX%/mingw64/bin"
	set "DEPS_WIN=%DEPS_ROOT_WIN%\x64"
	set "DEPS_NIX=%DEPS_ROOT_NIX%/x64"
	set "TOOLCHAIN_PARAM=--path-toolchain-64"
	
	REM Clear the x64 build flag so we don't re-enter this if statement
	set BUILD_FOR_x64=
	
	REM Flag that we need to copy the 64-bit outputs
	set COPY_x64=TRUE
) else ( GOTO BUILD_END )

:BUILD_START
REM Install toolchain components for this arch (download, unpack, and build)
echo Installing toolchain...
%MSYS_SH_WIN% "%INST_DIR%\install-toolchain.sh" --path-7zip=%CMD_7ZIP% --path-deps="%DEPS_NIX%" --path-msys="%MSYS_BIN_NIX%" %TOOLCHAIN_PARAM%="%TOOLCHAIN_BIN_NIX%" --path-mingw="%MINGW_BIN_NIX%"

REM Install dependencies (and build this arch)
%MSYS_SH_WIN% "%INST_DIR%\install-deps.sh" --path-7zip=%CMD_7ZIP% --path-deps="%DEPS_NIX%" --path-msys="%MSYS_BIN_NIX%" --path-toolchain="%TOOLCHAIN_BIN_NIX%" --path-mingw="%MINGW_BIN_NIX%"

REM Perform build steps that require Windows CMD
set "PATH=%TOOLCHAIN_BIN_WIN%;%OLD_PATH%"

REM Boost
echo Building Boost...
cd "%DEPS_WIN%\boost_1_61_0"
call bootstrap.bat gcc
b2 --build-type=complete --with-chrono --with-filesystem --with-program_options --with-system --with-thread %BOOST_ENABLE_TESTS% toolset=gcc variant=release link=static threading=multi runtime-link=static stage

REM Miniunpuc
echo Building Miniunpuc...
cd "%DEPS_WIN%\miniupnpc"
mingw32-make -f Makefile.mingw init upnpc-static

REM Qt 5
echo Building Qt 5.3.2...
cd "%DEPS_WIN%\Qt\5.3.2"
set "INCLUDE=%DEPS_WIN%\libpng-1.6.16;%DEPS_WIN%\openssl-1.0.1k\include"
set "LIB=%DEPS_WIN%\libpng-1.6.16\.libs;%DEPS_WIN%\openssl-1.0.1k"
call configure.bat -release -opensource -confirm-license -static -make libs -no-sql-sqlite -no-opengl -system-zlib -qt-pcre -no-icu -no-gif -system-libpng -no-libjpeg -no-freetype -no-angle -no-vcproj -openssl -no-dbus -no-audio-backend -no-wmf-backend -no-qml-debug
mingw32-make -j4

echo Building Qt Tools...
set "PATH=%PATH%;%DEPS_WIN%\Qt\5.3.2\bin"
set "PATH=%PATH%;%DEPS_WIN%\Qt\qttools-opensource-src-5.3.2"
cd "%DEPS_WIN%\Qt\qttools-opensource-src-5.3.2"
qmake qttools.pro
mingw32-make -j4

REM ##################################################################################################
REM Time to build Bitcoin
REM ##################################################################################################
echo Building bitcoin...
%MSYS_SH_WIN% "%INST_DIR%\make-bitcoin.sh" --path-bitcoin="%BITCOIN_GIT_ROOT_NIX%" --path-deps="%DEPS_NIX%" --path-msys="%MSYS_BIN_NIX%" --path-toolchain="%TOOLCHAIN_BIN_NIX%" --path-mingw="%MINGW_BIN_NIX%" %INCLUDE_TESTS% --clean

REM head back to the top in case we just built 32-bit, but still need to build 64-bit
GOTO BUILD_START_64

:BUILD_END
REM If 64-bit was built, copy generated exe files to output location
if "%COPY_x64%" NEQ "" (
	REM Make sure output directory exists
	mkdir "%BITCOIN_GIT_ROOT_WIN%\build-output\x64\"
	
	REM cd to src to copy bitcoin-tx.exe, bitcoin-cli.exe, and bitcoind.exe
	cd "%BITCOIN_GIT_ROOT_WIN%\src"
	copy bitcoin-tx.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x64\bitcoin-tx.exe"
	copy bitcoin-cli.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x64\bitcoin-cli.exe"
	copy bitcoind.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x64\bitcoind.exe"
	
	REM cd to src\qt to copy bitcoin-qt.exe
	cd qt
	copy bitcoin-qt.exe "%BITCOIN_GIT_ROOT_WIN%\build-output\x64\bitcoin-qt.exe"
)


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