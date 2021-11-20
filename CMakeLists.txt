cmake_minimum_required(VERSION 3.10)

project(Fn VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "-O0 -Wall")
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  set(CMAKE_CXX_FLAGS "-Os -Wall")
else()
  set(CMAKE_CXX_FLAGS "-O2 -Wall")
endif()

set(PREFIX "/usr" CACHE PATH "Installation prefix")

configure_file("config.h.in" "config.h")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(src)

# this if also prevents tests from building when this project is included
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
  include(CTest)
  add_subdirectory(test)
endif()

