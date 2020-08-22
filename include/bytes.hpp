// bytecode definitions
#ifndef __FN_BYTES_HPP
#define __FN_BYTES_HPP

#include "base.hpp"
#include "vm.hpp"

#include <iostream>

namespace fn_bytes {

using namespace fn;

constexpr u8 OP_NOP = 0x00;

// stack operations

// pop; pops an element off the top of the stack
constexpr u8 OP_POP = 0x01;

// local BYTE; access the BYTEth element of the stack, indexed from the bottom
constexpr u8 OP_LOCAL = 0x02;
// set-local BYTE; set the BYTEth element of the stack to the current top of the stack
constexpr u8 OP_SET_LOCAL = 0x03;

// copy BYTE; works like OP_LOCAL but its indices count down from the top of the stack
constexpr u8 OP_COPY = 0x04;

// global; get a global variable based on a string on top of the stack
constexpr u8 OP_GLOBAL = 0x05;
// set-global; set a global based on a string on top of the stack followed by its value
constexpr u8 OP_SET_GLOBAL = 0x06;

// upvalue BYTE; get the BYTEth upvalue
constexpr u8 OP_UPVALUE = 0x07;
// set-upvalue BYTE; set the BYTEth upvalue to the value on top of the stack
constexpr u8 OP_SET_UPVALUE = 0x08;

// closure SHORT; instantiate a closure using SHORT as the function ID. Requires that the closure's
// specifed upvalues and stack locations exist and the function ID is valid.
constexpr u8 OP_CLOSURE = 0x09;

// close BYTE; pop the stack BYTE times, closing any open upvalues in the process
constexpr u8 OP_CLOSE = 0x0A;

// const SHORT; load a constant via its 16-bit ID. Requires that the constant ID is valid.
constexpr u8 OP_CONST = 0x10;

// null; push a null value on top of the stack
constexpr u8 OP_NULL  = 0x11;
// false; push a false value on top of the stack
constexpr u8 OP_FALSE = 0x12;
// true; push a true value on top of the stack
constexpr u8 OP_TRUE  = 0x13;


// [DEPRECATED] save the value at the top of the stack and unroll the next <byte> of them
constexpr u8 OP_UNROLL = 0x0B;

// control flow & function calls

// skip-true; must be followed by a jump instruction. If the top of the stack is truthy, increment ip
// to skip over the jump instruction.
constexpr u8 OP_SKIP_TRUE = 0x30;
// skip-false; must be followed by a jump instruction. Like skip-true
constexpr u8 OP_SKIP_FALSE = 0x31;
// jump SHORT; adds SHORT to ip. SHORT is a (2's complement) signed value here. Offset is relative
// to the end of the jump instruction, (e.g. jump -3 is an infinite loop, jump 0 is a NOP).
constexpr u8 OP_JUMP = 0x32;
// call the function in the register
constexpr u8 OP_CALL = 0x37;
// return with the top of the stack as the value
constexpr u8 OP_RETURN = 0x38;


// replace the top of the stack with its boolean negation
constexpr u8 OP_NEGATE = 0x14;
// binary equality check
constexpr u8 OP_EQ = 0x15;
// check for identicalness
constexpr u8 OP_IS = 0x16;




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
    case OP_GLOBAL:
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
    case OP_UPVALUE:
    case OP_SET_UPVALUE:
    case OP_UNROLL:
    case OP_CALL:
        return 2;
    case OP_CONST:
    case OP_JUMP:
    case OP_CLOSURE:
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
