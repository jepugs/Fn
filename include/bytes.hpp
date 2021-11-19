// bytecode definitions
#ifndef __FN_BYTES_HPP
#define __FN_BYTES_HPP

#include "base.hpp"
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
    u32 end_addr;
    source_loc loc;
    chunk_source_info* prev;
};

// IMPLNOTE: the structures below currently contain STL containers, but it might
// be better to replace these with C-style ad-hoc containers, in case I want to
// make C bindings later.

// A code_chunk is a dynamic array of bytecode instructions combined with all
// constants and functions used by that chunk.
struct code_chunk {
    gc_header h;

    // namespace id
    symbol_id ns_id;
    // (dynamic) arrays holding chunk data
    u8* code;
    code_address code_size;
    code_address code_capacity;
    value* constant_table;
    constant_id num_constants;
    constant_id constant_capacity;
    function_stub** function_table;
    constant_id num_functions;
    constant_id function_capacity;
    // debug information
    chunk_source_info* source_info;

    // functions used to resize dynamic arrays
    void ensure_code_capacity(code_address min_cap);
    void ensure_constant_capacity(constant_id min_cap);
    void ensure_function_capacity(constant_id min_cap);

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

    // add constants. No duplication checking is done.
    constant_id add_constant(value v);
    // get a constant
    value get_constant(constant_id id) const;

    // add a new function and return its id. pparams is a list of parameter
    // names. req_args is number of required args.
    u16 add_function(const vector<symbol_id>& pparams,
                     local_address req_args,
                     optional<symbol_id> vl_param,
                     optional<symbol_id> vt_param);
    function_stub* get_function(u16 id);
    const function_stub* get_function(u16 id) const;

    // add a source location. new writes to the end will use this value
    void add_source_loc(const source_loc& s);
    // find the location of an instruction
    source_loc location_of(u32 addr);
};

// Initialize a new code chunk at location dest. If dest=nullptr, then a new
// code_chunk is allocated. Returns a pointer to the created object.
code_chunk* mk_code_chunk(symbol_id ns_id, code_chunk* dest=nullptr);
// Deallocate code chunk members. This will not handle freeing the constant
// values, (but it will take care of the function stubs)
void free_code_chunk(code_chunk* obj);


/// instruction opcodes

// note: whenever an instruction uses a value on the stack, that value is popped off.

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
// closure SHORT; instantiate a closure using SHORT as the function id. Also
// takes the function's init values as arguments on the stack. Init vals are
// ordered with the last one in the parameter list on the top of the stack.
constexpr u8 OP_CLOSURE = 0x07;
// close BYTE; pop the stack BYTE times, closing any open upvalues in the
// process
constexpr u8 OP_CLOSE = 0x08;

// global; get a global variable based on a string on top of the stack
constexpr u8 OP_GLOBAL = 0x10;
// set-global; set a global variable. stack arguments ->[value] symbol ...
constexpr u8 OP_SET_GLOBAL = 0x11;
// obj-get;  stack arguments ->[key] obj; get the value of a property.
constexpr u8 OP_OBJ_GET = 0x12;
// obj-set; add or update the value of a property. stack arguments ->[new-value]
// key obj ...
constexpr u8 OP_OBJ_SET = 0x13;
// macro-get; get the function associated to a symbol, raising an error if there
// is none. stack arguments ->[symbol]
constexpr u8 OP_MACRO_GET = 0x14;
// macro-set; set the macro function associated to a symbol. stack arguments:
// ->[function] symbol.
constexpr u8 OP_MACRO_SET = 0x15;


// const SHORT; push a constant, identified by its 16-bit id
constexpr u8 OP_CONST = 0x20;
// nil; push nil value
constexpr u8 OP_NIL  = 0x21;
// false; push false value
constexpr u8 OP_FALSE = 0x22;
// true; push true value
constexpr u8 OP_TRUE  = 0x23;


// control flow & function calls

// jump SHORT; add signed SHORT to ip
constexpr u8 OP_JUMP = 0x30;
// cjump SHORT; if top of the stack is falsey, add signed SHORT to ip
constexpr u8 OP_CJUMP = 0x31;
// call BYTE; perform a function call. Uses BYTE+2 elements on the stack,
// one for the function, one for each positional argument, and one for the
// keyword table
constexpr u8 OP_CALL = 0x32;
// tcall BYTE; perform a tail call
constexpr u8 OP_TCALL = 0x33;
// return; return from the current function
constexpr u8 OP_RETURN = 0x34;

// import; stack arguments ->[ns_id]; perform an import using the given
// namespace id (symbol).
constexpr u8 OP_IMPORT = 0x40;

constexpr u8 OP_TABLE = 0x50;


// gives the width of an instruction + its operands in bytes
inline u8 instr_width(u8 instr) {
    switch (instr) {
    case OP_NOP:
    case OP_POP:
    case OP_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_NIL:
    case OP_FALSE:
    case OP_TRUE:
    case OP_RETURN:
    case OP_OBJ_GET:
    case OP_OBJ_SET:
    case OP_MACRO_GET:
    case OP_MACRO_SET:
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
