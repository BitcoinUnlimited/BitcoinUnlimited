@echo off
REM ##################################################################################################
REM You should not need to edit this file.  All user configuration should be completed in the
REM SET_ENV_VARS.bat file.  That is where you should set system paths and build options.
REM ##################################################################################################

REM Ensure any variable changes don't exceed the life of this batch file.
setlocal

REM Remember the path without anything prepended so we can easily switch toolchains
set "OLD_PATH=%PATH%"

REM Capture timing metrics
set START_TIME=%TIME%

REM Set up the environment variables
call SET_ENV_VARS.bat

REM ##################################################################################################
REM Since this is the initial configuration file, we want to make sure certain settings are always
REM configured for a clean build, regardless of what the user put in SET_ENV_VARS.bat
REM  1. Force a clean build
REM  2. Don't skip autogen.sh or configure steps
REM ##################################################################################################
set CLEAN_BUILD=YES
set SKIP_AUTOGEN=
set SKIP_CONFIGURE=

REM TODO: add checking to ensure environment variables are set correctly
REM 1. Required variables set
REM 2. Any set variables are correct
REM 3. All configured paths that are expected to pre-exist, do exist

REM Build necessary paths based on this information
set "INST_DIR=%CD%"
set "MINGW_BIN=%MINGW_ROOT%\bin\"
set MINGW_GET="%MINGW_BIN%\mingw-get.exe"
set "MSYS_BIN=%MINGW_ROOT%\msys\1.0\bin\"
set MSYS_SH="%MSYS_BIN%\sh.exe"
set "TOOL_CHAIN_ROOT=%MINGW_ROOT%"

REM If including tests, set up the configure flag used by Boost to enabled tests
if "%ENABLE_TESTS%" NEQ "" set BOOST_ENABLE_TESTS=--with-test

REM Install required msys base package (provide access to msys sh shell)
echo Updating base MinGW
%MINGW_GET% update
%MINGW_GET% install msys-base-bin

REM Add MSYS bin directory to the start of path so commands are available
set "PATH=%MSYS_BIN%;%PATH%"

REM Install toolchain components for the specified architecture(s) (download and unpack)
echo Installing toolchain...
%MSYS_SH% "%INST_DIR%\install-toolchain.sh"

REM "Installs" utilities (creates wrapper scripts to alias installed python and git)
echo Installing utilities...
%MSYS_SH% "%INST_DIR%\install-utils.sh"


REM Since the build procedure is the same for x86 and x64 except for the toolchain and deps path
REM Just set the variable for these paths here based on build mode
REM This allows building both 32-bit and 64-bit in a single run
if "%BUILD_32_BIT%" NEQ "" (
	echo Installing for 32-bit
	
	REM The way the toolchain is installed, the \mingw32 subdirectory will always be created
	set "TOOLCHAIN_BIN=%TOOL_CHAIN_ROOT%\mingw32\bin"
	set "PATH_DEPS=%DEPS_ROOT%\x86"
	set "BUILD_OUTPUT=%BITCOIN_GIT_ROOT%\build-output\x86"
	
	GOTO BUILD_START
)
REM else fall-through

:BUILD_START_64
if "%HAS_BUILT_64_BIT%" NEQ "" GOTO BUILD_END
if "%BUILD_64_BIT%" NEQ "" (
	echo Installing for 64-bit
	
	REM The way the toolchain is installed, the \mingw64 subdirectory will always be created
	set "TOOLCHAIN_BIN=%TOOL_CHAIN_ROOT%\mingw64\bin"
	set "PATH_DEPS=%DEPS_ROOT%\x64"
	set "BUILD_OUTPUT=%BITCOIN_GIT_ROOT%\build-output\x64"
	
	set HAS_BUILT_64_BIT=TRUE
	
	GOTO BUILD_START
) else ( GOTO BUILD_END )

:BUILD_START
REM Install dependencies (and build this arch)
%MSYS_SH% "%INST_DIR%\install-deps.sh"

REM ##################################################################################################
REM Perform build steps that require Windows CMD
REM ##################################################################################################
setlocal
REM Set PATH with toolchain, but not msys, as this causes compile issues for 64-bit Qt
set "PATH=%TOOLCHAIN_BIN%;%OLD_PATH%"

REM Boost
echo Building Boost...
cd "%PATH_DEPS%\boost_1_61_0"
call bootstrap.bat gcc
b2 --build-type=complete --with-chrono --with-filesystem --with-program_options --with-system --with-thread %BOOST_ENABLE_TESTS% toolset=gcc variant=release link=static threading=multi runtime-link=static stage

REM Miniunpuc
echo Building Miniunpuc...
cd "%PATH_DEPS%\miniupnpc"
mingw32-make -f Makefile.mingw init upnpc-static

REM Qt 5
echo Building Qt 5.3.2...
cd "%PATH_DEPS%\Qt\5.3.2"
set "INCLUDE=%PATH_DEPS%\libpng-1.6.16;%PATH_DEPS%\openssl-1.0.1k\include"
set "LIB=%PATH_DEPS%\libpng-1.6.16\.libs;%PATH_DEPS%\openssl-1.0.1k"
call configure.bat -release -opensource -confirm-license -static -make libs -no-sql-sqlite -no-opengl -system-zlib -qt-pcre -no-icu -no-gif -system-libpng -no-libjpeg -no-freetype -no-angle -no-vcproj -openssl -no-dbus -no-audio-backend -no-wmf-backend -no-qml-debug
mingw32-make %MAKE_CORES%

echo Building Qt Tools...
set "PATH=%PATH%;%PATH_DEPS%\Qt\5.3.2\bin"
set "PATH=%PATH%;%PATH_DEPS%\Qt\qttools-opensource-src-5.3.2"
cd "%PATH_DEPS%\Qt\qttools-opensource-src-5.3.2"
qmake qttools.pro
mingw32-make %MAKE_CORES%

endlocal
REM ##################################################################################################
REM Time to build Bitcoin
REM ##################################################################################################
echo Building bitcoin...
%MSYS_SH% "%INST_DIR%\make-bitcoin.sh"

echo Saving bitcoin executables to %BUILD_OUTPUT%
REM Make sure output directory exists
mkdir "%BUILD_OUTPUT%\"

REM cd to src to copy bitcoin-tx.exe, bitcoin-cli.exe, and bitcoind.exe
cd "%BITCOIN_GIT_ROOT%\src"
copy bitcoin-tx.exe "%BUILD_OUTPUT%\bitcoin-tx.exe"
copy bitcoin-cli.exe "%BUILD_OUTPUT%\bitcoin-cli.exe"
copy bitcoind.exe "%BUILD_OUTPUT%\bitcoind.exe"

REM cd to src\qt to copy bitcoin-qt.exe
cd qt
copy bitcoin-qt.exe "%BUILD_OUTPUT%\bitcoin-qt.exe"

REM Go to the 64-bit build section (in case we are building both 32 and 64 bit)
GOTO BUILD_START_64

:BUILD_END
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

REM Clear any variables that may have been set in this script
endlocal

REM Allow the user to see the final results at the end of file execution.
pause
