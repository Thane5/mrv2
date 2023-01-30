#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# mrv2
# Copyright Contributors to the mrv2 Project. All rights reserved.




#
# This is needed on macOS which spits out linking errors otherwise
#
if [[ $KERNEL == *Darwin* ]]; then
    rm -f $BUILD_DIR/install/bin/mrv2
fi

dir=$BUILD_DIR/mrv2/src/mrv2-build

if [[ ! -d $dir ]]; then
    echo "mrv2 build directory does not exist."
    echo "Please run:"
    echo " $ runme.sh [sameflags]"
    exit 1
fi


cd $dir

cmake --build . $FLAGS --config $CMAKE_BUILD_TYPE -t install

cd -

