// bytecode definitions
#ifndef __FN_BYTES_HPP
#define __FN_BYTES_HPP

#include "array.hpp"
#include "base.hpp"
#include "parse.hpp"
#include "values.hpp"

#include <iostream>
#include <list>

namespace fn {


/// instruction opcodes

// note: whenever an instruction uses a value on the stack, that value is popped
// off unless otherwise specified

enum OPCODES : u8 {
    // nop, do absolutely nothing
    OP_NOP,

    // pop, pop one element off the top of the stack
    OP_POP,
    // local BYTE, access the BYTEth element of the stack, indexed from the bottom
    OP_LOCAL,
    // set-local BYTE, set the BYTEth element of the stack to the current top of the stack
    OP_SET_LOCAL,
    // copy BYTE, works like OP_LOCAL but its indices count down from the top of the stack
    OP_COPY,

    // upvalue BYTE, get the BYTEth upvalue
    OP_UPVALUE,
    // set-upvalue BYTE, set the BYTEth upvalue to the value on top of the stack
    OP_SET_UPVALUE,
    // closure SHORT. instantiate a closure using SHORT as the function id. Also
    // takes the function's init values as arguments on the stack. Init vals are
    // ordered with the last one in the parameter list on the top of the stack.
    OP_CLOSURE,
    // close BYTE. pop the stack BYTE times, closing any open upvalues in the
    // process
    OP_CLOSE,

    // NOTE: might be better if set_macro, set_global had argument orders switched

    // global. get a global variable. stack arguments ->[symbol]
    OP_GLOBAL,
    // set-global. set a global variable. stack arguments ->[value] symbol
    OP_SET_GLOBAL,
    // obj-get. get the value of a property. stack arguments ->[key] obj
    OP_OBJ_GET,
    // obj-set. add or update an entry. stack arguments ->[new-value] key obj
    // ...
    OP_OBJ_SET,
    // macro-get, get the function associated to a symbol, raising an error if there
    // is none. stack arguments ->[symbol]
    OP_MACRO,
    // macro-set, set the macro function associated to a symbol. stack arguments:
    // ->[function] symbol.
    OP_SET_MACRO,
    // get global by its full name, e.g. /fn/builtin:map
    OP_BY_GUID,

    // perform method calls. Stack: ->[arg-n] ... arg-0 object symbol
    OP_CALLM,
    OP_TCALLM,

    // const SHORT, push a constant, identified by its 16-bit id
    OP_CONST,
    // nil, push nil value
    OP_NIL,
    // no, push false value
    OP_NO,
    // yes, push true value
    OP_YES,


    // control flow & function calls

    // jump SHORT, add signed SHORT to ip
    OP_JUMP,
    // cjump SHORT, if top of the stack is falsey, add signed SHORT to ip
    OP_CJUMP,
    // call BYTE, perform a function call. Uses BYTE+1 elements on the stack,
    // one for the function, one for each positional argument.
    // -> [func] pos-arg-n ... pos-arg-1
    OP_CALL,
    // tcall BYTE, perform a tail call
    OP_TCALL,
    // apply BYTE, apply function. Uses BYTE+2 stack elements. ->[func] args
    // pos-arg-n ... pos-arg-1. Like call, but expands the list args to provide
    // additional positional arguments to the function.
    OP_APPLY,
    // tail call version of apply
    OP_TAPPLY,
    // return, return from the current function
    OP_RETURN,


    // import, stack arguments ->[ns_id], perform an import using the given
    // namespace id (symbol).
    OP_IMPORT,

    OP_TABLE
};


// gives the width of an instruction + its operands in bytes
inline u8 instr_width(u8 instr) {
    switch (instr) {
    case OP_NOP:
    case OP_POP:
    case OP_NIL:
    case OP_NO:
    case OP_YES:
    case OP_RETURN:
    case OP_OBJ_GET:
    case OP_OBJ_SET:
    case OP_MACRO:
    case OP_SET_MACRO:
    case OP_IMPORT:
    case OP_TABLE:
        return 1;
    case OP_LOCAL:
    case OP_SET_LOCAL:
    case OP_COPY:
    case OP_UPVALUE:
    case OP_SET_UPVALUE:
    case OP_CLOSE:
    case OP_CALL:
    case OP_TCALL:
    case OP_CALLM:
    case OP_TCALLM:
    case OP_APPLY:
    case OP_TAPPLY:
        return 2;
    case OP_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_CONST:
    case OP_JUMP:
    case OP_CJUMP:
    case OP_CLOSURE:
        return 3;
    default:
        // TODO: shouldn't get here. maybe raise a warning?
        return 1;
    }
}

}


#endif
