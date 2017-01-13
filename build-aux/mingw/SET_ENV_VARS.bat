@echo off

REM ##################################################################################################
REM These paths need to be customized to match your system.
REM All paths should be specified in Windows format (i.e. C:\Windows\system\)
REM Key things that need to be modfied are:
REM    1. Install directory of the mingw-get-setup.exe (you should have manually installed this)
REM    2. Full path to the 7z.exe file (you should have manually installed this)
REM    3. Drive letter for other directories, if not C: (otherwise the defaults should be fine)
REM
REM    NOTE: It is assumed you are running this configure/build script from the check-out location
REM          of your bitcoin source under ".\build-aux\mingw\".  If this is true, you do not need
REM          to modify the BITCOIN_GIT_ROOT path.  Otherwise update this with the absolute path to
REM          the root folder of your checkout.
REM
REM    IMPORTANT: It is HIGHLY RECOMMENDED you do not use paths with spaces in them.
REM               While these scripts were written to be able to handle spaces, other scripts
REM               called by these scripts may not be.  This is a confirmed issue with the location
REM               of MinGW, MSYS, the toolchains, and several of the dependency libraries.
REM ##################################################################################################

REM Set MinGW root installation path
REM This should be where you installed mingw-get-setup.exe
REM IMPORTANT: DO NOT USE PATHS WITH SPACES for the MinGW root!  If you do, you WILL run into build errors
set "MINGW_ROOT=C:\MinGW"

REM Set 7-Zip installation path
REM This should be where you installed 7-Zip
set CMD_7ZIP="C:\Program Files\7-Zip\7z.exe"

REM Set the dependency path.  This is where all of the Bitcoin dependencies will be downloaded and built
REM IMPORTANT: DO NOT USE PATHS WITH SPACES for the dependencies root!  If you do, you WILL run into build errors
set "DEPS_ROOT=C:\deps"

REM Set the path to your Bitcoin git checkout.
REM NOTE: If you are running these scripts from the bitcoin checkout location, you do not need to modify this
set "BITCOIN_GIT_ROOT=%CD%\..\..\"


REM ##################################################################################################
REM These parameters configure how the bitcoin client and dependencies are built.
REM
REM The default configuration is:
REM    1. 32-bit disabled
REM    2. 64-bit enabled
REM    3. Build cores disabled (means make without the -jN parameter)
REM    4. Tests disabled
REM    5. Clean disabled
REM    6. Strip enabled
REM    7. Skip Bitcoin autogen.sh step disabled (useful for rebuilding local changes only)
REM    8. Skip Bitcoin configure step disbled (useful for rebuilding local changes only)
REM
REM NOTE: If you set to build both 32-bit and 64-bit at the same time:
REM    1. Clean will be enabled
REM    2. Skip autogen.sh will be disabled
REM    3. Skip configure will be disabled
REM ##################################################################################################

REM Following will set up the tool-chain and build dependencies with 32-bit outputs
REM If you want to perform 32-bit builds, uncomment the line below.
REM SET BUILD_32_BIT=YES

REM Following will set up the tool-chain and build dependencies with 64-bit outputs
REM If you don't want to perform 64-bit builds, comment out the line below.
SET BUILD_64_BIT=YES

REM Following will set the number of cores used to build the binaries
REM NOTE: Rule of thumb is to set the number of cores to the number of CPU cores available.
REM       If you run into issues with a higher -jN try reducing the number or eliminating
REM       the parameter altogether.
REM SET "MAKE_CORES=-j4"

REM If you want to run tests ("make check" and "rpc-tests" you must uncomment below line).
REM NOTE: Many of the RPC tests will not run on Windows due to a depencency not available in Windows
REM SET ENABLE_TESTS=YES

REM If you want to remove any previous build outputs and configurations uncomment the line below.
REM NOTE: If you are switching between the 32-bit and 64-bit tool chains, you should clean old
REM       build outputs with this switch, otherwise you may run into linker issues.
REM SET CLEAN_BUILD=YES

REM Following will strip debug symbols from the generated bitcoin executables, greatly reducing file size.
REM This will, however, make it more difficult to debug any issues that may occur while testing.
REM If you want to keep debug symbols in the generated bitcoin executables, uncomment the line below.
SET STRIP=YES

REM If you want to skip running "./autogen.sh" when building bitcoin uncomment the line below.
REM This is useful to turn off if you have already run this once and not changed the toolchain
REM or autogen configuration files, as it eleminates a redundant build step.
REM NOTE: This only affects the build of the bitcoin executables, not any of the dependencies.
REM       Additionally, this does not perform a check to see if autogen.sh has been run before
REM       so if you turn this on but have not previously run autogen.sh you will run into errors.
REM SET SKIP_AUTOGEN=YES

REM If you want to skip running "./configure" when building bitcoin uncomment the line below.
REM This is useful to turn off if you have already run this once and not changed the toolchain
REM or build configuration, as it eleminates a redundant build step.
REM NOTE: This only affects the build of the bitcoin executables, not any of the dependencies.
REM       Additionally, this does not perform a check to see if configure has been run before
REM       so if you turn this on but have not previously run configure you will run into errors.
REM
REM       If this setting is turned on, then SKIP_AUTOGEN is also automatically turned on too.
REM SET SKIP_CONFIGURE=YES
