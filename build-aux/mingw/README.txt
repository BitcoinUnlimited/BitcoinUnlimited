This document describes the full set of steps in order to install the base Bitcoin development environment on the Windows operating system using MinGW.  Please complete the configuration steps listed below, then run the modified batch file to perform the rest of the installation.


Configuration Steps
-------------------

1. Download and install the MinGW base installation.  This may be downloaded from http://sourceforge.net/projects/mingw/files/Installer/mingw-get-setup.exe/download
2. You do not need to set anything outside of the default for this installation.  Once base installation is complete you may exit the installer without installing any additional packages.

3. Ensure you have the 7-Zip client installed on your system.  You will need the installation path for the subsequent steps.  By default this is typically under C:\Program Files\7-Zip\7z.exe or C:\Program Files (x86)\7-Zip\7z.exe, depending on if you have the 64-bit or 32-bit version installed.

4. Ensure that the Bitcoin source code has been downloaded.  You can download using a git client by connecting to https://github.com/BitcoinUnlimited/BitcoinUnlimited.git  This will download the latest development source for you to build.

5. Update the path specification section at the top of the config-mingw.bat file as follows.  Please note that all paths must be absolute (relative paths will break the scripts).
  a. Set the MinGW root installation location.  There are two parameters here, one for defining the path in linux format and one for defining the path in Windows format.  Both must be specified.
  b. Set the path to your 7-Zip executable.  This should be specified in linux format and include the executable name.
  c. Set the root install location for the MinGW-builds project toolchain.  This is the location where the toolchain will be installed to by the scripts.  Must be defined in both linux and windows formats.
  d. Set the root dependencies path.  This is the location where all external dependencies required to build Bitcoin will be downloaded, extracted, and built.  Must be defined in both linux and windows formats.
  e. Set the root path for your local Bitcoin repository.  This must be specified in linux format.

Once the paths have been updated, save the changes to config-mingw.bat


Execution Steps
----------------

6. Double-click on config-mingw.bat.  This will start the process of downloading and installing all of the needed dependnencies followed by building Bitcoin.  Be patient, installing on a clean system will take more than 30 minutes.

7. Once config-mingw.bat completes successfully, you should be able to run the bitcoin-qt.exe client from .\src\qt\bitcoin-qt.exe within your Bitcoin repository location.


