#ifndef __FN_LLIR_HPP
#define __FN_LLIR_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {

enum llir_tag {
    // indicates illegal syntax
    llir_error,
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
    // function creation
    llir_fn,
    // namespace import
    llir_import,
    // table mutation
    llir_set_key,
    // local var mutation
    llir_set_var,
    // variable lookup
    llir_var,
    // sequence of expressions in a new lexical environment, returning last
    // result
    llir_with
};

struct llir_form {
    source_loc origin;
    llir_tag tag;
};

struct llir_error_obj {
    llir_form header;
    string desc;
};
llir_error_obj* mk_llir_error(const source_loc& origin,
        const string& desc,
        llir_error_obj* dest=nullptr);
void clear_llir_error(llir_error_obj* obj);
void free_llir_error(llir_error_obj* obj);

struct llir_def_obj {
    llir_form header;
    symbol_id name;
    llir_form* value;
};
llir_def_obj* mk_llir_def(const source_loc& origin,
        symbol_id name,
        llir_form* value,
        llir_def_obj* dest=nullptr);
void clear_llir_def(llir_def_obj* obj, bool recursive=true);
void free_llir_def(llir_def_obj* obj, bool recursive=true);

struct llir_defmacro_obj {
    llir_form header;
    symbol_id name;
    llir_form* macro_fun;
};
llir_defmacro_obj* mk_llir_defmacro(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun,
        llir_defmacro_obj* dest=nullptr);
void clear_llir_defmacro(llir_defmacro_obj* obj, bool recursive=true);
void free_llir_defmacro(llir_defmacro_obj* obj, bool recursive=true);

struct llir_dot_obj {
    llir_form header;
    llir_form* obj;
    local_address num_keys;
    symbol_id* keys;
};
llir_dot_obj* mk_llir_dot(const source_loc& origin,
        llir_form* obj,
        local_address num_keys,
        llir_dot_obj* dest=nullptr);
void clear_llir_dot(llir_dot_obj* obj, bool recursive=true);
void free_llir_dot(llir_dot_obj* obj, bool recursive=true);

struct llir_call_obj {
    llir_form header;
    llir_form* caller;
    local_address num_args;
    llir_form** args;
};
llir_call_obj* mk_llir_call(const source_loc& origin,
        llir_form* caller,
        local_address num_args,
        llir_call_obj* dest=nullptr);
void clear_llir_call(llir_call_obj* obj, bool recursive=true);
void free_llir_call(llir_call_obj* obj, bool recursive=true);

struct llir_const_obj {
    llir_form header;
    constant_id id;
};
llir_const_obj* mk_llir_const(const source_loc& origin,
        constant_id id,
        llir_const_obj* dest=nullptr);
void free_llir_const(llir_const_obj* obj);

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

struct llir_fn_obj {
    llir_form header;
    llir_fn_params params;
    llir_form* body;
};
llir_fn_obj* mk_llir_fn(const source_loc& origin,
        local_address num_pos_args,
        bool has_var_list_arg,
        bool has_var_table_arg,
        local_address req_args,
        llir_form* body,
        llir_fn_obj* dest=nullptr);
void clear_llir_fn(llir_fn_obj* obj, bool recursive=true);
void free_llir_fn(llir_fn_obj* obj, bool recursive=true);

struct llir_import_obj {
    llir_form header;
    symbol_id target;
    bool has_alias;
    symbol_id alias;
    bool unqualified;
};
llir_import_obj* mk_llir_import(const source_loc& origin,
        symbol_id target,
        llir_import_obj* dest=nullptr);
void clear_llir_import(llir_import_obj* obj, bool recursive=true);
void free_llir_import(llir_import_obj* obj, bool recursive=true);

struct llir_set_key_obj {
    llir_form header;
    llir_form* target;
    llir_form* key;
    llir_form* value;
};
llir_set_key_obj* mk_llir_set_key(const source_loc& origin,
        llir_form* target,
        llir_form* key,
        llir_form* value,
        llir_set_key_obj* dest=nullptr);
void clear_llir_set_key(llir_set_key_obj* obj, bool recursive=true);
void free_llir_set_key(llir_set_key_obj* obj, bool recursive=true);

struct llir_set_var_obj {
    llir_form header;
    symbol_id var;
    llir_form* value;
};
llir_set_var_obj* mk_llir_set(const source_loc& origin,
        symbol_id var,
        llir_form* value,
        llir_set_var_obj* dest=nullptr);
void clear_llir_set_var(llir_set_var_obj* obj, bool recursive=true);
void free_llir_set_var(llir_set_var_obj* obj, bool recursive=true);

struct llir_var_obj {
    llir_form header;
    symbol_id name;
};
llir_var_obj* mk_llir_var(const source_loc& origin,
        symbol_id name,
        llir_var_obj* dest=nullptr);
void free_llir_var(llir_var_obj* obj);

struct llir_with_obj {
    llir_form header;
    local_address num_vars;
    symbol_id* vars;
    llir_form** value_forms;
    llir_form* body;
};
llir_with_obj* mk_llir_with(const source_loc& origin,
        local_address num_vars,
        llir_form* body,
        llir_with_obj* dest=nullptr);
void clear_llir_with(llir_with_obj* obj, bool recursive=true);
void free_llir_with(llir_with_obj* obj, bool recursive=true);

void clear_llir_form(llir_form* obj, bool recursive=true);
void free_llir_form(llir_form* obj, bool recursive=true);

}

#endif
