// bytecode definitions
#ifndef __FN_BYTES_HPP
#define __FN_BYTES_HPP

#include "base.hpp"
#include "vm.hpp"

#include <iostream>

namespace fn_bytes {

using namespace fn;

/// Instruction Opcodes

// Note: whenever an instruction uses a value on the stack, that value is popped off.

// nop; do absolutely nothing
constexpr u8 OP_NOP = 0x00;

// pop; pop one element off the top of the stack
constexpr u8 OP_POP = 0x01;
// local BYTE; access the BYTEth element of the stack, indexed from the bottom
constexpr u8 OP_LOCAL = 0x02;
// set-local BYTE; set the BYTEth element of the stack to the current top of the stack
constexpr u8 OP_SET_LOCAL = 0x03;
// copy BYTE; works like OP_LOCAL but its indices count down from the top of the stack
constexpr u8 OP_COPY = 0x04;

// upvalue BYTE; get the BYTEth upvalue
constexpr u8 OP_UPVALUE = 0x05;
// set-upvalue BYTE; set the BYTEth upvalue to the value on top of the stack
constexpr u8 OP_SET_UPVALUE = 0x06;
// closure SHORT; instantiate a closure using SHORT as the function ID
constexpr u8 OP_CLOSURE = 0x07;
// close BYTE; pop the stack BYTE times, closing any open upvalues in the process
constexpr u8 OP_CLOSE = 0x08;

// global; get a global variable based on a string on top of the stack
constexpr u8 OP_GLOBAL = 0x10;
// set-global; set a global based on a string on top of the stack followed by its value
constexpr u8 OP_SET_GLOBAL = 0x11;

// const SHORT; load a constant via its 16-bit ID
constexpr u8 OP_CONST = 0x12;
// null; push a null value on top of the stack
constexpr u8 OP_NULL  = 0x13;
// false; push a false value on top of the stack
constexpr u8 OP_FALSE = 0x14;
// true; push a true value on top of the stack
constexpr u8 OP_TRUE  = 0x15;


// control flow & function calls

// jump SHORT; add signed SHORT to ip
constexpr u8 OP_JUMP = 0x30;
// cjump SHORT; if top of the stack is falsey, add signed SHORT to ip
constexpr u8 OP_CJUMP = 0x31;
// call BYTE; perform a function call
constexpr u8 OP_CALL = 0x32;
// return; return from the current function
constexpr u8 OP_RETURN = 0x33;

// might want this for implementing apply
// // apply;
// constexpr u8 OP_APPLY = 0x34;

// might want this for implementing TCO
// // long-jump ADDR; jump to the given 32-bit address
// constexpr u8 OP_LONG_JUMP = 0x35;


// gives the width of an instruction + its operands in bytes
inline u8 instrWidth(u8 instr) {
    switch (instr) {
    case OP_NOP:
    case OP_POP:
    case OP_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_NULL:
    case OP_FALSE:
    case OP_TRUE:
    case OP_RETURN:
        return 1;
    case OP_LOCAL:
    case OP_SET_LOCAL:
    case OP_COPY:
    case OP_UPVALUE:
    case OP_SET_UPVALUE:
    case OP_CLOSE:
    case OP_CALL:
        return 2;
    case OP_CONST:
    case OP_JUMP:
    case OP_CJUMP:
    case OP_CLOSURE:
        return 3;
    default:
        // TODO: shouldn't get here. Maybe raise a warning?
        return 1;
    }
}


// disassembly a single instruction, writing output to out
void disassembleInstr(Bytecode& code, u32 ip, ostream& out);


void disassemble(Bytecode& code, ostream& out);


}


#endif
