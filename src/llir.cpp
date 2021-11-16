#include "llir.hpp"

namespace fn {

llir_def_obj* mk_llir_def(const source_loc& origin,
        symbol_id name,
        llir_form* value,
        llir_def_obj* dest) {
    if (dest == nullptr) {
        dest = new llir_def_obj;
    }
    (*dest) = {
        .header={.origin=origin, .tag=llir_def},
        .name=name,
        .value=value};
    return dest;
}

llir_defmacro_obj* mk_llir_defmacro(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun,
        llir_defmacro_obj* dest) {
    if (dest == nullptr) {
        dest = new llir_defmacro_obj;
    }
    (*dest) = {
        .header={.origin=origin, .tag=llir_defmacro},
        .name=name,
        .macro_fun=macro_fun};
    return dest;
}

llir_dot_obj* mk_llir_dot(const source_loc& origin,
        llir_form* obj,
        local_address num_keys,
        llir_dot_obj* dest) {
    if (dest == nullptr) {
        dest = new llir_dot_obj;
    }
    (*dest) = {
        .header={.origin=origin, .tag=llir_dot},
        .obj=obj,
        .num_keys=num_keys,
        .keys=new symbol_id[num_keys]
    };
    return dest;
}

llir_call_obj* mk_llir_call(const source_loc& origin,
        llir_form* caller,
        local_address num_args,
        llir_call_obj* dest) {
    if (dest == nullptr) {
        dest = new llir_call_obj;
    }
    return new (dest) llir_call_obj {
        .header={.origin=origin, .tag=llir_call},
        .caller=caller,
        .num_args=num_args,
        .args=new llir_form*[num_args]
    };
}

llir_const_obj* mk_llir_const(const source_loc& origin,
        constant_id id,
        llir_const_obj* dest) {
    return new llir_const_obj{
        .header={.origin=origin, .tag=llir_const},
        .id=id
    };
}

llir_fn_obj* mk_llir_fn(const source_loc& origin,
        local_address num_pos_args,
        bool has_var_list_arg,
        bool has_var_table_arg,
        local_address req_args,
        llir_form* body) {
    return new llir_fn_obj {
        .header={.origin=origin, .tag=llir_fn},
        .params={
            .num_pos_args=num_pos_args,
            .pos_args=new symbol_id[num_pos_args],
            .has_var_list_arg=has_var_list_arg,
            .has_var_table_arg=has_var_table_arg,
            .req_args=req_args,
            .init_forms=new llir_form*[num_pos_args-req_args]
        },
        .body=body
    };
}

llir_import_obj* mk_llir_import(const source_loc& origin,
        symbol_id target) {
    return new llir_import_obj {
        .header={.origin=origin, .tag=llir_import},
        .target=target
    };
}

llir_set_key_obj* mk_llir_set_key(const source_loc& origin,
        llir_form* target,
        llir_form* key,
        llir_form* value) {
    return new llir_set_key_obj {
        .header={.origin=origin, .tag=llir_set_key},
        .target=target,
        .key=key,
        .value=value
    };
}

llir_set_var_obj* mk_llir_set_var(const source_loc& origin,
        symbol_id var,
        llir_form* value) {
    return new llir_set_var_obj {
        .header={.origin=origin, .tag=llir_set_var},
        .var=var,
        .value=value
    };
}

llir_var_obj* mk_llir_var(const source_loc& origin,
        symbol_id name) {
    return new llir_var_obj{
        .header={.origin=origin, .tag=llir_var},
        .name=name
    };
}

llir_with_obj* mk_llir_with(const source_loc& origin,
        local_address num_vars,
        llir_form* body) {
    return new llir_with_obj{
        .header={.origin=origin, .tag=llir_var},
        .num_vars=num_vars,
        .vars=new symbol_id[num_vars],
        .value_forms=new llir_form*[num_vars],
        .body=body
    };
}


}
