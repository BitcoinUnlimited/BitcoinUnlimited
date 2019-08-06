#!/bin/bash
# script modified from: https://gist.github.com/enh/b2dc8e2cbbce7fffffde2135271b10fd

version=1.70.0
echo "Building boost $version..."

set -eu

toolchain=$PWD/toolchain
if [ ! -d "$toolchain" ]; then
  echo "Building toolchain..."
  $ANDROID_NDK_ROOT/build/tools/make-standalone-toolchain.sh \
      --arch=arm --platform=android-21 \
      --install-dir="$toolchain" \
      --toolchain=arm-linux-androideabi-clang \
      --use-llvm --stl=libc++
else
  echo "Toolchain already built"
fi

dir_name=boost_$(sed 's#\.#_#g' <<< $version)
archive=${dir_name}.tar.bz2
if [ ! -f "$archive" ]; then
  wget -O $archive "https://dl.bintray.com/boostorg/release/$version/source/$archive"
else
  echo "Archive $archive already downloaded"
fi

echo "Extracting..."
if [ ! -d "$dir_name" ]; then
  # rm -rf $dir_name
  tar xf $archive
else
  echo "Archive $archive already unpacked into $dir_name"
fi
cd $dir_name

echo "Generating config..."
user_config=tools/build/src/user-config.jam
rm -f $user_config
cat <<EOF > $user_config
import os ;

using clang : android
:
"$toolchain/bin/clang++"
:
<cxxflags>-std=c++14
<archiver>$toolchain/bin/arm-linux-androideabi-ar
<ranlib>$toolchain/bin/arm-linux-androideabi-ranlib
;
EOF

echo "Bootstrapping..."
./bootstrap.sh #--with-toolset=clang

echo "Building..."
./b2 -j$(nproc) \
    --with-atomic \
    --with-chrono \
    --with-container \
    --with-date_time \
    --with-exception \
    --with-fiber \
    --with-filesystem \
    --with-graph \
    --with-graph_parallel \
    --with-iostreams \
    --with-locale \
    --with-log \
    --with-math \
    --with-mpi \
    --with-program_options \
    --with-random \
    --with-regex \
    --with-serialization \
    --with-system \
    --with-test \
    --with-thread \
    --with-timer \
    --with-type_erasure \
    --with-wave \
    toolset=clang-android \
    architecture=arm \
    variant=release \
    --layout=versioned \
    target-os=android \
    threading=multi \
    threadapi=pthread \
    link=static \
    runtime-link=static \

echo "Running ranlib on libraries..."
libs=$(find "bin.v2/libs/" -name '*.a')
for lib in $libs; do
  "$toolchain/bin/arm-linux-androideabi-ranlib" "$lib"
done

echo "Done!"
