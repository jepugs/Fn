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

target_link_libraries(fn fn_lib)

install(TARGETS fn DESTINATION bin)
