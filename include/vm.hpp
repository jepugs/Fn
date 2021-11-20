// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "allocator.hpp"
#include "base.hpp"
#include "ffi/interpreter_handle.hpp"
#include "parse.hpp"
#include "table.hpp"
#include "values.hpp"

#include <filesystem>

namespace fn {

namespace fs = std::filesystem;

// virtual_machine stack size limit (per call frame)
constexpr stack_address STACK_SIZE = 255;

// This structure includes a pointer to the previous call frame, so it is
// actually a linked list representing the entire call stack.
struct call_frame {
    // call frame above this one
    call_frame* prev;
    // return address
    code_address ret_addr;
    // base pointer (i.e. offset from the true bottom of the stack)
    u32 bp;
    // the function we're in. nullptr on the top level.
    function* caller;
    // the number of arguments we need to pop after exiting the current call
    local_address num_args;

    call_frame(call_frame* prev,
            code_address ret_addr,
            u32 bp,
            function* caller,
            local_address num_args=0)
        : prev(prev)
        , ret_addr(ret_addr)
        , bp(bp)
        , caller(caller)
        , num_args(num_args) {
    }
};

// When a vm_thread finishes executing, it will leave behind some exit state,
// which can indicate that it's done, that it requires an import, etc.

// possible statuses for a vm_thread
enum vm_status {
    vs_stopped,
    vs_running,
    vs_waiting_for_import,
    vs_error
};

// WARNING: despite the name, vm_threads cannot truly be run in parallel (until
// the allocator and global_env are made threadsafe).

// vm_thread represents a single thread of the interpreter, so it has its own
// instruction pointer, stack, etc. This is where the bytecode execution logic
// is.
struct vm_thread {
private:
    // These are weak references to objects maintained by the interpreter.
    symbol_table* symtab;
    global_env* globals;
    allocator* alloc;
    code_chunk* toplevel_chunk;

    // current execution status
    vm_status status;
    // set when the execution status is set to vs_error
    string error_message;
    // set when the execution status is vs_waiting_for_import
    value pending_import_id;

    // instruction pointer and stack
    code_address ip;
    call_frame* frame;
    root_stack* stack;

    // last pop; used to access the result of the last expression
    value lp;

    // peek relative to the top of the stack
    value peek(stack_address offset=0) const;
    // get a local value from the current call frame
    value local(local_address l) const;
    // set a local_address value
    void set_local(local_address l, value v);
    // set a stack value from the top (cannot exceed current frame)
    void set_from_top(local_address l, value v);

    // internalize a symbol by name
    value get_symbol(const string& name);

    void add_global(value name, value v);
    value get_global(value name);
    void add_macro(value name, value v);
    value get_macro(value name);

    // attempt an import without escaping to interpreter
    optional<value> try_import(symbol_id ns_id);
    // perform an import using the top of the stack as the id. If the target
    // namespace is not already loaded, then this will cause execution to halt
    // with the waiting_for_import status.
    void do_import();

    // stack operations
    void pop();
    // this pins the top of the stack through the given working_set before
    // popping it
    value pop_to_ws(working_set* ws);
    void pop_times(stack_address n);
    void push(value v);

    // helper for arrange_call_stack. Takes the keyword table from the stack,
    // kw_tab, and returns a table matching call stack positions to values from
    // the table. num_args is number of arguments in the call. The var_table
    // argument will be used to hold unrecognized/duplicate arguments if the
    // provided function_stub supports a variadic table argument. (Otherwise
    // such arguments cause a runtime error).
    table<local_address,value> process_kw_table(function_stub* stub,
            local_address num_args,
            value kw_tab,
            value var_table);
    // helper for call. This arranges the arguments on the stack after a
    // function call. It expects the call arguments to be on top of the stack.
    void arrange_call_stack(working_set* ws,
            function* func,
            local_address num_args);
    // returns the next addr to go to
    code_address call(working_set* ws, local_address num_args);

    // set up a newly created function (including taking init values off the
    // stack)
    void init_function(working_set* ws, function* obj);

public:
    // initialize the virtual machine
    vm_thread(allocator* use_alloc, global_env* use_globals,
            code_chunk* use_chunk);
    ~vm_thread();

    vm_status check_status() const;
    const string& get_error_message() const;
    value get_pending_import_id() const;

    // step a single instruction
    void step();
    // execute instructions (stops if the end of the generated bytecode is reached)
    void execute();
    // get the instruction pointer
    code_address get_ip() const;

    // get the last popped value (null if there isn't any)
    value last_pop() const;

    code_chunk* cur_chunk() const;
    code_chunk* get_toplevel_chunk();
    allocator* get_alloc();
    symbol_table* get_symtab();

    // raise an exception of type fn_error containing the provided message
    void runtime_error(const string& msg) const;
};


// disassemble a single instruction, writing output to out
void disassemble_instr(const code_chunk& code, code_address ip, std::ostream& out);

void disassemble(const symbol_table& symtab, const code_chunk& code, std::ostream& out);


}

#endif
