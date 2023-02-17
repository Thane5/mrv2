#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# mrv2
# Copyright Contributors to the mrv2 Project. All rights reserved.

#
#
# Main build script for mrv2 using Docker.
# We build binaries on a Rocky Linux 8 image.
# We build the latest tag released by default.  If you want to build the
# current HEAD change the Dockerfile.
#

#
# Build the image
#
docker build -t mrv2_builder .

#
# Run the compile and package extraction
#
docker run -v ${PWD}/packages:/packages \
       --name mrv2_build_$(date "+%s") \
       mrv2_builder
