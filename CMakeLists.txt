cmake_minimum_required(VERSION 3.10)

project(Fn VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "-O0 -Wall")

add_subdirectory(src)

# this if also prevents tests from building when this project is included
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
  include(CTest)
  add_subdirectory(test)
endif()

