# SPDX-License-Identifier: BSD-3-Clause
# mrv2 
# Copyright Contributors to the mrv2 Project. All rights reserved.

include( ExternalProject )

# The cutting EDGE!
#set( FLTK_GIT_TAG master )

# Latest Stable Wayland (moving, resizing and subwindows okay)
set(FLTK_GIT_TAG ecb3c9c6fca1af050b80f6ea94e03852bd51765b)

if(MRV2_PYFLTK OR FLTK_BUILD_SHARED)
    # If we are building pyFLTK compile shared
    set( FLTK_BUILD_SHARED_LIBS ON )  
else()
    # else compile static
    set( FLTK_BUILD_SHARED_LIBS OFF ) 
endif()

set( FLTK_BUILD_TYPE ${CMAKE_BUILD_TYPE} )

set( FLTK_CXX_COMPILER ${CMAKE_CXX_COMPILER})
set( FLTK_CXX_FLAGS ${CMAKE_CXX_FLAGS} )

set( FLTK_C_COMPILER ${CMAKE_C_COMPILER})
set( FLTK_C_FLAGS ${CMAKE_C_FLAGS} )

if(WIN32)
    set(FLTK_C_COMPILER cl)
    set(FLTK_CXX_COMPILER cl)
elseif(UNIX AND NOT APPLE)
    list(APPEND FLTK_C_FLAGS -fPIC)
    list(APPEND FLTK_CXX_FLAGS -fPIC)
endif()

# These two are always built by tlRender
set(FLTK_USE_SYSTEM_ZLIB TRUE)
set(FLTK_USE_SYSTEM_LIBPNG TRUE)

# This one may be turned off
set(FLTK_USE_SYSTEM_LIBJPEG FALSE)
if(TLRENDER_JPEG)
    set(FLTK_USE_SYSTEM_LIBJPEG TRUE)
endif()

set(FLTK_PATCH )
    
if (APPLE OR WIN32)
    set(TLRENDER_X11 OFF)
    set(TLRENDER_WAYLAND OFF )
    set(FLTK_PANGO   OFF )
endif()
    
ExternalProject_Add(
    FLTK
    GIT_REPOSITORY "https://github.com/fltk/fltk.git"
    GIT_TAG ${FLTK_GIT_TAG}
    DEPENDS tlRender
    PATCH_COMMAND ${FLTK_PATCH}
    CMAKE_ARGS
    -DCMAKE_C_COMPILER=${FLTK_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${FLTK_CXX_COMPILER}
    -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHIECTURES}
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    -DCMAKE_VERBOSE_MAKEFILE=${CMAKE_VERBOSE_MAKEFILE}
    -DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}
    -DCMAKE_BUILD_TYPE=${FLTK_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    -DCMAKE_C_FLAGS=${FLTK_C_FLAGS}
    -DCMAKE_CXX_FLAGS=${FLTK_CXX_FLAGS}
    -DCMAKE_INSTALL_MESSAGE=${CMAKE_INSTALL_MESSAGE}
    -DFLTK_BUILD_EXAMPLES=OFF
    -DFLTK_BUILD_FLTK_OPTIONS=OFF
    -DFLTK_BUILD_HTML_DOCS=OFF
    -DFLTK_BUILD_TEST=OFF
    -DFLTK_BUILD_SHARED_LIBS=${FLTK_BUILD_SHARED_LIBS}
    -DFLTK_BACKEND_WAYLAND=${TLRENDER_WAYLAND}
    -DFLTK_BACKEND_X11=${TLRENDER_X11}
    -DFLTK_USE_SYSTEM_LIBDECOR=0
    -DFLTK_USE_SYSTEM_ZLIB=${FLTK_USE_SYSTEM_ZLIB}
    -DFLTK_USE_SYSTEM_LIBJPEG=${FLTK_USE_SYSTEM_LIBJPEG}
    -DFLTK_USE_SYSTEM_LIBPNG=${FLTK_USE_SYSTEM_LIBPNG}
    -DFLTK_USE_PANGO=${FLTK_PANGO}
)

set(FLTK_DEP FLTK)
