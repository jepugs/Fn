#ifndef __FN_LLIR_HPP
#define __FN_LLIR_HPP

#include "array.hpp"
#include "base.hpp"
#include "bytes.hpp"
#include "values.hpp"

#include <iostream>

namespace fn {

enum llir_tag {
    // apply operation
    lt_apply,
    // constant
    lt_const,
    // global definition
    lt_def,
    // macro definition
    lt_defmacro,
    // dot form
    lt_dot,
    // function call
    lt_call,
    // conditional
    lt_if,
    // function creation
    lt_fn,
    // namespace import
    lt_import,
    // mutation
    lt_set,
    // variable lookup
    lt_var,
    // sequence of expressions in a new lexical environment, returning last
    // result
    lt_with
};

// NOTE: (on the mk_/clear_/free_ functions below):
// - mk_* functions will allocate new memory if dest=nullptr, otherwise
//   they'll initialize a structure at the place provided.
// - clear_* functions delete all the fields of a structure (but not
//   the structure itself). If recursive=true, this also frees (not just
//   clears) all subordinate llir_form* instances.
// - free_* functions clear a structure and also delete the pointer. If
//   recursive=true, then this frees all subordinate llir_form* instances.

// Upon further inspection, I appear to have independently reinvented
// inheritance below, (llir_* is essentially a subclass of llir_form). This
// approach is slightly more flexible and doesn't use virtual methods, so I'll
// leave it for now.

struct llir_form {
    source_loc origin;
    llir_tag tag;
};

struct llir_apply {
    llir_form header;
    llir_form* callee;
    // number of arguments includes the required list and table
    local_address num_args;
    llir_form** args;
};
llir_apply* mk_llir_apply(const source_loc& origin,
        llir_form* callee,
        local_address num_args);
void free_llir_apply(llir_apply* obj);

struct llir_call {
    llir_form header;
    llir_form* callee;
    local_address num_args;
    llir_form** args;
};
llir_call* mk_llir_call(const source_loc& origin,
        llir_form* callee,
        local_address num_args);
void free_llir_call(llir_call* obj);

struct llir_const {
    llir_form header;
    constant_id id;
};
llir_const* mk_llir_const(const source_loc& origin, constant_id id);
void free_llir_const(llir_const* obj);

struct llir_def {
    llir_form header;
    symbol_id name;
    llir_form* value;
};
llir_def* mk_llir_def(const source_loc& origin, symbol_id name,
        llir_form* value);
void free_llir_def(llir_def* obj);

struct llir_defmacro {
    llir_form header;
    symbol_id name;
    llir_form* macro_fun;
};
llir_defmacro* mk_llir_defmacro(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun);
void free_llir_defmacro(llir_defmacro* obj);

struct llir_dot {
    llir_form header;
    llir_form* obj;
    symbol_id key;
};
llir_dot* mk_llir_dot(const source_loc& origin,
        llir_form* obj,
        symbol_id key);
void free_llir_dot(llir_dot* obj);

struct llir_if {
    llir_form header;
    llir_form* test;
    llir_form* then;
    llir_form* elce;
};
llir_if* mk_llir_if(const source_loc& origin,
        llir_form* test,
        llir_form* then,
        llir_form* elce);
void free_llir_if(llir_if* obj);

struct llir_fn {
    llir_form header;
    constant_id fun_id;
    // number of optional args
    local_address num_opt;
    // init forms for the optional args
    llir_form** inits;
};
llir_fn* mk_llir_fn(const source_loc& origin,
        constant_id fun_id,
        local_address num_opt);
void free_llir_fn(llir_fn* obj);

struct llir_import {
    llir_form header;
    symbol_id target;
    bool has_alias;
    symbol_id alias;
    bool unqualified;
};
llir_import* mk_llir_import(const source_loc& origin,
        symbol_id target);
void free_llir_import(llir_import* obj);

struct llir_set {
    llir_form header;
    llir_form* target;
    llir_form* value;
};
llir_set* mk_llir_set(const source_loc& origin,
        llir_form* target,
        llir_form* value);
void free_llir_set(llir_set* obj);

struct llir_var {
    llir_form header;
    symbol_id name;
};
llir_var* mk_llir_var(const source_loc& origin,
        symbol_id name);
void free_llir_var(llir_var* obj);

struct llir_with {
    llir_form header;
    local_address num_vars;
    symbol_id* vars;
    llir_form** values;
    u32 body_length;
    llir_form** body;
};
llir_with* mk_llir_with(const source_loc& origin,
        local_address num_vars,
        u32 body_length);
void free_llir_with(llir_with* obj);

void free_llir_form(llir_form* obj);

llir_form* copy_llir_form(llir_form* src);

// for debugging/testing
string print_llir(llir_form* f, symbol_table& st, dyn_array<value>* const_arr);

}

#endif
