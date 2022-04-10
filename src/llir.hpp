#ifndef __FN_HIR_HPP
#define __FN_HIR_HPP

#include "array.hpp"
#include "base.hpp"
#include "bytes.hpp"
#include "values.hpp"

#include <iostream>

namespace fn {

namespace hir {

enum hir_kind {
    // constant
    hk_const,
    // global definition
    hk_def,
    // macro definition
    hk_defmacro,
    // sequence of expressions in a new lexical environment, returning last
    // result
    hk_do,
    // function call
    hk_call,
    // conditional
    hk_if,
    // function creation
    hk_fn,
    // namespace import
    hk_import,
    // mutation
    hk_set,
    // unquoted symbol
    hk_symbol
};

// hir::node is a header class present in all HIR data structures.
struct node {
    hir_kind kind;
    source_loc loc;
};

struct hconst {
    node h;
    constant_id id;
};

struct hdef {
    node h;
    symbol_id name;
    node* value;
};

struct hdefmacro {
    node h;
    symbol_id name;
    hfn* value;
};

struct hdo {
    node h;
    dyn_array<node*> body;
};

struct hcall {
    node h;
    node* op;
    dyn_array<node*> args;
};

struct hif {
    node h;
    node* test;
    node* then;
    node* elce;
};

struct hfn {
    node h;
    constant_id fid;
};

struct himport {
    node h;
    symbol_id ns_name;
    symbol_id alias;
};

struct hset {
    node h;
    node* target;
    node* alias;
};

struct hsymbol {
    node h;
    symbol_id id;
};

// free an entire hir graph
void free_graph(node* root);

} // end namespace fn::hir

// NOTE: (on the mk_/clear_/free_ functions below):
// - mk_* functions will allocate new memory if dest=nullptr, otherwise
//   they'll initialize a structure at the place provided.
// - clear_* functions delete all the fields of a structure (but not
//   the structure itself). If recursive=true, this also frees (not just
//   clears) all subordinate hir_form* instances.
// - free_* functions clear a structure and also delete the pointer. If
//   recursive=true, then this frees all subordinate hir_form* instances.

// Upon further inspection, I appear to have independently reinvented
// inheritance below, (hir_* is essentially a subclass of hir_form). This
// approach is slightly more flexible and doesn't use virtual methods, so I'll
// leave it for now.

struct hir_form {
    source_loc origin;
    hir_tag tag;
};

struct hir_apply {
    hir_form header;
    hir_form* callee;
    // number of arguments includes the required list and table
    local_address num_args;
    hir_form** args;
};
hir_apply* mk_hir_apply(const source_loc& origin,
        hir_form* callee,
        local_address num_args);
void free_hir_apply(hir_apply* obj);

struct hir_call {
    hir_form header;
    hir_form* callee;
    local_address num_args;
    hir_form** args;
};
hir_call* mk_hir_call(const source_loc& origin,
        hir_form* callee,
        local_address num_args);
void free_hir_call(hir_call* obj);

struct hir_const {
    hir_form header;
    constant_id id;
};
hir_const* mk_hir_const(const source_loc& origin, constant_id id);
void free_hir_const(hir_const* obj);

struct hir_def {
    hir_form header;
    symbol_id name;
    hir_form* value;
};
hir_def* mk_hir_def(const source_loc& origin, symbol_id name,
        hir_form* value);
void free_hir_def(hir_def* obj);

struct hir_defmacro {
    hir_form header;
    symbol_id name;
    hir_form* macro_fun;
};
hir_defmacro* mk_hir_defmacro(const source_loc& origin,
        symbol_id name,
        hir_form* macro_fun);
void free_hir_defmacro(hir_defmacro* obj);

struct hir_if {
    hir_form header;
    hir_form* test;
    hir_form* then;
    hir_form* elce;
};
hir_if* mk_hir_if(const source_loc& origin,
        hir_form* test,
        hir_form* then,
        hir_form* elce);
void free_hir_if(hir_if* obj);

struct hir_fn {
    hir_form header;
    constant_id fun_id;
    // number of optional args
    local_address num_opt;
    // init forms for the optional args
    hir_form** inits;
};
hir_fn* mk_hir_fn(const source_loc& origin,
        constant_id fun_id,
        local_address num_opt);
void free_hir_fn(hir_fn* obj);

struct hir_import {
    hir_form header;
    symbol_id target;
    bool has_alias;
    symbol_id alias;
};
hir_import* mk_hir_import(const source_loc& origin,
        symbol_id target);
hir_import* mk_hir_import(const source_loc& origin,
        symbol_id target, symbol_id alias);
void free_hir_import(hir_import* obj);

struct hir_set {
    hir_form header;
    hir_form* target;
    hir_form* value;
};
hir_set* mk_hir_set(const source_loc& origin,
        hir_form* target,
        hir_form* value);
void free_hir_set(hir_set* obj);

struct hir_var {
    hir_form header;
    symbol_id name;
};
hir_var* mk_hir_var(const source_loc& origin,
        symbol_id name);
void free_hir_var(hir_var* obj);

struct hir_with {
    hir_form header;
    local_address num_vars;
    symbol_id* vars;
    hir_form** values;
    u32 body_length;
    hir_form** body;
};
hir_with* mk_hir_with(const source_loc& origin,
        local_address num_vars,
        u32 body_length);
void free_hir_with(hir_with* obj);

void free_hir_form(hir_form* obj);

hir_form* copy_hir_form(hir_form* src);

// for debugging/testing
string print_hir(hir_form* f, symbol_table& st, dyn_array<value>* const_arr);

}

#endif
