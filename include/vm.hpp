// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "allocator.hpp"
#include "base.hpp"
#include "table.hpp"
#include "values.hpp"

#include <filesystem>
#include <forward_list>
#include <vector>

namespace fn {

namespace fs = std::filesystem;

// a linked list structure which associates source code locations to bytecode.
struct bytecode_loc {
    // maximum 
    u32 max_addr;
    source_loc loc;
    bytecode_loc *next;
};

// a bytecode object consists of a symbol table, a constant table, and an array of bytes which
// holds the actual instructions. the idea is that bytecode instances for fn are roughly analogous
// to object files for c.

// in principle this is almost enough information to link two or more bytecode objects together
// (i.e. by doing the necessary symbol translation + combining the constant tables), however we
// still need a way to adjust the addresses of functions (i.e. a table of function stubs). we'll
// cross that bridge much farther down the line, since we'll also certainly need to add other things
// to the bytecode object, like debugging and module information.
class bytecode {
private:
    // instruction array
    u32 cap;
    bc_addr size;
    u8* data;
    // source code locations list
    bytecode_loc* locs;
    // pointer to the end of the list
    bytecode_loc* last_loc;

    // constants and symbols
    vector<value> constants;
    symbol_table symbols;
    // constants which need to be freed in the destructor
    std::list<value> managed_constants;
    // function stubs
    vector<func_stub*> functions;

    void ensure_capacity(u32 new_cap);

public:
    bytecode();
    ~bytecode();

    bc_addr get_size() const;

    // set the location for writing bytes
    void set_loc(source_loc l);
    // get the source code location corresponding to the bytes at addr
    source_loc* location_of(bc_addr addr) const;

    // write 1 or two bytes
    void write_byte(u8 b);
    void write_short(u16 s);
    void write_bytes(const u8* bytes, bc_addr len);

    // these don't do bounds checking
    u8 read_byte(bc_addr addr) const;
    u16 read_short(bc_addr addr) const;
    void patch_short(bc_addr addr, u16 s);

    value get_constant(u16 id) const;
    u16 num_constants() const;

    // add a function and set it to start at the current ip
    u16 add_function(local_addr arity, bool vararg, value mod_id);
    func_stub* get_function(u16 id) const;

    // directly add values to the constants array and return their i_d
    const_id add_const(value v);
    // create a numerical constant
    const_id num_const(f64 num);
    // string constants are copied and automatically freed on destruction
    const_id str_const(const string& str);
    const_id str_const(const char* str);
    // create a new cons cell and return its 16-bit i_d
    const_id cons_const(value hd, value tl);
    // equivalent to add_const(symbol(name))
    const_id sym_const(const string& name);

    symbol_table* get_symbols();
    const symbol_table* get_symbols() const;
    value symbol(const string& name);
    optional<value> find_symbol(const string& name) const;
    u32 symbol_id(const string& name);

    inline u8& operator[](bc_addr addr) {
        return data[addr];
    }

    inline const u8& operator[](bc_addr addr) const {
        return data[addr];
    }

};


// v_m stack size limit (per call frame)
constexpr stack_addr STACK_SIZE = 255;

struct open_upvalue {
    upvalue_slot slot;
    local_addr pos;
};

struct call_frame {
    // call frame above this one
    call_frame *prev;
    // return address
    bc_addr ret_addr;
    // base pointer (i.e. offset from the true bottom of the stack)
    stack_addr bp;
    // the function we're in. nullptr on the top level.
    function* caller;
    // the number of arguments we need to pop after exiting the current call
    local_addr num_args;

    // current stack pointer
    stack_addr sp;
    // currently open upvalues
    forward_list<open_upvalue> open_upvals;

    call_frame(call_frame* prev, bc_addr ret_addr, stack_addr bp, function* caller, local_addr num_args=0)
        : prev(prev)
        , ret_addr(ret_addr)
        , bp(bp)
        , caller(caller)
        , num_args(num_args)
        , sp(num_args)
        , open_upvals()
    { }

    // allocate a new call frame as an extension of this one. assumes the last num_args values on the
    // stack are arguments for the newly called function.
    call_frame* extend_frame(bc_addr ret_addr, local_addr num_args, function* caller);

    // create a new upvalue. ptr should point to the stack at pos.
    upvalue_slot create_upvalue(local_addr pos, value* ptr);
    // decrement the stack pointer and close affected upvalues
    void close(stack_addr n);
    // close all open upvalues regardless of stack position
    void close_all();
};

// the v_m object contains all global state for a single instance of the interpreter.
class virtual_machine {
private:
    bytecode code;
    object* module;
    // the namespace hierarchy contains the module hierarchy
    object* ns;
    object* core_mod;

    allocator alloc;

    // foreign functions table
    vector<value> foreign_funcs;

    // working directory
    fs::path wd;

    // instruction pointer and stack
    stack_addr ip;
    call_frame *frame;
    value stack[STACK_SIZE];

    // last pop; used to access the result of the last expression
    value lp;

    // create and initialize a new module in the ns hierarchy. (this includes setting up the
    // _modinfo and ns variables).
    object* init_module(value module_id);
    // search for a module in the ns object. returns nullptr on failure
    object* find_module(value module_id);
    //object* find_module(value module_id);

    // stack operations
    value pop();
    value pop_times(stack_addr n);
    void push(value v);

    // peek relative to the top of the stack
    value peek(stack_addr offset=0) const;
    // get a local value from the current call frame
    value local(local_addr l) const;
    // set a local_addr value
    void set_local(local_addr l, value v);

    // returns the next addr to go to
    bc_addr call(local_addr num_args);
    bc_addr apply(local_addr num_args);

    // get a generator which returns the root objects for the gc
    generator<value> generate_roots();

public:
    // initialize the v_m with a blank image
    virtual_machine();
    ~virtual_machine();

    // step a single instruction
    void step();
    // execute instructions (stops if the end of the generated bytecode is reached)
    void execute();
    // get the instruction pointer
    bc_addr get_ip() const;

    // get the last popped value (null if there isn't any)
    value last_pop() const;

    // add a foreign function and bind it to a global variable
    void add_foreign(string name,
                     value (*func)(local_addr, value*, virtual_machine*),
                     local_addr min_args,
                     bool var_args=false);

    void add_global(value name, value v);
    value get_global(value name);

    upvalue_slot get_upvalue(local_addr id) const;

    // get a pointer to the bytecode object so the compiler can write its output there
    bytecode* get_bytecode();
    allocator* get_alloc();

    // raise an exception of type fn_error containing the provided message
    void runtime_error(const string& msg) const;

};




}

#endif
