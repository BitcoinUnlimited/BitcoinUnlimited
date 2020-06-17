# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.10.2)
set(CMAKE_VERBOSE_MAKEFILE ON)
# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_definitions(-Wno-tautological-constant-compare -DBOOST_SYSTEM_NO_DEPRECATED)

add_library( # Sets the name of the library.
             bitcoincashandroid

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             cashlib.cpp
             ../script/interpreter.cpp
             ../script/script.cpp
             ../script/script.h
             ../primitives/transaction.cpp
             ../primitives/block.cpp
             ../primitives/block.h
             ../support/pagelocker.cpp
             ../support/cleanse.cpp
             ../script/sigcommon.cpp
             ../script/bitfield.cpp
             ../script/bitfield.h
             ../crypto/sha256.cpp
             ../crypto/hmac_sha512.cpp
             ../crypto/hmac_sha512.h
             ../crypto/sha512.cpp
             ../crypto/sha512.h
             ../crypto/ripemd160.cpp
             ../crypto/ripemd160.h
             ../crypto/sha1.cpp
             ../crypto/sha1.h
             ../hashwrapper.cpp
             ../hashwrapper.h
             ../key.cpp
             ../key.h
             ../uint256.cpp
             ../uint256.h
             ../utilstrencodings.cpp
             ../utilstrencodings.h
             ../cashaddrenc.cpp
             ../cashaddrenc.h
             ../cashaddr.cpp
             ../cashaddr.h
             ../pubkey.cpp
             ../pubkey.h
             ../secp256k1/src/secp256k1.c
             ../chainparamsbase.cpp
             ../chainparams.cpp
             ../chainparams.h
             ../consensus/merkle.cpp
             ../arith_uint256.cpp
             ../arith_uint256.h
             ../bloom.cpp
             ../bloom.h
             ../merkleblock.cpp
             ../merkleblock.h
             ../streams.h
             ../rsm/include/recursive_shared_mutex.h
             )


SET(OPT_SELECTION "-DHAVE_CONFIG_H -DVERIFY")
SET_SOURCE_FILES_PROPERTIES(../secp256k1/src/secp256k1.c PROPERTIES COMPILE_FLAGS ${OPT_SELECTION} )

# broken but should work
# set(Boost_USE_STATIC_LIBS OFF)
# set(Boost_USE_MULTITHREADED ON)
# set(Boost_USE_STATIC_RUNTIME OFF)
# set(Boost_DEBUG ON)
# set(Boost_NO_BOOST_CMAKE ON)
#set(BOOST_ROOT /fast/bitcoin/boostAndroid/boost_1_67_0)
#or maybe
#set(BOOST_ROOT /fast/bitcoin/boostAndroid/build/out/${ANDROID_ABI})
# set(BOOST_INCLUDEDIR /fast/bitcoin/boostAndroid/boost_1_67_0/boost)

#find_package(Boost 1.67.0 REQUIRED
#  COMPONENTS system
#  )
# HINTS /fast/bitcoin/boostAndroid/boost_1_67_0/boost
#)

# We expect that you have built boost in the cashlib subdirectory.  Run the ./buildBoostAndroid.sh script in the cashlib directory to do this.
set( Boost_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/boost_1_70_0/stage/lib)
set( Boost_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/boost_1_70_0)

# No boost libraries are currently needed (just the headers)
# file(GLOB F1 ${Boost_LIB_DIR}/libboost_thread-clang-mt-*-1_70.a)
# file(GLOB F2 ${Boost_LIB_DIR}/libboost_system-clang-mt-*-1_70.a)
#set( Boost_LIBRARIES ${F1} ${F2} )
set( Boost_LIBRARIES )

include_directories( .
  ..
  ../rsm/include
  ../script
  ../secp256k1/include
  ../secp256k1
  ../secp256k1/src
  ${Boost_INCLUDE_DIRS}
  )

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

set_target_properties(bitcoincashandroid PROPERTIES LINK_FLAGS -L/fast/bitcoin/boostAndroid/build/out/${ANDROID_ABI}/lib)

target_link_libraries( # Specifies the target library.
                       bitcoincashandroid
                       # Links the target library to these libraries
                       android  # base android library
                       log  # android log library
                       ${Boost_LIBRARIES}
                       )
