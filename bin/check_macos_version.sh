#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# mrv2
# Copyright Contributors to the mrv2 Project. All rights reserved.



otool -l $1 | grep -E -A4 '(LC_VERSION_MIN_MACOSX|LC_BUILD_VERSION)' | grep -B1 sdk
