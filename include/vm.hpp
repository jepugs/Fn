// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "allocator.hpp"
#include "base.hpp"
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
    // chunk to return to
    code_chunk* ret_chunk;
    // base pointer (i.e. offset from the true bottom of the stack)
    u32 bp;
    // the function we're in. nullptr on the top level.
    function* caller;
    // the number of arguments we need to pop after exiting the current call
    local_address num_args;
    // since this is the main reason we use the callee, it makes sense to put it
    // here directly.
    upvalue_cell** upvals;

    call_frame(call_frame* prev,
               code_address ret_addr,
               code_chunk* ret_chunk,
               u32 bp,
               function* caller,
               local_address num_args=0)
        : prev{prev}
        , ret_addr{ret_addr}
        , ret_chunk{ret_chunk}
        , bp{bp}
        , caller{caller}
        , num_args{num_args} {
        if (caller) {
            upvals = caller->upvals;
        }
    }
};

// When a vm_thread finishes executing, it will leave behind some exit state,
// which can indicate that it's done, that it requires an import, etc.

// possible statuses for a vm_thread
enum vm_status {
    vs_stopped,
    vs_running,
    vs_waiting_for_import,
    vs_fault
};


// WARNING: despite the name, vm_threads cannot truly be run in parallel (until
// the allocator and global_env are made threadsafe).

// vm_thread represents a single thread of the interpreter, so it has its own
// instruction pointer, stack, etc. This is where the bytecode execution logic
// is.
struct vm_thread {
    friend class fn_handle;
private:
    // These are weak references to objects maintained by the interpreter.
    symbol_table* symtab;
    global_env* globals;
    allocator* alloc;
    code_chunk* chunk;
    fault* err;

    // current execution status
    vm_status status;
    // set when the execution status is set to vs_error
    string error_message;
    // set when the execution status is vs_waiting_for_import
    symbol_id pending_import_id;

    // instruction pointer and stack
    code_address ip;
    call_frame* frame;
    root_stack* stack;


    // stack operations
    // pop the top of the stack
    void pop();
    // pins the value using ws before popping it
    value pop_to_ws(working_set* ws);
    // pop multiple times
    void pop_times(stack_address n);
    // push to the top of the stack
    void push(value v);
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

    // manipulate global variables
    void add_global(value name, value v);
    value get_global(value name);
    void add_macro(value name, value v);
    value get_macro(value name);
    value by_guid(value name);

    // attempt an import without escaping to interpreter
    optional<value> try_import(symbol_id ns_id);
    // perform an import using the top of the stack as the id. If the target
    // namespace is not already loaded, then this will cause execution to halt
    // with the waiting_for_import status.
    void do_import();

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
    // Version of arrange_call_stack() which doesn't process keyword arguments.
    void arrange_call_stack_no_kw(working_set* ws,
            function* func,
            local_address num_args);
    // Called when a function is called, after arranging the function arguments
    // on the stack. This creates the new call frame and returns the address to
    // jump to for the call.
    code_address make_call(working_set* ws, function* func);
    // Analogue to make_call() but for tail calls. This means the current call
    // frame is replaced rather than extended.
    code_address make_tcall(function* func);
    // returns the next addr to go to. num_args does not count the function or
    // the keyword table.
    code_address call_kw(local_address num_args);
    // like call, but replaces the current call frame rather than creating a new
    // one. Effectively it's call + return in a single instruction
    code_address tcall_kw(local_address num_args);
    // like call and tcall, but these are faster because they assume there's no
    // keyword table and hence do no keyword processing.
    code_address call_no_kw(local_address num_args);
    code_address tcall_no_kw(local_address num_args);
    // num_args does not count the function, the keyword table, or the argument
    // list.
    code_address apply(local_address num_args,bool tail=false);

    // set up a newly created function (including taking init values off the
    // stack)
    void init_function(working_set* ws, function* obj);

    // raise an exception of type fn_exception containing the provided message
    void runtime_error(const string& msg) const;

    // step a single instruction
    // Note: experimented with inline here and found no difference whether I
    // used -O2 or -O0. I'll leave it as a compiler hint anyway.
    inline void step();

public:
    // initialize the virtual machine
    vm_thread(allocator* use_alloc, global_env* use_globals,
            code_chunk* use_chunk);
    ~vm_thread();

    // checks the status of the virtual machine
    vm_status check_status() const;
    symbol_id get_pending_import_id() const;
    

    // execute instructions until a stopping condition occurs. Check status to
    // see what happened.
    void execute(fault* err);
    // get the instruction pointer
    code_address get_ip() const;

    // get the last popped value (null if there isn't any)
    value last_pop(working_set* ws) const;

    // accessors
    code_chunk* get_chunk() const;
    allocator* get_alloc() const;
    symbol_table* get_symtab() const;
    const root_stack* get_stack() const; // the stack is for looking, not touching

};


// disassemble a single instruction, writing output to out
void disassemble_instr(const code_chunk& code, code_address ip, std::ostream& out);

void disassemble(const symbol_table& symtab, const code_chunk& code, std::ostream& out);


}

#endif
