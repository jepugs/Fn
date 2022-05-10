# this defines the fn_asm library 
# FIXME: add a test to make sure we're on the right platform
add_subdirectory(platforms/x86_64)

add_library(fn_lib OBJECT
  api.cpp
  alloc.cpp
  base.cpp
  builtin.cpp
  compile.cpp
  gc.cpp
  istate.cpp
  obj.cpp
  namespace.cpp
  parse.cpp
  scan.cpp
  values.cpp
  vector.cpp
  vm.cpp
  )

target_include_directories(fn_lib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  )

add_executable(fn main.cpp)

target_link_libraries(fn fn_lib asm_routines)

install(TARGETS fn DESTINATION bin)
