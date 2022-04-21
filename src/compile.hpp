// compile.hpp -- routines to compile functions to bytecode
#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include "array.hpp"
#include "base.hpp"
#include "bytes.hpp"
#include "istate.hpp"
#include "namespace.hpp"
#include "obj.hpp"
#include "scan.hpp"
#include "table.hpp"

namespace fn {

// NOTE: (Bytecode Compiler Dependencies). With the important exception of
// macroexpansion, the bytecode compiler avoids any dependency on the garbage
// collection. This lets us go all the way from source code to compiled
// functions without having to touch the Fn allocator in any way. We have to go
// a tiny bit out of our way to accomplish this, but it simplifies things in the
// long run. The allocator has a routine called reify_function(), which converts
// bc_compiler.

// NOTE: (Parser Output Must Outlive Compiler Output). The bc_compiler_output
// object is *almost* self-contained. It includes weak references to the parser
// output used to generate it. In particular, it refers to the
// scanner_string_table and possibly some parts of the AST. Since compilation is
// always done right after parsing, this is not a big deal, but care must be
// taken to manage object lifetimes.

// encodes the type of a constant
enum bc_constant_kind {
    bck_number,
    bck_string,
    bck_symbol,
    bck_quoted
};

// used by the bytecode compiler to represent an entry in the constant table.
// These manage the lifetime of the associated ast::node* object
struct bc_output_const {
    bc_constant_kind kind;
    using datum = union {
        f64 num;
        sst_id str_id;
        ast::node* quoted;      // danger! this is a weak reference
    };
    datum d;

    bc_output_const(bc_constant_kind kind, datum d);
    bc_output_const(const bc_output_const& other);
    // explicit copy constructor is needed here since we have to drop ownership
    // of the ast::node
    bc_output_const(bc_output_const&& other) noexcept;
    bc_output_const& operator=(const bc_output_const&) = delete;
    bc_output_const& operator=(bc_output_const&&) = delete;
    ~bc_output_const();
};

// used by the bytecode compiler to represent a global variable
struct bc_output_global {
    // name of the global variable, before any resolution is done
    sst_id raw_name;
    // address in bytecode where the global ID is
    u32 patch_addr;
};

// data assembled by the bytecode compiler. This contains sufficient information
// for the allocator to initialize a function. Note that bc_compiler_output
// hangs on to references to the ast::node that originally generated the
// function (so quoted constants can be generated when the function is created)
struct bc_compiler_output {
    // the string table used to build this object (weak reference)
    scanner_string_table* sst;
    sst_id name_id;

    // code and constants
    dyn_array<u8> code;
    dyn_array<bc_output_global> globals;
    dyn_array<bc_output_const> const_table;
    dyn_array<bc_compiler_output> sub_funs;
    dyn_array<code_info> ci_arr;

    // stack space required
    u32 stack_required;

    // params info
    dyn_array<symbol_id> params;
    u8 num_opt;
    bool has_vari;
    symbol_id vari_param;

    // upvalues
    u8 num_upvals;
    // the next two arrays always have the same length
    dyn_array<u8> upvals;
    dyn_array<u8> upvals_direct;
};

// structure representing a local variable
struct lexical_var {
    // scanner_string_table id
    sst_id name;
    u8 index;
};

struct local_upvalue {
    // scanner_string_table id
    sst_id name;
    // direct upvalues are plucked right off the stack; indirect upvalues are
    // copied from the enclosing function
    bool direct;
    // index for a direct upvalue is its stack address relative to the bp of the
    // enclosing frame. For an indirect upvalue is its upvalue index in the
    // enclosing frame.
    u8 index;
};

class bc_compiler {
private:
    friend bool compile_to_bytecode(bc_compiler_output& out, istate* S,
            scanner_string_table& sst, const ast::node* root);

    // when compiling a function within a function, this is set to the compiler
    // for the enclosing function. It is used for lexical variable search.
    bc_compiler* parent;
    // istate used for macroexpansion
    istate* S;
    scanner_string_table* sst;
    // this is ordered by stack address
    dyn_array<lexical_var> vars;
    // values here are the upvalue IDs in the function
    table<sst_id, u8> upvals;
    u8 sp;

    // output from the compiler
    bc_compiler_output* output;

    // if parent is non-nil, this assumes that the top of the stack is holding
    // the parent function
    bc_compiler(bc_compiler* parent, istate* S, scanner_string_table& sst,
            bc_compiler_output& output);

    // get the function_stub from the top of the stack
    function_stub* get_stub() const;

    // pop off the variables with indices above base
    void pop_vars(u8 base);
    // add a new local variable at the current stack pointer. Does not increment
    // the stack pointer!
    void push_var(sst_id name);
    // attempt to find a function-local variable. Returns true on success and
    // sets index to the index of the variable
    bool find_local_var(u8& index, sst_id name);
    // attempt to find a variable in any enclosing functions
    bool find_upvalue_var(u8& index, sst_id name);

    // emit bytecode
    void emit8(u8 u);
    void emit16(u16 u);
    void emit32(u32 u);
    // update the source code location
    void update_source(const source_loc& loc);

    // update a 16-bit value at the given address
    void patch16(u16 u, u32 addr);

    // helper to process function argument lists.
    bool process_params(const ast::node* params, dyn_array<sst_id>& pos_params,
            dyn_array<ast::node*>& init_vals, bool& has_vari, sst_id& vari);

    // expand macros. macro_form must be a list of length >= 1
    ast::node* macroexpand(const ast::node* macro_form);

    // Compile special forms. Note that cond, defn, dollar-fn, letfn, and
    // quasiquote are not compiled directly here. These are implemented as
    // macros. Each root argument must be a symbol list of length >= 1
    // representing the relevant special form (although the operator is not
    // checked, as it is presumed to have been checked previously).
    bool compile_def(const ast::node* root);
    bool compile_defmacro(const ast::node* root);
    bool compile_do(const ast::node* root, bool tail);
    bool compile_do_inline(const ast::node* root, bool tail);
    bool compile_if(const ast::node* root, bool tail);
    bool compile_fn(const ast::node* root);
    bool compile_import(const ast::node* root);
    bool compile_let(const ast::node* root);
    bool compile_quote(const ast::node* root);
    bool compile_set(const ast::node* root);

    // these are technically functions, but we provide special compiler
    // optimizations for them
    bool compile_apply(const ast::node* apply, bool tail);
    bool compile_dot(const ast::node* root);
    bool compile_List(const ast::node* root);

    // compile a constant symbol
    bool compile_const_symbol(sst_id str_id);
    // compile a subordinate function. This involves creating a child
    // bc_compiler_output object
    bool compile_sub_fun(const ast::node* params, const ast::node** body,
            u32 body_len, const string& name);
    // compile a list whose operator is a symbol
    bool compile_symbol_list(const ast::node* root, bool tail);
    bool compile_call(const ast::node* root, bool tail);
    bool validate_let_form(const ast::node* expr);
    bool is_do_inline_form(const ast::node* node);
    bool is_dot_form(const ast::node* node);
    bool is_let_form(const ast::node* node);
    bool compile_body(const ast::node** exprs, u32 len, bool tail);
    // compile a form within a body. This accounts for let and do-inline forms.
    bool compile_within_body(const ast::node* expr, bool tail);

    bool compile_number(const ast::node* root);
    bool compile_string(const ast::node* root);
    bool compile_symbol(const ast::node* root);
    bool compile_list(const ast::node* root, bool tail);

    bool compile(const ast::node* exprs, bool tail);
    bool compile_function_body(const ast::node** exprs, u32 len);
    bool compile_toplevel(const ast::node* root);

    // set an error
    void compile_error(const source_loc& loc, const string& message);
};

// generate bc_compiler_output from the given ast. The generated object might
// contain weak references to the ast::node passed in, so take care about that.
bool compile_to_bytecode(bc_compiler_output& out, istate* S,
        scanner_string_table& sst, const ast::node* root);
// peek at the top of the stack, disassemble it, and push the result as a
// string. Decompiles subfunctions recursively if recur=true.
void disassemble_top(istate* S, bool recur=false);

}

#endif
