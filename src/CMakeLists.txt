add_library(fn_lib OBJECT
            allocator.cpp
            bytes.cpp
            compile.cpp
            interpret.cpp
            ffi/builtin.cpp
            ffi/interpreter_handle.cpp
            llir.cpp
            namespace.cpp
            parse.cpp
            scan.cpp
            values.cpp
            vm.cpp
            )

target_include_directories(fn_lib PUBLIC
                           "${PROJECT_SOURCE_DIR}/include"
                           )

add_executable(fn main.cpp)

target_link_libraries(fn fn_lib)

install(TARGETS fn DESTINATION bin)
