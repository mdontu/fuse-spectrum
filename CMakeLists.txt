# SPDX-License-Identifier: GPL-2.0
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "Prevented in-tree built. Please use the -B <build-dir> option")
endif()

cmake_minimum_required(VERSION 3.19)

project(fuse-spectrum VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(CMAKE_POSITION_INDEPENDENT_CODE True)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)
endif()

add_library(common_exe_flags INTERFACE)
target_compile_definitions(common_exe_flags INTERFACE
	$<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>
	_FILE_OFFSET_BITS=64 _GNU_SOURCE
)
target_compile_options(common_exe_flags INTERFACE
	$<$<CONFIG:Release>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2>
	-Wall -Wextra -Werror=return-type -fvisibility-inlines-hidden -fvisibility=hidden -ffunction-sections -fdata-sections -fstack-protector-strong -fno-strict-aliasing -fstack-clash-protection -fasynchronous-unwind-tables
)
target_link_options(common_exe_flags INTERFACE -Wl,-z,nodlopen,-z,defs,-z,now,-z,noexecstack,-z,relro,-O,1,--gc-sections,--build-id)

find_program(CCACHE_PROGRAM ccache)
set(CCACHE_SUPPORT ON CACHE BOOL "Enable ccache support")
if(CCACHE_PROGRAM AND CCACHE_SUPPORT)
	set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

find_package(Threads REQUIRED)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/version.h
	@ONLY
)

add_executable(fuse-spectrum src/disk.cpp src/filesystem.cpp src/hcfs.cpp src/dsk.cpp src/imd.cpp src/main.cpp)
target_include_directories(fuse-spectrum PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(fuse-spectrum PRIVATE FUSE_USE_VERSION=30)
target_link_libraries(fuse-spectrum PRIVATE common_exe_flags ${FUSE_LIBRARIES} Threads::Threads)

install(TARGETS fuse-spectrum)
