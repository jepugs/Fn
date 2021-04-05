#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include <filesystem>

#include "base.hpp"
#include "bytes.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

namespace fn {

using namespace fn_scan;

namespace fs = std::filesystem;

// locals object tracks all state
struct locals {
    // table of local variable locations
    table<string,local_addr> vars;
    // parent environment
    locals* parent;

    // the function we're currently compiling. this is needed to keep track of upvalues
    func_stub* cur_func;

    locals(locals* parent=nullptr, func_stub* cur_func=nullptr);
    // add an upvalue which has the specified number of levels of indirection (each level corresponds
    // to one more enclosing function before)
    u8 add_upvalue(u32 levels, u8 pos);
};

class compiler {
private:
    bytecode* dest;
    scanner* sc;
    // compiler's internally-tracked stack pointer
    u8 sp;

    // compiler working directory. this is used as an import path.
    fs::path dir;
    // table of imported modules. associates a sequence of strings (i.e. the symbols in a dot
    // expression) to a constant containing that module's i_d.
    table<vector<string>,u16> modules;
    // constant holding the current module's i_d
    u16 cur_mod_id;

    // search for the path to a module given a vector denoting the (dot-separated) components of its
    // name.
    fs::path module_path(const vector<string>& id);

    // compile a single expression, consuming tokens. t0 is an optional first token. running will
    // leave the expression on top of the stack.
    void compile_expr(locals* l, token* t0=nullptr);

    // compile a sequence of expressions terminated by ')', creating a new lexical scope
    void compile_block(locals* l);

    // special forms
    void compile_and(locals* l);
    void compile_apply(locals* l);
    void compile_cond(locals* l);
    void compile_def(locals* l);
    void compile_do(locals* l);
    void compile_dot_expr(locals* l);
    void compile_dot_token(locals* l, token& tok);
    void compile_fn(locals* l);
    void compile_if(locals* l);
    void compile_import(locals* l); // t_od_o
    void compile_let(locals* l);
    void compile_or(locals* l);
    void compile_quote(locals* l, bool prefix);
    void compile_set(locals* l);

    // braces and brackets
    void compile_braces(locals* l);
    void compile_brackets(locals* l);

    // parentheses
    void compile_call(locals* l, token* t0);

    // variables

    // find a local variable by its name. returns std::nullopt when no global variable is found,
    // otherwise the corresponding local value (i.e. stack position or upvalue i_d). the value
    // pointed to by levels will be set to the number of layers of enclosing functions that need to
    // be visited to access the variable, thus a value of 0 indicates a local variable on the stack
    // while a value greater than 0 indicates an upvalue.
    optional<local_addr> find_local(locals* l, const string& name, u32* levels);
    // compile a variable reference
    void compile_var(locals* l, const string& name);

    // helpers functions

    // note: this doesn't update the stack pointer
    inline void constant(u16 id) {
        dest->write_byte(fn_bytes::OP_CONST);
        dest->write_short(id);
    }
    // attempt to parse a name, i.e. a symbol, a dot form, or a dot token. returns a vector
    // consisting of the names of its constitutent symbols.
    vector<string> tokenize_name(optional<token> t0={ });

public:
    compiler(const fs::path& dir, bytecode* dest, scanner* sc=nullptr);
    ~compiler();
    // compile all scanner input until e_of. running the generated code should leave the interpreter
    // stack empty, with last_pop() returning the result of the final toplevel expression
    void compile();
    // compile the contents of the specified file (in place). this doesn't affect the current
    // module, so if this file corresponds to a new module, that must be set up ahead of time.
    void compile_file(const fs::path& filename);
    void compile_file(const string& filename);

    // set a new scanner
    void setscanner(scanner* sc);
};

// hash function used by the compiler for module i_ds
template<> u32 hash<vector<string>>(const vector<string>& v);

}

#endif
