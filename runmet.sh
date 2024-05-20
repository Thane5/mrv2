#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# mrv2
# Copyright Contributors to the mrv2 Project. All rights reserved.



#
#
# Wrap up script to compile FLTK, tlRender and mrv2
#
#



. $PWD/etc/build_dir.sh


dir=$BUILD_DIR/tlRender/etc/SuperBuild/tlRender/src/tlRender-build/
if [[ ! -d $dir ]]; then
    echo "tlRender directory:"
    echo $dir
    echo "does not exist. Please run:"
    echo " $ runme.sh [sameflags]"
    exit 1
fi

cd $dir

#
#  Rebuild tlRender
#
cmake --build . $FLAGS --config $CMAKE_BUILD_TYPE -t install

cd -

dir=$BUILD_DIR/FLTK-prefix/src/FLTK-build/
if [[ ! -d $dir ]]; then
    echo "FLTK directory"
    echo $dir
    echo "does not exist. Please run:"
    echo " $ runme.sh [sameflags]"
    exit 1
fi

cd $dir

#
#  Rebuild FLTK
#
cmake --build . $FLAGS --config $CMAKE_BUILD_TYPE -t install

cd -

dir=$BUILD_DIR/mrv2/src/mrv2-build

# Needed to to force a relink and update build info.
rm -f $dir/src/mrv2

if [[ "$CMAKE_TARGET" == "" ]]; then
    CMAKE_TARGET=install
fi

run_cmd ./runmeq.sh $CMAKE_BUILD_TYPE -t $CMAKE_TARGET

