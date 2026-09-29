cmake_minimum_required(VERSION 3.20)

project(mining_dds_hands_on LANGUAGES C CXX)

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

option(BUILD_CONNEXT_PRO_APPS "Build Connext Professional 7.7.0 applications" ON)
option(BUILD_CONNEXT_MICRO_APPS "Build the Connext Micro 4.3.0 application" ON)


list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/cmake"
)

if(BUILD_CONNEXT_PRO_APPS)
	add_subdirectory(src/pro)
endif()

if(BUILD_CONNEXT_MICRO_APPS)
	add_subdirectory(src/micro)
endif()
