# SPDX-License-Identifier: GPL-2.0
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "Prevented in-tree built. Please use the -B <build-dir> option")
endif()

cmake_minimum_required(VERSION 3.19)

project(fuse-spectrum)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(CMAKE_POSITION_INDEPENDENT_CODE True)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-Og -D_DEBUG -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,defs,-z,now,-z,noexecstack,-z,relro,-O,1,--gc-sections,--build-id")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,defs,-z,now,-z,noexecstack,-z,relro,-O,1,--gc-sections,--build-id")

find_program(CCACHE_FOUND ccache)
set(CCACHE_SUPPORT ON CACHE BOOL "Enable ccache support")
if(CCACHE_FOUND AND CCACHE_SUPPORT)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif()

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3)

add_executable(fuse-spectrum src/disk.cpp src/filesystem.cpp src/hcfs.cpp src/dsk.cpp src/imd.cpp src/main.cpp)
target_compile_definitions(fuse-spectrum PRIVATE -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=30 -D_GNU_SOURCE)
target_compile_options(fuse-spectrum PRIVATE -Wall -Wextra -Werror=return-type -ggdb -fvisibility-inlines-hidden -fvisibility=hidden -ffunction-sections -fdata-sections -fstack-protector-strong -fno-strict-aliasing -fstack-clash-protection -fasynchronous-unwind-tables -pipe)
target_link_libraries(fuse-spectrum PRIVATE ${FUSE_LIBRARIES})

install(TARGETS fuse-spectrum)
