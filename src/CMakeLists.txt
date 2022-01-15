add_library(fn_lib OBJECT
  allocator.cpp
  base.cpp
  #compile.cpp
  expand.cpp
  #ffi/builtin.cpp
  ffi/fn_handle.cpp
  #interpret.cpp
  llir.cpp
  #log.cpp
  obj.cpp
  namespace.cpp
  parse.cpp
  scan.cpp
  values.cpp
  #vm.cpp
  )

target_include_directories(fn_lib PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  )

add_executable(fn main.cpp)

target_link_libraries(fn fn_lib)

install(TARGETS fn DESTINATION bin)
