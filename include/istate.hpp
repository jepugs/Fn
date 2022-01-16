#ifndef __FN_SSTATE_HPP
#define __FN_SSTATE_HPP

#include "base.hpp"
#include "obj.hpp"
#include "values.hpp"

namespace fn {

// size used for the istate stack
constexpr u32 STACK_SIZE = 256;

// list of stack values that correspond to upvalues
struct open_upvalue {
    u32 index;
    upvalue_cell* uv;
    open_upvalue* next;
};

struct allocator;
struct global_env;

struct istate {
    allocator* alloc;
    symbol_table* symtab;
    table<symbol_id,fn_namespace*> globals;  // all loaded namespaces
    table<symbol_id,value> by_guid;          // all globals, indexed by GUID
    symbol_id ns_id;                         // current namespace ID
    fn_namespace* ns;                        // current namespace
    u32 pc;                                  // program counter
    value stack[STACK_SIZE];
    u32 bp;                                  // base ptr
    u32 sp;                                  // stack ptr (rel to stack bottom)
    open_upvalue* uv_head;                   // open upvalues on the stack
    // error handling
    bool err_happened;
    // this is nullptr unless err_happened == true, in which case it must be
    // freed after processing the error.
    char* err_msg;
};

istate* init_istate();
void free_istate(istate*);

//void ierror(istate* S, const char* message);
void ierror(istate* S, const string& message);

void push(istate* S, value v);
void pop(istate* S);
void pop(istate* S, u32 n);
// peek values relative to the top of the stack
value peek(istate* S);
value peek(istate* S, u32 offset);
// get and set values relative to the base pointer
value get(istate* S, u32 index);
void set(istate* S, u32 index, value v);

// get symbols
symbol_id intern(istate* S, const string& str);
symbol_id gensym(istate* S);

// create values on top of the stack
void push_number(istate* S, f64 num);
void push_string(istate* S, u32 size);
void push_string(istate* S, const string& str);
void push_sym(istate* S, symbol_id sym);
void push_nil(istate* S);
void push_true(istate* S);
void push_false(istate* S);

// create a list from the top n elements of the stack
void pop_to_list(istate* S, u32 n);

// perform a function call of n arguments, (default 0). The calling convention
// is to put the function on the bottom followed by the arguments in order, so
// the last argument is on top of the stack.
void call(istate* S);
void call(istate* S, u32 n);

// push a function with an newly-created, empty function stub. This is used
// during function compilation to ensure the function stub is visible to the
// compiler while the function is being created.
void push_empty_fun(istate* S);
// push a foreign function by that wraps the provided function pointer
void push_foreign_fun(istate* S, void (*foreign)(istate*));

// print out the top of the stack to stdout
void print_top(istate* S);

}


#endif
