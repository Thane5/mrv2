#!/bin/bash

. build_dir.sh

cd $BUILD_DIR

cmake ../.. -G Ninja -D CMAKE_VERBOSE_MAKEFILE=ON -D CMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE -D CMAKE_INSTALL_PREFIX=$PWD/install -D CMAKE_PREFIX_PATH=$PWD/install -D TLRENDER_PROGRAMS=OFF -D TLRENDER_EXAMPLES=FALSE -D TLRENDER_TESTS=FALSE -D TLRENDER_QT6=OFF -D TLRENDER_QT5=OFF -D LIBINTL_ROOT=${LIBINTL_ROOT}

cmake --build . $FLAGS --config $CMAKE_BUILD_TYPE

cd -

. build_end.sh
