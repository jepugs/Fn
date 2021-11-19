#ifndef __FN_LLIR_HPP
#define __FN_LLIR_HPP

#include "base.hpp"
#include "bytes.hpp"
#include "values.hpp"

#include <iostream>

namespace fn {

enum llir_tag {
    // global definition
    llir_def,
    // macro definition
    llir_defmacro,
    // dot form
    llir_dot,
    // function call
    llir_call,
    // constant lookup
    llir_const,
    // conditional
    llir_if,
    // function creation
    llir_fn,
    // namespace import
    llir_import,
    // mutation
    llir_set,
    // variable lookup
    llir_var,
    // sequence of expressions in a new lexical environment, returning last
    // result
    llir_with
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
// inheritance below, (llir_*_form is essentially a subclass of llir_form). This
// approach is slightly more flexible and doesn't use virtual methods, so I'll
// leave it for now.

struct llir_form {
    source_loc origin;
    llir_tag tag;
};

struct llir_def_form {
    llir_form header;
    symbol_id name;
    llir_form* value;
};
llir_def_form* mk_llir_def_form(const source_loc& origin,
        symbol_id name,
        llir_form* value,
        llir_def_form* dest=nullptr);
void clear_llir_def_form(llir_def_form* obj, bool recursive=true);
void free_llir_def_form(llir_def_form* obj, bool recursive=true);

struct llir_defmacro_form {
    llir_form header;
    symbol_id name;
    llir_form* macro_fun;
};
llir_defmacro_form* mk_llir_defmacro_form(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun,
        llir_defmacro_form* dest=nullptr);
void clear_llir_defmacro_form(llir_defmacro_form* obj, bool recursive=true);
void free_llir_defmacro_form(llir_defmacro_form* obj, bool recursive=true);

struct llir_dot_form {
    llir_form header;
    llir_form* obj;
    local_address num_keys;
    symbol_id* keys;
};
llir_dot_form* mk_llir_dot_form(const source_loc& origin,
        llir_form* obj,
        local_address num_keys,
        llir_dot_form* dest=nullptr);
void clear_llir_dot_form(llir_dot_form* obj, bool recursive=true);
void free_llir_dot_form(llir_dot_form* obj, bool recursive=true);

struct llir_call_form {
    llir_form header;
    llir_form* callee;
    local_address num_args;
    llir_form** args;
};
llir_call_form* mk_llir_call_form(const source_loc& origin,
        llir_form* callee,
        local_address num_args,
        llir_call_form* dest=nullptr);
void clear_llir_call_form(llir_call_form* obj, bool recursive=true);
void free_llir_call_form(llir_call_form* obj, bool recursive=true);

struct llir_if_form {
    llir_form header;
    llir_form* test_form;
    llir_form* then_form;
    llir_form* else_form;
};
llir_if_form* mk_llir_if_form(const source_loc& origin,
        llir_form* test_form,
        llir_form* then_form,
        llir_form* else_form,
        llir_if_form* dest=nullptr);
void clear_llir_if_form(llir_if_form* obj, bool recursive=true);
void free_llir_if_form(llir_if_form* obj, bool recursive=true);

struct llir_const_form {
    llir_form header;
    constant_id id;
};
llir_const_form* mk_llir_const_form(const source_loc& origin,
        constant_id id,
        llir_const_form* dest=nullptr);
void free_llir_const_form(llir_const_form* obj);

struct llir_fn_params {
    // positional arguments
    local_address num_pos_args;
    symbol_id* pos_args;
    // variadic table and list args
    bool has_var_list_arg;
    symbol_id var_list_arg;
    bool has_var_table_arg;
    symbol_id var_table_arg;
    // number of required args
    local_address req_args;
    // init forms for optional args
    llir_form** init_forms;
};

struct llir_fn_form {
    llir_form header;
    llir_fn_params params;
    llir_form* body;
};
llir_fn_form* mk_llir_fn_form(const source_loc& origin,
        local_address num_pos_args,
        bool has_var_list_arg,
        bool has_var_table_arg,
        local_address req_args,
        llir_form* body,
        llir_fn_form* dest=nullptr);
// note: this takes ownership of the pointers in params
llir_fn_form* mk_llir_fn_form(const source_loc& origin,
        llir_fn_params params,
        llir_form* body,
        llir_fn_form* dest=nullptr);
void clear_llir_fn_form(llir_fn_form* obj, bool recursive=true);
void free_llir_fn_form(llir_fn_form* obj, bool recursive=true);

struct llir_import_form {
    llir_form header;
    symbol_id target;
    bool has_alias;
    symbol_id alias;
    bool unqualified;
};
llir_import_form* mk_llir_import_form(const source_loc& origin,
        symbol_id target,
        llir_import_form* dest=nullptr);
void free_llir_import_form(llir_import_form* obj);

struct llir_set_form {
    llir_form header;
    llir_form* target;
    llir_form* value;
};
llir_set_form* mk_llir_set_form(const source_loc& origin,
        llir_form* target,
        llir_form* value,
        llir_set_form* dest=nullptr);
void clear_llir_set_form(llir_set_form* obj, bool recursive=true);
void free_llir_set_form(llir_set_form* obj, bool recursive=true);

struct llir_var_form {
    llir_form header;
    symbol_id name;
};
llir_var_form* mk_llir_var_form(const source_loc& origin,
        symbol_id name,
        llir_var_form* dest=nullptr);
void free_llir_var_form(llir_var_form* obj);

struct llir_with_form {
    llir_form header;
    local_address num_vars;
    symbol_id* vars;
    llir_form** value_forms;
    u32 body_length;
    llir_form** body;
};
llir_with_form* mk_llir_with_form(const source_loc& origin,
        local_address num_vars,
        u32 body_length,
        llir_with_form* dest=nullptr);
void clear_llir_with_form(llir_with_form* obj, bool recursive=true);
void free_llir_with_form(llir_with_form* obj, bool recursive=true);

void clear_llir_form(llir_form* obj, bool recursive=true);
void free_llir_form(llir_form* obj, bool recursive=true);

llir_form* copy_llir_form(llir_form* src, llir_form* dest=nullptr);

// for debugging/testing
void print_llir(llir_form* f, symbol_table& st, code_chunk* chunk);

}

#endif
