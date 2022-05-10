set(CMAKE_ASM-ATT_COMPILER "${CMAKE_CXX_COMPILER}")
set(CMAKE_ASM-ATT_FLAGS "-c -x assembler-with-cpp")
message(STATUS "compile is ${CMAKE_ASM-ATT_COMPILER}")

add_library(asm_routines OBJECT
  add.s
  mul.s)
