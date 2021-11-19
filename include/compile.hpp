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

struct compile_error {
    bool has_error;
    source_loc origin;
    string message;
};

struct compiler {
private:
    symbol_table* symtab;
    allocator* alloc;
    code_chunk* dest;

    // Find a local variable. An upvalue is created in the enclosing lex
    // structure if necessary. *is_upval is set to true if this is an upvalue
    // (indirect reference), false otherwise.
    optional<local_address> find_local(lexical_env* lex,
            bool* is_upval,
            symbol_id name);

    void write_byte(u8 byte);
    void write_short(u16 u);
    void patch_short(u16 u, code_address where);

    void compile_llir(const llir_call_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_const_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_def_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_defmacro_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_dot_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_if_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_fn_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_import_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_set_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_var_form* llir,
            lexical_env* lex,
            compile_error* err);
    void compile_llir(const llir_with_form* llir,
            lexical_env* lex,
            compile_error* err);

    void compile_llir_generic(const llir_form* llir,
            lexical_env* lex,
            compile_error* err);

public:
    compiler(symbol_table* use_symtab, allocator* use_alloc, code_chunk* dest)
        : symtab{use_symtab}
        , alloc{use_alloc}
        , dest{dest} {
    }
    compiler(const compiler& c) = delete;
    compiler& operator=(const compiler& c) = delete;

    // compile a single expression. Returns false on failure.
    void compile(const llir_form* expr, compile_error* err);
};

}
#endif
