#ifndef __FN_COMPILE2_HPP
#define __FN_COMPILE2_HPP

#include "base.hpp"
#include "bytes.hpp"
#include "scan.hpp"
#include "parse.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

namespace fn {

struct locals {
    // table of local variable locations
    table<symbol_id,u8> vars;
    // parent environment
    locals* parent;

    // the function we're currently compiling. this is needed to keep track of upvalues
    func_stub* cur_func;

    // stack pointer
    u8 sp;

    locals(locals* parent=nullptr, func_stub* cur_func=nullptr);
    // add an upvalue which has the specified number of levels of indirection (each level corresponds
    // to one more enclosing function before)
    u8 add_upvalue(u32 levels, u8 pos);
};

class compiler {
private:
    bytecode* dest;
    fn_scan::scanner* sc;
    symbol_table* symtab;

    void compile_subexpr(locals& locals, const fn_parse::ast_node* expr);

    void constant(const_id id) {
        dest->write_byte(fn_bytes::OP_CONST);
        dest->write_short(id);
    }
    // Find a local variable. An upvalue is created in the enclosing locals
    // structure if necessary. *is_upval is set to true if this is an upvalue
    // (indirect reference), false otherwise.
    optional<u8> find_local(locals& locals, bool* is_upval, symbol_id name);

    void compile_atom(locals& locals,
                      const fn_parse::ast_atom& atom,
                      const source_loc& loc);
    void compile_var(locals& locals, symbol_id id, const source_loc& loc);
    void compile_list(locals& locals,
                      const vector<fn_parse::ast_node*>& list,
                      const source_loc& loc);

    void compile_do(locals& locals,
                    const vector<fn_parse::ast_node*>& list,
                    const source_loc& loc);
    void compile_def(locals& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);
    void compile_let(locals& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);


public:
    compiler(bytecode* dest, fn_scan::scanner* sc, symbol_table* symtab)
        : dest(dest)
        , sc(sc)
        , symtab(symtab) {
    }
    ~compiler() {
    }

    // compile a single expression
    void compile_expr();
    // compile until eof is reached
    void compile_to_eof();

    inline void error(const char* msg, const source_loc& loc) {
        throw fn_error("parser", msg, loc);
    }};
}

#endif
