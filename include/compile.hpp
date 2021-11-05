#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include "allocator.hpp"
#include "base.hpp"
#include "bytes.hpp"
#include "scan.hpp"
#include "parse.hpp"
#include "table.hpp"
#include "values.hpp"

namespace fn {

// TODO: rename (to local_environment unless I think of a better name)
struct local_table {
    // table of local variable locations
    table<symbol_id,u8> vars;
    // table of upvalue offsets
    table<symbol_id, i32> upvals;

    // parent environment
    local_table* parent;

    // Non-null value means this local environment is the base of a new
    // function.
    function_stub* enclosing_func;

    // stack pointer
    u8 sp;
    // base pointer of the current function relative to enclosing one
    u8 bp;

    // Construct a new local_table with the given parent. If new_func !=
    // nullptr, then the created local environment has its stack pointer reset.
    local_table(local_table* parent=nullptr, function_stub* new_func=nullptr);
};

struct compiler {
private:
    allocator* alloc;
    code_chunk* dest;

    void compile_subexpr(local_table& locals, const fn_parse::ast_node* expr);

    // Find a local variable. An upvalue is created in the enclosing locals
    // structure if necessary. *is_upval is set to true if this is an upvalue
    // (indirect reference), false otherwise.
    optional<u8> find_local(local_table& locals, bool* is_upval, symbol_id name);

    void write_byte(u8 byte);
    void write_short(u16 u);
    void patch_short(bc_addr where, u16 u);

    // these don't automatically adjust the stack pointer
    void compile_num(f64 num);
    void compile_sym(symbol_id id);
    void compile_string(const fn_string& id);
    void compile_quoted_form(const fn_parse::ast_node* form);

    // FIXME: should probably replace this
    symbol_table& get_symtab();

    void compile_atom(local_table& locals,
                      const fn_parse::ast_atom& atom,
                      const source_loc& loc);
    void compile_var(local_table& locals, symbol_id id, const source_loc& loc);
    // compile the object of a dot expression up to the last all_but keys
    void compile_dot_obj(local_table& locals,
                         const vector<fn_parse::ast_node*>& dot_expr,
                         u8 all_but,
                         const source_loc& loc);
    void compile_list(local_table& locals,
                      const vector<fn_parse::ast_node*>& list,
                      const source_loc& loc);
    void compile_call(local_table& locals, const vector<fn_parse::ast_node*>& list);
    void compile_body(local_table& locals,
                      const vector<fn_parse::ast_node*>& list,
                      u32 body_start);
    void compile_function(local_table& locals,
                          const fn_parse::param_list& params,
                          const vector<fn_parse::ast_node*>& body_vec,
                          u32 body_start);

    // Define a global function by directly specifying its bytecode. The
    // bytecode is responsible for calling return or making a tail call.
    void define_bytecode_function(const string& name,
                                  const vector<symbol_id>& positional,
                                  local_addr optional_index,
                                  bool var_list,
                                  bool var_table,
                                  vector<u8>& bytes);

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

    compiler(allocator* use_alloc, code_chunk* dest)
        : alloc{use_alloc}
        , dest{dest} {
    }

    ~compiler() {
    }

    // compile a single expression
    void compile_expr(fn_parse::ast_node* expr);

    inline void error(const char* msg, const source_loc& loc) {
        throw fn_error("compiler", msg, loc);
    }};
}

#endif
