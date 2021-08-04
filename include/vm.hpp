// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "allocator.hpp"
#include "base.hpp"
#include "compile.hpp"
#include "parse.hpp"
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

//// NOTE: bytecode object is being replaced

// a bytecode object consists of a symbol table, a constant table, and an array
// of bytes which holds the actual instructions. the idea is that bytecode
// instances for fn are roughly analogous to object files for c.

// in principle this is almost enough information to link two or more bytecode
// objects together (i.e. by doing the necessary symbol translation + combining
// the constant tables), however we still need a way to adjust the addresses of
// functions (i.e. a table of function stubs). we'll cross that bridge much
// farther down the line, since we'll also certainly need to add other things to
// the bytecode object, like debugging and namespace information.
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
    const_id next_const = 0;
    table<value,const_id> const_lookup;
    vector<value> constants;
    symbol_table symtab;
    // symbol locations
    
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

    // write code which defines a global variable set to a function containing
    // the provided bytecode. (The bytecode must end in a return or a tail
    // call or else something bad could happen).
    void define_bytecode_function(const string& name,
                                  const vector<symbol_id>& positional,
                                  local_addr optional_index,
                                  bool var_list,
                                  bool var_table,
                                  fn_namespace* ns,
                                  vector<u8>& bytes);

    // these don't do bounds checking
    u8 read_byte(bc_addr addr) const;
    u16 read_short(bc_addr addr) const;
    void patch_short(bc_addr addr, u16 s);

    value get_constant(u16 id) const;
    u16 num_constants() const;

    // add a function to the current ns, set to start at the current ip
    u16 add_function(const vector<symbol_id>& positional,
                     local_addr optional_index,
                     bool var_list,
                     bool var_table,
                     fn_namespace* ns);
    func_stub* get_function(u16 id) const;

    // directly add values to the constants array and return their ID
    const_id add_const(value v);
    const_id add_sym_const(symbol_id s);

    symbol_table* get_symbol_table();
    const symbol_table* get_symbol_table() const;
    value get_symbol(const string& name);
    symbol_id get_symbol_id(const string& name);

    inline u8& operator[](bc_addr addr) {
        return data[addr];
    }

    inline const u8& operator[](bc_addr addr) const {
        return data[addr];
    }

};


// virtual_machine stack size limit (per call frame)
constexpr stack_addr STACK_SIZE = 255;

struct open_upvalue {
    upvalue_slot slot;
    local_addr pos;
};

struct call_frame {
    // call frame above this one
    call_frame* prev;
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
        , open_upvals() {
    }

    // allocate a new call frame as an extension of this one. assumes the last
    // num_args values on the stack are arguments for the newly called function.
    call_frame* extend_frame(bc_addr ret_addr, local_addr num_args, function* caller);

    // create a new upvalue. ptr should point to the stack at pos.
    upvalue_slot create_upvalue(local_addr pos, value* ptr);
    // decrement the stack pointer and close affected upvalues
    void close(stack_addr n);
    // close all open upvalues regardless of stack position
    void close_all();
};

// the virtual_machine object contains all global state for a single instance of
// the interpreter.
struct virtual_machine {
private:
    // these are pointers to objects maintained by the interpreter
    code_chunk* code;
    allocator* alloc;

    // current namespace
    fn_namespace* cur_ns;
    // root of the namespace hierarchy
    fn_namespace* ns_root;
    // specially designated core namespace
    fn_namespace* core_ns;

    // IMPLNOTE: not sure if the allocator should be here, or if this should be
    // a pointer to an instance which is actually owned by the interpreter. The
    // problem is that the compiler also wants the allocator for when it makes
    // quoted forms and strings.

    // foreign functions table
    vector<value> foreign_funcs;

    // working directory
    string wd;

    // instruction pointer and stack
    bc_addr ip;
    call_frame *frame;
    value stack[STACK_SIZE];

    // last pop; used to access the result of the last expression
    value lp;

    // create and initialize a new namespace in the ns hierarchy. (this includes
    // setting up the _modinfo and ns variables).
    fn_namespace* init_namespace(value namespace_id);
    //fn_namespace* find_namespace(value namespace_id);

    // peek relative to the top of the stack
    value peek(stack_addr offset=0) const;
    // get a local value from the current call frame
    value local(local_addr l) const;
    // set a local_addr value
    void set_local(local_addr l, value v);

    // get a vector which returns the root objects for the gc
    vector<value> get_roots();

    // returns the next addr to go to
    // FIXME: these are public for foreign functions, but maybe they shouldn't
    // be?
    bc_addr call(local_addr num_args);
    bc_addr apply(local_addr num_args);

public:
    // initialize the virtual machine
    virtual_machine(allocator* use_alloc, code_chunk* chunk);
    ~virtual_machine();

    // FIXME: not sure these should be public
    // stack operations
    value pop();
    value pop_times(stack_addr n);
    void push(value v);

    // Queue up a function application for immediate execution. This will result
    // in the apply arguments being consumed and the return value being pushed
    // to the top of the stack. num_args is the number of positional arguments,
    // so num_args + 3 arguments are consumed.
    void do_apply(local_addr num_args);

    void set_wd(const string& new_wd);
    string get_wd();

    // compile_ methods just compile, don't execute
    void compile_string(const string& src, const string& origin="<cmdline>");
    void compile_file(const string& filename);

    // interpret_ methods compile and execute 1 expression at a time
    void interpret_string(const string& src, const string& origin="<cmdline>");
    void interpret_file(const string& filename);

    value get_symbol(const string& name) {
        return as_sym_value(get_symtab().intern(name)->id);
    }

    // Search for a namespace in the ns_root. Returns nullptr on failure
    fn_namespace* find_ns(value id);
    // Given a namespace id, find a file containing it. This does not validate
    // id, so there will be a memory error if it is not a list of symbols.
    optional<string> find_ns_file(value id);

    // Create a new namespace and return it as a value. id is a list of symbols
    // identifying the namespace. Invalid id will cause a runtime error. No
    // variables are set.
    fn_namespace* create_empty_ns(value id);
    // Create and initialize new namespace. (At the moment, this just creates an
    // ns variable).
    fn_namespace* create_ns(value id);
    // Like create_namespace(value), but copies the contents of the template
    // namespace into the new one. New copies of objects are not created.
    fn_namespace* create_ns(value id, fn_namespace* templ);

    // Interprets the given file in a fresh namespace. The resulting global
    // namespace is inserted into the ns hierarchy and returned. id is a
    // symbol or a list of symbols determining where it is inserted.
    fn_namespace* load_ns(value id, const string& filename);
    fn_namespace* import_ns(value id);

    // switch to another namespace
    void use_ns(fn_namespace* ns);
    // current namespace of the VM
    fn_namespace* current_ns();

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
                     optional<value> (*func)(local_addr, value*, virtual_machine*),
                     local_addr min_args,
                     bool var_args=false);

    void add_global(value name, value v);
    value get_global(value name);

    upvalue_slot get_upvalue(local_addr id) const;

    // access the bytecode, allocator, and symbol table
    bytecode& get_bytecode();
    allocator& get_alloc();
    symbol_table& get_symtab();


    // raise an exception of type fn_error containing the provided message
    void runtime_error(const string& msg) const;

};


// disassemble a single instruction, writing output to out
void disassemble_instr(const bytecode& code, bc_addr ip, std::ostream& out);

void disassemble(const bytecode& code, std::ostream& out);


}

#endif
