Building Bitcoin on Native Windows (Semi-Automated Install & Configuration)
===========================================================================

This document describes how to use the included scripts to perform a semi-
automated installation & configuration of the full Bitcoin development
environment on Native Windows using MinGW from scratch.  These scripts simply
automate most of the steps found in [build-windows-mingw.md](/doc/build-windows-mingw.md).


Quick Summary
-------------

#### Initial Development Environment Installation

1. Manually install a git client.
2. Clone the Bitcoin Unlimited repository.
3. Manually install 7-zip.
4. Manually install MinGW base package.
5. Configure `/build-aux/mingw/SET_ENV_VARS.bat` to match your local system
   install paths.
6. Execute `/build-aux/mingw/config-mingw.bat` to install the initial development
   environment.


#### (Re)Build Bitcoin

1. If necessary, modify configuration of `/build-aux/mingw/SET_ENV_VARS.bat`
2. Execute `/build-aux/mingw/rebuild-bitcoin.bat` to build bitcoin from source.
3. Output of a successful build will be placed in `/build-output/`


Detailed Instructions
---------------------

#### 1. Install a Git Client

In order to do development work and submit Pull Requests, you will need a git
client.  There are any number of different clients out there, but for the
purposes of this guide, we will use the GitHub Desktop Client.  This is a
no-frills client which has a limited UI and grants you access to the command-
line.

[GitHub Desktop Client Download Page](https://desktop.github.com/)


#### 2. Clone the Bitcoin Unlimited Repository

Once git is installed, you should clone the Bitcoin Unlimited repository.  This
will download the latest source files, including the configuration file which
you need to customize to your local system settings.

1. Open Git Shell (this should have been installed as part of GitHub Desktop)
2. Switch to the directory where you want your copy of the source code to live.
   If the directory doesn't already exist, first create it.
	
	`mkdir C:\BitcoinUnlimited\`
	
	`cd C:\BitcoinUnlimited\`
	
3. Clone the Bitcoin Unlimited repository with the following command:

	`git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git .`
	
4. See section 5.2 below for setting the git path in the configuration file.

	
#### 3. Install 7-zip

Several of the dependency packages come in 7-zip format archives so a version of
7-zip needs to be installed on your system.  For our purposes it doesn't matter
if the installed version is 32-bit or 64-bit.

[7-Zip Download Page](http://www.7-zip.org/)

Once installed, note the path to the `7z.exe` file.
By default this path should be:

64-bit: `C:\Program Files\7-Zip\7z.exe`

or

32-bit: `C:\Program Files (x86)\7-Zip\7z.exe`

	
See section 5.3 below for configuring the 7-zip path in the configuration file.


#### 4. Install MinGW Base Configuration

You need to manually download and install the MinGW base configuration.

[MinGW Download Page](http://sourceforge.net/projects/mingw/files/Installer/)


1. Download the installer, mingw-get-setup.exe
2. Execute the installer, choosing default options.
3. Once installation completes, you do not need to download any packages, simply
   close the installer.
4. See section 5.4 below for configuring the MinGW Base path in the
   configuration file.


#### 5. Open Environment Configuration File

Now that the Bitcoin Unlimited source code is downloaded, you should open the
configuration file in a text editor such that you may begin customizing to match
your local development environment settings.  There are several settings you may
customize, as described below.

NOTE: The `SET_ENV_VARS.bat` file uses MS-DOS style comments.  Any line that
      begins with `REM` is considered a comment line.  Each of the configurable
	  parameters in this file have a commented out example.  You may simply
	  uncomment these lines by deleting the `REM ` and modifying the path to match
	  your local system settings.

1. Open `/build-aux/mingw/SET_ENV_VARS.bat` in a text editor.
2. You may set the `GIT_EXE` path to match the install location on your system.
   If you installed GitHub Desktop, then you may find the `git.exe` file in a path
   similar to the one given as an example in the config file.  By setting this
   path, the configuration scripts will automatically create an alias to the
   git executable so you don't need to manually mess with any path variables and
   can execute git commands from the MinGW command line without further
   configuration.
3. Set the `CMD_7ZIP` path to match the path you installed 7-Zip in section 3
   above.  **This path is required to be correctly set for the scripts to work.**
4. Set the `MINGW_ROOT` path to match the path you installed the MinGW base
   configuration in section 4 above.  **This path is required to be correctly set
   for the scripts to work.**
5. Set the `DEPS_ROOT` path.  This is the path where all dependencies needed to
   build bitcoin will be downloaded and built.  Within this path, an x86 and/or
   an x64 sub-directory will be created depending on if you are building 32-bit,
   64-bit, or both clients.  **This path is required to be correctly set for the
   scripts to work.**
6. NOTE: The `BITCOIN_GIT_ROOT` variable should only be modified if you have the
   build scripts located outside of the git repository download for some reason.
   This variable should point to the root path of the source.
7. `BUILD_32_BIT` is commented out by default.  If you wish to build 32-bit
   versions of the Bitcoin executables, uncomment this line.
8. `BUILD_64_BIT` is enabled by default.  If you do not wish to build 64-bit
   executables then comment out this line.
9. `MAKE_CORES` will improve build speed by compiling multiple object files in
   parallel.  It is recommended that you set this to "-jN" where N is the number
   of logical cores your processor has available.
10. `ENABLE_TESTS` is disabled by default.  As the tests do not currently run
   when built natively under Windows, it is recommended to leave this off.
   NOTE: If for some reason you do want to build with tests enabled, be sure
   you run a full rebuild of all dependencies, as well as the Bitcoin client, as
   some of the dependencies require extra compiler options to enable tests.
11. `CLEAN_BUILD` is disabled by default.  This will force a `make clean` prior
   to build of the Bitcoin client.
   NOTE: This only affects the build of the Bitcoin client, it does not affect
   the build of dependencies.
12. `STRIP` is enabled by default.  This will strip out debugging symbols from
   the generated executables, greatly reducing the file size.
13. `SKIP_AUTOGEN` is disabled by default.  This is a convenience feature that
   should only be used when rebuilding Bitcoin, not while doing the initial
   development environment configuration.  This is merely a convenience feature
   for when you know you will be rebuilding without any changes to autogen file
   settings.  **If in doubt, leave this commented out.**
14. `SKIP_CONFIGURE` is disabled by default.  This is a convenience feature that
   should only be used when rebuilding Bitcoin, not while doing the initial
   development environment configuration.  This is merely a convenience feature
   for when you know you will be rebuilding without needing to re-evaluate the
   configuration settings for make.  **If in doubt, leave this commented out.**


#### Note About Building 32-bit and 64-bit Versions

The development environment supports installing both 32-bit and 64-bit versions
of the toolchain and dependencies, allowing you to build the statically linked
Bitcoin binaries as either 32-bit or 64-bit.  The configuration scripts allow
you to build just 32-bit, just 64-bit, or both 32 and 64 bit at a single go.
This is controlled via the `BUILD_32_BIT` and `BUILD_64_BIT` options.

One thing to note is that in order to build 32-bit or 64-bit, the 
`config-mingw.bat` file must have been executed with the corresponding
`BUILD_XX_BIT` option enabled, otherwise the correct toolchain and/or
dependencies will not have been installed/built.


#### 6. Configure the Development Environment

At this point all pre-requisites should be installed and the configuration file
set up to match your local system.  The remainder of the installation will be
completely automated.

1. Double-click on `config-mingw.bat`.

The installation process will take quite a bit of time, depending on the options
chosen in the configuration file.  Once installation completes, you should have
all of your dependencies download and built in your `DEPS_ROOT` folder.  32-bit
binaries will reside under x86 sub-folder and 64-bit binaries will reside under
the x64 sub-folder.

The initial configuration process is also set up to build the Bitcoin client
upon completion of the development environment setup.  All build outputs will
be placed under `/build-output` with 32-bit binaries under the x86 sub-folder
and 64-bit binaries under the x64 sub-folder.

