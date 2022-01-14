#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include "allocator.hpp"
#include "base.hpp"
#include "bytes.hpp"
#include "llir.hpp"
#include "parse.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"

namespace fn {

struct lexical_env {
    // table of local variable locations
    table<symbol_id,local_address> vars;
    // table of upvalue IDs. (These only go on call frames).
    table<symbol_id,local_address> upvals;

    // parent environment
    lexical_env* parent = nullptr;

    // true value means this is the base frame of a function call.
    bool is_call_frame = false;
    // the stub for the function we're in (nullptr at the top level)
    function_stub* enclosing_func = nullptr;

    // stack pointer
    u8 sp = 0;
    // base pointer of the current function relative to enclosing one
    u8 bp = 0;
};

// Create a new lexical environment with the given parent. A non-null function
// will cause a call frame to be created. The stack and base pointer are set
// based on the parent and on whether or not new_func is null.
lexical_env extend_lex_env(lexical_env* parent, function_stub* new_func=nullptr);

// compile the provided form to a function
void compile(vm_thread* vm, llir_form* llir);

// compiler internal exception type.
class compiler_exception : public std::exception {
public:
    const char* what() const noexcept override {
        return "compiler_exception. Something went wrong internally :(";
    }

};

struct compiler {
private:
    function_stub* dest;
    table<value, constant_id> constant_table;
    fault* err;

    // Find a local variable. An upvalue is created in the enclosing lex
    // structure if necessary. *is_upval is set to true if this is an upvalue
    // (indirect reference), false otherwise.
    optional<local_address> find_local(lexical_env* lex,
            bool* is_upval,
            symbol_id name);

    void write_byte(u8 byte);
    void write_short(u16 u);
    void patch_short(u16 u, code_address where);
    // patch in a jump address.
    void patch_jump(i64 offset,
            code_address where,
            const source_loc& origin);

    // add a symbol as a constant and compile it. Unlike other compilation
    // functions, this does not affect the stack pointer.
    void compile_symbol(symbol_id sym);

    // TODO: add tail apply
    void compile_apply(const llir_apply* llir,
            lexical_env* lex,
            bool tail);
    void compile_call(const llir_call* llir,
            lexical_env* lex,
            bool tail);
    void compile_const(const llir_const* llir,
            lexical_env* lex);
    void compile_def(const llir_def* llir,
            lexical_env* lex);
    void compile_defmacro(const llir_defmacro* llir,
            lexical_env* lexn);
    void compile_dot(const llir_dot* llir,
            lexical_env* lex);
    void compile_if(const llir_if* llir,
            lexical_env* lex,
            bool tail);
    void compile_fn(const llir_fn* llir,
            lexical_env* lex);
    void compile_import(const llir_import* llir,
            lexical_env* lex);
    void compile_set(const llir_set* llir,
            lexical_env* lex);
    void compile_var(const llir_var* llir,
            lexical_env* lex);
    void compile_with(const llir_with* llir,
            lexical_env* lex,
            bool tail);


    void c_fault(const source_loc& origin, const string& message);

public:
    compiler() { };
    compiler(const compiler& c) = delete;
    compiler& operator=(const compiler& c) = delete;

    void compile_llir_generic(const llir_form* llir,
            lexical_env* lex,
            bool tail=false);
};

}
#endif
