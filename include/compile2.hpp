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

struct local_table {
    // table of local variable locations
    table<symbol_id,u8> vars;
    // parent environment
    local_table* parent;

    // the function we're currently compiling. this is needed to keep track of upvalues
    func_stub* cur_func;

    // stack pointer
    u8 sp;

    local_table(local_table* parent=nullptr, func_stub* cur_func=nullptr);
    // add an upvalue which has the specified number of levels of indirection (each level corresponds
    // to one more enclosing function before)
    u8 add_upvalue(u32 levels, u8 pos);
};

class compiler {
private:
    virtual_machine* vm;
    fn_scan::scanner* sc;

    void compile_subexpr(local_table& locals, const fn_parse::ast_node* expr);

    // Find a local variable. An upvalue is created in the enclosing locals
    // structure if necessary. *is_upval is set to true if this is an upvalue
    // (indirect reference), false otherwise.
    optional<u8> find_local(local_table& locals, bool* is_upval, symbol_id name);

    void constant(const_id id) {
        vm->get_bytecode().write_byte(fn_bytes::OP_CONST);
        vm->get_bytecode().write_short(id);
    }

    void write_byte(u8 byte) {
        vm->get_bytecode().write_byte(byte);
    }
    void write_short(u16 u) {
        vm->get_bytecode().write_short(u);
    }
    void patch_short(bc_addr where, u16 u) {
        vm->get_bytecode().patch_short(where, u);
    }
    bc_addr cur_addr() {
        return vm->get_bytecode().get_size();
    }

    inline bytecode& get_bytecode() {
        return vm->get_bytecode();
    }
    inline symbol_table& get_symtab() {
        return vm->get_symtab();
    }

    void compile_atom(local_table& locals,
                      const fn_parse::ast_atom& atom,
                      const source_loc& loc);
    void compile_var(local_table& locals, symbol_id id, const source_loc& loc);
    void compile_list(local_table& locals,
                      const vector<fn_parse::ast_node*>& list,
                      const source_loc& loc);
    void compile_call(local_table& locals, const vector<fn_parse::ast_node*>& list);

    void compile_and(local_table& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);
    void compile_cond(local_table& locals,
                      const vector<fn_parse::ast_node*>& list,
                      const source_loc& loc);
    void compile_def(local_table& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);
    void compile_defmacro(local_table& locals,
                          const vector<fn_parse::ast_node*>& list,
                          const source_loc& loc);
    void compile_defn(local_table& locals,
                      const vector<fn_parse::ast_node*>& list,
                      const source_loc& loc);
    void compile_do(local_table& locals,
                    const vector<fn_parse::ast_node*>& list,
                    const source_loc& loc);
    void compile_dollar_fn(local_table& locals,
                           const vector<fn_parse::ast_node*>& list,
                           const source_loc& loc);
    void compile_dot(local_table& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);
    void compile_fn(local_table& locals,
                    const vector<fn_parse::ast_node*>& list,
                    const source_loc& loc);
    void compile_if(local_table& locals,
                    const vector<fn_parse::ast_node*>& list,
                    const source_loc& loc);
    void compile_import(local_table& locals,
                        const vector<fn_parse::ast_node*>& list,
                        const source_loc& loc);
    void compile_let(local_table& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);
    void compile_letfn(local_table& locals,
                       const vector<fn_parse::ast_node*>& list,
                       const source_loc& loc);
    void compile_or(local_table& locals,
                    const vector<fn_parse::ast_node*>& list,
                    const source_loc& loc);
    void compile_quasiquote(local_table& locals,
                            const vector<fn_parse::ast_node*>& list,
                            const source_loc& loc);
    void compile_quote(local_table& locals,
                       const vector<fn_parse::ast_node*>& list,
                       const source_loc& loc);
    void compile_unquote(local_table& locals,
                         const vector<fn_parse::ast_node*>& list,
                         const source_loc& loc);
    void compile_unquote_splicing(local_table& locals,
                                  const vector<fn_parse::ast_node*>& list,
                                  const source_loc& loc);
    void compile_set(local_table& locals,
                     const vector<fn_parse::ast_node*>& list,
                     const source_loc& loc);
    void compile_with(local_table& locals,
                      const vector<fn_parse::ast_node*>& list,
                      const source_loc& loc);
public:

    compiler(virtual_machine* vm, fn_scan::scanner* sc)
        : vm{vm}
        , sc{sc} {
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
