@echo off
REM ##################################################################################################
REM You should not need to edit this file.  All user configuration should be completed in the
REM SET_ENV_VARS.bat file.  That is where you should set system paths and build options.
REM ##################################################################################################

REM Ensure any variable changes don't exceed the life of this batch file.
setlocal

REM Capture timing metrics
set START_TIME=%TIME%

REM Set up the environment variables
call SET_ENV_VARS.bat

REM ##################################################################################################
REM If the user has configured to build both 32-bit and 64-bit at the same time, we will be swapping
REM toolchains, so we MUST perform clean rebuilds, regardless of what the user put in SET_ENV_VARS.bat
REM  1. Force a clean build
REM  2. Don't skip autogen.sh or configure steps
REM ##################################################################################################
if "%BUILD_32_BIT%" NEQ "" (
  if "%BUILD_64_BIT%" NEQ "" (
    set CLEAN_BUILD=YES
    set SKIP_AUTOGEN=
    set SKIP_CONFIGURE=
  )
)

REM Build necessary paths based on this information
set "INST_DIR=%CD%"
set "MINGW_BIN=%MINGW_ROOT%\bin\"
set "MSYS_BIN=%MINGW_ROOT%\msys\1.0\bin\"
set MSYS_SH="%MSYS_BIN%\sh.exe"
set "TOOL_CHAIN_ROOT=%MINGW_ROOT%"

REM Verify that the user specified to build at least one of 32 or 64 bit
set BUILD_ARCH=
if "%BUILD_32_BIT%" NEQ "" set BUILD_ARCH=T
if "%BUILD_64_BIT%" NEQ "" set BUILD_ARCH=T
if "%BUILD_ARCH%" NEQ "T" (
	echo You must specify building at least one of 32-bit or 64-bit version
	echo of the Bitcoin client in SET_ENV_VARS.bat.
	echo Aborting initial configuration...
	pause
	exit /b -1
)

REM Verify that base MinGW was installed and correctly specified in SET_ENV_VARS.bat
if not exist "%MINGW_ROOT%" (
	echo MinGW base does not appear to be installed.  Please ensure that you have
	echo executed mingw-setup.exe, updated SET_ENV_VARS.bat to list the install
	echo location you chose in the variable MINGW_ROOT, and run config-mingw.bat.
	echo Current MINGW_ROOT = %MINGW_ROOT%
	echo Aborting build of Bitcoin client...
	pause
	exit /b -1
)

REM Verify that MSYS was correctly installed and updated by previous steps
if not exist "%MSYS_SH%" (
	echo MSYS does not appear to have installed correctly.  Build of Bitcoin client
	echo cannot continue without MSYS being properly installed and config-mingw.bat
	echo having been successfully executed at least once.
	echo Current MSYS_SH = %MSYS_SH%
	echo Aborting build of Bitcoin client...
	pause
	exit /b -1
)

REM Verify that DEPS_ROOT is specified and exists
if not exist "%DEPS_ROOT%" (
	echo The DEPS_ROOT directory could not be created.  Initial configuration
	echo cannot continue without a valid path to download and build dependencies.
	echo Current DEPS_ROOT = %DEPS_ROOT%
	echo Aborting initial configuration...
	pause
	exit /b -1
)


REM Add MSYS and MinGW bin directories to the start of path so commands are available
set "PATH=%MSYS_BIN%;%MINGW_BIN%;%PATH%"
REM Remember the path without the toolchain prepended so we can easily switch toolchains
set "OLD_PATH=%PATH%"

REM Since the build procedure is the same for x86 and x64 except for the toolchain and deps path
REM Just set the variable for these paths here based on build mode
REM This allows building both 32-bit and 64-bit in a single run
if "%BUILD_32_BIT%" NEQ "" (
	echo Building for 32-bit
	
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
	echo Building for 64-bit
	
	REM The way the toolchain is installed, the \mingw64 subdirectory will always be created
	set "TOOLCHAIN_BIN=%TOOL_CHAIN_ROOT%\mingw64\bin"
	set "PATH_DEPS=%DEPS_ROOT%\x64"
	set "BUILD_OUTPUT=%BITCOIN_GIT_ROOT%\build-output\x64"

	set HAS_BUILT_64_BIT=TRUE
	
	GOTO BUILD_START
) else ( GOTO BUILD_END )

:BUILD_START
REM Verify that the current build toolchain exists (by checking for gcc.exe)
if not exist "%TOOLCHAIN_BIN%\gcc.exe" (
	echo The build toolchain does not exist.  Initial configuration cannot
	echo continue without a valid build toolchain.  This may indicate that
	echo you have not run config-mingw.bat for the current architecture yet.
	echo Current TOOLCHAIN_BIN = %TOOLCHAIN_BIN%
	echo Aborting initial configuration...
	pause
	exit /b -1
)

REM Set the path variable to contain the toolchain as well as MSYS bin directories
set "PATH=%TOOLCHAIN_BIN%;%OLD_PATH%"

%MSYS_SH% "%INST_DIR%\make-bitcoin.sh"
REM Check to see if make-bitcoin.sh failed (possibly due to missing dependencies)
if %errorlevel% neq 0 (
	REM Assume that whatever caused the error also wrote an output so we
	REM don't need to write an output here
	pause
	exit /b %errorlevel%
)

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
