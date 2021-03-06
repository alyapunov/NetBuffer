cmake_minimum_required(VERSION 3.5)
project(NetBuffer)

SET(DEFAULT_BUILD_TYPE "Debug")
IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
ENDIF()
SET(EXPECTED_BUILD_TYPES Debug Release RelWithDebInfo MinSizeRel)
IF("${CMAKE_BUILD_TYPE}" IN_LIST EXPECTED_BUILD_TYPES)
    MESSAGE(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
ELSE()
    MESSAGE(WARNING "Unexcpected build type: ${CMAKE_BUILD_TYPE}")
ENDIF()

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_C_STANDARD 11)
ADD_COMPILE_OPTIONS(-Wall -Wextra -Wpedantic -Werror)

include_directories(. ./MemPool ./MemPool/Lists)
add_subdirectory(./MemPool)

set(SOURCE_FILES main.cpp)
add_executable(a.out ${SOURCE_FILES})

enable_testing()
add_test(NAME a.out COMMAND a.out)
