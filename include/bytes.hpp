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

// each chunk stores a linked list of chunk_source_info objects. To find the
// source_loc associated to an address, we search for the smallest end_addr
// greater than the address.

// IMPLNOTE: this structure could be made into a linked list itself for the C
// version of the VM.
struct chunk_source_info {
    u32 start_addr;
    source_loc loc;
    chunk_source_info* prev;
};

class allocator;

// A code_chunk is a dynamic array of bytecode instructions combined with all
// constants and functions used by that chunk.
struct code_chunk {
    gc_header h;
    // we use this to track changes to the chunk's size
    allocator* alloc;

    // namespace id
    symbol_id ns_id;
    // (dynamic) arrays holding chunk data
    dyn_array<u8> code;
    dyn_array<value> constant_arr;
    // This table is to prevent duplicate constants from being inserted. This
    // turns out to save memory overall. When serializing chunks, this should
    // obviously be dropped.
    table<value,constant_id> constant_table;

    dyn_array<function_stub*> function_arr;
    // debug information
    chunk_source_info* source_info;

    // read a byte. Requires (where < size())
    u8 read_byte(u32 where) const;
    // read a 2-byte short. Requires (where < size())
    u16 read_short(u32 where) const;

    // write a byte to the end of the chunk
    void write_byte(u8 data);
    // overwrite a byte in the chunk. Requires (where < size())
    void write_byte(u8 data, u32 where);
    // write a 2-byte quantity to the end of the chunk
    void write_short(u16 data);
    // overwrite 2 bytes in the chunk. Requires (where < size()-1)
    void write_short(u16 data, u32 where);

    // add constants. No duplication checking is done, and if the chunk has
    // already been marked global in the allocator, newly added values have to
    // be marked global themselves.
    constant_id add_constant(value v);
    constant_id add_string(const string& str);
    constant_id add_quoted(fn_parse::ast_form* ast);
    // get a constant
    value get_constant(constant_id id) const;

    // add a new function and return its id. pparams is a list of parameter
    // names. req_args is number of required args.
    u16 add_function(local_address num_pos,
            symbol_id* pos_params,
            local_address req_args,
            optional<symbol_id> vl_param,
            optional<symbol_id> vt_param,
            const string& name);
    u16 add_foreign_function(local_address num_pos,
            symbol_id* pos_params,
            local_address req_args,
            optional<symbol_id> vl_param,
            optional<symbol_id> vt_param,
            void (*foreign_func)(fn_handle*, value*),
            const string& name);
    function_stub* get_function(u16 id);
    const function_stub* get_function(u16 id) const;

    // add a source location. new writes to the end will use this value
    void add_source_loc(const source_loc& s);
    // find the location of an instruction
    source_loc location_of(u32 addr);
};

// Initialize a new code chunk at location dest. If dest=nullptr, then a new
// code_chunk is allocated. Returns a pointer to the created object.
code_chunk* mk_code_chunk(allocator* use_alloc, symbol_id ns_id);

// TODO: due to progress made since originally writing this, we should probably
// just use a destructor instead of this function.
// Deallocate code chunk members. This will not handle freeing the constant
// values, (but it will take care of the function stubs)
void free_code_chunk(code_chunk* obj);


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

    // look up a method (in an object's metatable). Stack arugments ->[sym] obj
    OP_METHOD,


    // const SHORT, push a constant, identified by its 16-bit id
    OP_CONST,
    // nil, push nil value
    OP_NIL,
    // false, push false value
    OP_FALSE,
    // true, push true value
    OP_TRUE,


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
    case OP_BY_GUID:
    case OP_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_NIL:
    case OP_FALSE:
    case OP_TRUE:
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
    case OP_APPLY:
    case OP_TAPPLY:
        return 2;
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
