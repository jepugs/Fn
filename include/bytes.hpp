// bytecode definitions
#ifndef __FN_BYTES_HPP
#define __FN_BYTES_HPP

#include "base.hpp"
#include "vm.hpp"

#include <iostream>

namespace fn_bytes {

using namespace fn;

constexpr u8 OP_NOP = 0x00;
constexpr u8 OP_POP = 0x01;
// copy a value at the specified 8-bit offset from the top of the stack
constexpr u8 OP_COPY = 0x02;
// copy a value from the stack and pushes it. Ues an 8-bit address which counts from the bottom of
// the current call frame up.
constexpr u8 OP_LOCAL = 0x03;
// get a global variable based on a string on top of the stack
constexpr u8 OP_GET_GLOBAL = 0x05;
// set a global based on a string on top of the stack followed by its value
constexpr u8 OP_SET_GLOBAL = 0x06;


// constants

// load a constant via its 16-bit ID
constexpr u8 OP_CONST = 0x10;
// load null, false, or true constant values
constexpr u8 OP_NULL  = 0x11;
constexpr u8 OP_FALSE = 0x12;
constexpr u8 OP_TRUE  = 0x13;

// replace the top of the stack with its boolean negation
constexpr u8 OP_NEGATE = 0x14;
// binary equality check
constexpr u8 OP_EQ = 0x15;
// check for identicalness
constexpr u8 OP_IS = 0x16;


// control flow & function calls

// skip the instruction pointer 16 bits forward if the head of the stack is true, false, resp.
constexpr u8 OP_SKIP_TRUE = 0x30;
constexpr u8 OP_SKIP_FALSE = 0x31;
// relative jump to the 8-bit offset following the instruction
constexpr u8 OP_JUMP = 0x32;
// call the function in the register
constexpr u8 OP_CALL = 0x37;
// return with the top of the stack as the value
constexpr u8 OP_RETURN = 0x38;


// numbers

constexpr u8 OP_CK_NUM = 0x50;
constexpr u8 OP_CK_INT = 0x51;
// binary operations
constexpr u8 OP_ADD = 0x52;
constexpr u8 OP_SUB = 0x53;
constexpr u8 OP_MUL = 0x54;
constexpr u8 OP_DIV = 0x55;
constexpr u8 OP_POW = 0x56;
// comparisions
// greater than, less than
constexpr u8 OP_GT = 0x57;
constexpr u8 OP_LT = 0x58;

// lists

// takes argumenst -> [head] tail where tail must be a list (or an exception is thrown)
constexpr u8 OP_CONS = 0x70;
// get head, tail resp. of a cons.
constexpr u8 OP_HEAD = 0x71;
constexpr u8 OP_TAIL = 0x72;
// check type of the object on top of the stack. Return a boolean
constexpr u8 OP_CK_CONS = 0x74;
constexpr u8 OP_CK_EMPTY = 0x75;
// a list is either a cons or an empty
constexpr u8 OP_CK_LIST = 0x76;

// gives the width of an instruction + its operands in bytes
inline u8 instrWidth(u8 instr) {
    switch (instr) {
    case OP_NOP:
    case OP_POP:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_NULL:
    case OP_FALSE:
    case OP_TRUE:
    case OP_NEGATE:
    case OP_EQ:
    case OP_IS:
    case OP_SKIP_TRUE:
    case OP_SKIP_FALSE:
    case OP_RETURN:
    // numbers
    case OP_CK_NUM:
    case OP_CK_INT:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_POW:
    case OP_GT:
    case OP_LT:
    // lists
    case OP_CONS:
    case OP_HEAD:
    case OP_TAIL:
    case OP_CK_CONS:
    case OP_CK_EMPTY:
    case OP_CK_LIST:
        return 1;

    case OP_LOCAL:
    case OP_COPY:
    case OP_JUMP:
    case OP_CALL:
        return 2;
    case OP_CONST:
        return 3;

    default:
        return 1;
    }
}


// disassembly a single instruction, writing output to out
void disassembleInstr(Bytecode& code, u32 ip, ostream& out);


void disassemble(Bytecode& code, ostream& out);


}


#endif
