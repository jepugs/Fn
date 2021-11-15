#include "llir.hpp"

namespace fn {

llir_def_obj* make_llir_def(const source_loc& origin,
        symbol_id name,
        llir_form* value) {
    return new llir_def_obj{
        .header={.origin=origin, .tag=llir_def},
        .name=name,
        .value=value
    };
}

llir_defmacro_obj* make_llir_defmacro(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun) {
    return new llir_defmacro_obj{
        .header={.origin=origin, .tag=llir_defmacro},
        .name=name,
        .macro_fun=macro_fun
    };
}

llir_dot_obj* make_llir_dot(const source_loc& origin,
        llir_form* obj,
        local_address num_keys) {
    return new llir_dot_obj{
        .header={.origin=origin, .tag=llir_dot},
        .obj=obj,
        .num_keys=num_keys,
        .keys=new symbol_id[num_keys]
    };
}

llir_call_obj* make_llir_call(const source_loc& origin,
        llir_form* caller,
        local_address num_args) {
    return new llir_call_obj {
        .header={.origin=origin, .tag=llir_call},
        .caller=caller,
        .num_args=num_args,
        .args=new llir_form*[num_args]
    };
}

llir_const_obj* make_llir_const(const source_loc& origin, const_id id) {
    return new llir_const_obj{
        .header={.origin=origin, .tag=llir_const},
        .id=id
    };
}

llir_fn_obj* make_llir_fn(const source_loc& origin,
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

llir_import_obj* make_llir_import(const source_loc& origin,
        symbol_id target) {
    return new llir_import_obj {
        .header={.origin=origin, .tag=llir_import},
        .target=target
    };
}

llir_set_obj* make_llir_set(const source_loc& origin,
        llir_form* target,
        llir_form* value) {
    return new llir_set_obj {
        .header={.origin=origin, .tag=llir_set},
        .target=target,
        .value=value
    };
}

llir_var_obj* make_llir_var(const source_loc& origin,
        symbol_id name) {
    return new llir_var_obj{
        .header={.origin=origin, .tag=llir_var},
        .name=name
    };
}

llir_with_obj* make_llir_with(const source_loc& origin,
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
