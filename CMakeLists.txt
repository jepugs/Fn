# I'm using the version from the current Debian stable. I figure that's old
# enough to run on pretty much anything.
cmake_minimum_required(VERSION 3.23)

# Adding this LANGUAGES option ensures that CMake searches for c++, not cc. This
# fixes a problem I discovered on a MacBook that had g++ for c++, but an old
# version of clang for cc.
project(Fn VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

string(CONCAT CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++20 -Wall -Wpedantic")

# set(CMAKE_CXX_FLAGS_DEBUG "-fsanitize=address ${CMAKE_CXX_FLAGS_DEBUG}")
# set(CMAKE_LINKER_FLAGS_DEBUG "-fsanitize=address ${CMAKE_LINKER_FLAGS_DEBUG}")

set(PREFIX "/usr" CACHE PATH "Installation prefix")

configure_file("config.h.in" "config.h")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(src)

# install the standard library
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/pkg
  DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/fn)

# this if also prevents tests from building when this project is included
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
  include(CTest)
  add_subdirectory(test)
endif()

