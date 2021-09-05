// bytecode definitions
#ifndef __FN_BYTES_HPP
#define __FN_BYTES_HPP

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
    u32 end_addr;
    source_loc loc;

    chunk_source_info(u32, const source_loc&);
};

// IMPLNOTE: the structures below currently contain STL containers, but it might
// be better to replace these with C-style ad-hoc containers, in case I want to
// make C bindings later.

// A code_chunk is a dynamic array of bytecode instructions combined with all
// constants and functions used by that chunk.
struct code_chunk {
private:
    vector<u8> code;
    vector<value> const_table;

    vector<fn_string*> const_strings;
    vector<cons*> const_conses;
    // TODO: use function_stub
    vector<function_stub*> functions;
    std::list<chunk_source_info> source_info;
    value ns;

    // This is a weak reference. It's the responsibility of the code using the
    // chunk to make sure the pertinent symbol table is alive.
    symbol_table* st;

    value quote_helper(const fn_parse::ast_node* node);

public:
    code_chunk(symbol_table* use_st, value use_ns);
    ~code_chunk();

    // chunk size in bytes
    u32 size() const;
    symbol_table* get_symtab();

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
    const_id const_num(f64 num);
    const_id const_sym(symbol_id id);
    const_id const_string(const fn_string& s);
    const_id const_quote(const fn_parse::ast_node* node);
    // get a constant
    value get_const(const_id id) const;

    // add a new function and return its id. pparams is a list of parameter
    // names. req_args is number of required args.

    u16 add_function(const vector<symbol_id>& pparams,
                     local_addr req_args,
                     optional<symbol_id> vl_param,
                     optional<symbol_id> vt_param);
    function_stub* get_function(u16 id);
    const function_stub* get_function(u16 id) const;

    // add a source location. new writes to the end will use this value
    void add_source_loc(const source_loc& s);
    // find the location of an instruction
    source_loc location_of(u32 addr);
};


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
// close BYTE; pop the stack BYTE times, closing any open upvalues in the process
constexpr u8 OP_CLOSE = 0x08;

// global; get a global variable based on a string on top of the stack
constexpr u8 OP_GLOBAL = 0x10;
// set-global; set a global variable. stack arguments ->[value] symbol ...
// note: leaves the symbol on the stack
constexpr u8 OP_SET_GLOBAL = 0x11;

// const SHORT; load a constant via its 16-bit id
constexpr u8 OP_CONST = 0x12;
// null; push a null value on top of the stack
constexpr u8 OP_NULL  = 0x13;
// false; push a false value on top of the stack
constexpr u8 OP_FALSE = 0x14;
// true; push a true value on top of the stack
constexpr u8 OP_TRUE  = 0x15;


// obj-get;  stack arguments ->[key] obj; get the value of a property.
constexpr u8 OP_OBJ_GET = 0x16;
// obj-set; add or update the value of a property. stack arguments ->[new-value] key obj ...
constexpr u8 OP_OBJ_SET = 0x17;

// TODO:
// TODO: implement these changes in vm
// TODO:
// import; stack arguments ->[ns_id]; perform an import using the given
// namespace id.
constexpr u8 OP_IMPORT = 0x19;

// ns_root; push the namespace root to the top of the stack
constexpr u8 OP_NS_ROOT = 0x20;


// control flow & function calls

// jump SHORT; add signed SHORT to ip
constexpr u8 OP_JUMP = 0x30;
// cjump SHORT; if top of the stack is falsey, add signed SHORT to ip
constexpr u8 OP_CJUMP = 0x31;
// call BYTE; perform a function call. Uses BYTE+2 elements on the stack,
// one for the function, one for each positional argument, and one for the
// keyword table
constexpr u8 OP_CALL = 0x32;
// return; return from the current function
constexpr u8 OP_RETURN = 0x33;

// FIXME: this one doesn't work lol
// apply BYTE; like call, but the last argument is actually a list to be expanded as individual
// arugments
constexpr u8 OP_APPLY = 0x34;

// tcall BYTE; perform a tail call
//constexpr u8 OP_TCALL = 0x35;

// value manipulation
// FIXME: not sure if I need this...
// table ; create a new empty table
constexpr u8 OP_TABLE = 0x40;
// constexpr u8 OP_EMPTY = 0x41;
// constexpr u8 OP_CONS  = 0x42;


// gives the width of an instruction + its operands in bytes
inline u8 instr_width(u8 instr) {
    switch (instr) {
    case OP_NOP:
    case OP_POP:
    case OP_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_NULL:
    case OP_FALSE:
    case OP_TRUE:
    case OP_RETURN:
    case OP_OBJ_GET:
    case OP_OBJ_SET:
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
    case OP_APPLY:
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
