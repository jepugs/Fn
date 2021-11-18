#include "llir.hpp"

namespace fn {

llir_def_form* mk_llir_def_form(const source_loc& origin,
        symbol_id name,
        llir_form* value,
        llir_def_form* dest) {
    if (dest == nullptr) {
        dest = new llir_def_form;
    }
    return new(dest) llir_def_form {
        .header={.origin=origin, .tag=llir_def},
        .name=name,
        .value=value
    };
}
void clear_llir_def_form(llir_def_form* obj, bool recursive) {
    if (recursive) {
        free_llir_form(obj->value);
    }
}
void free_llir_def_form(llir_def_form* obj, bool recursive) {
    clear_llir_def_form(obj, recursive);
    delete obj;
}

llir_defmacro_form* mk_llir_defmacro_form(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun,
        llir_defmacro_form* dest) {
    if (dest == nullptr) {
        dest = new llir_defmacro_form;
    }
    return new(dest) llir_defmacro_form {
        .header={.origin=origin, .tag=llir_defmacro},
        .name=name,
        .macro_fun=macro_fun
    };
}
void clear_llir_defmacro_form(llir_defmacro_form* obj, bool recursive) {
    if (recursive) {
        delete obj->macro_fun;
    }
}
void free_llir_defmacro_form(llir_defmacro_form* obj, bool recursive) {
    clear_llir_defmacro_form(obj, recursive);
    delete obj;
}

llir_dot_form* mk_llir_dot_form(const source_loc& origin,
        llir_form* obj,
        local_address num_keys,
        llir_dot_form* dest) {
    if (dest == nullptr) {
        dest = new llir_dot_form;
    }
    (*dest) = {
        .header={.origin=origin, .tag=llir_dot},
        .obj=obj,
        .num_keys=num_keys,
        .keys=new symbol_id[num_keys]
    };
    return dest;
}
void clear_llir_dot_form(llir_dot_form* obj, bool recursive) {
    delete obj->keys;
    if (recursive) {
        free_llir_form(obj->obj);
    }
}
void free_llir_dot_form(llir_dot_form* obj, bool recursive) {
    clear_llir_dot_form(obj, recursive);
    delete obj;
}

llir_call_form* mk_llir_call_form(const source_loc& origin,
        llir_form* callee,
        local_address num_args,
        llir_call_form* dest) {
    if (dest == nullptr) {
        dest = new llir_call_form;
    }
    return new (dest) llir_call_form {
        .header={.origin=origin, .tag=llir_call},
        .callee=callee,
        .num_args=num_args,
        .args=new llir_form*[num_args]
    };
}
void clear_llir_call_form(llir_call_form* obj, bool recursive) {
    if (recursive) {
        free_llir_form(obj->callee);
        for (int i = 0; i < obj->num_args; ++i) {
            free_llir_form(obj->args[i]);
        }
    }
    delete obj->args;
}
void free_llir_call_form(llir_call_form* obj, bool recursive) {
    clear_llir_call_form(obj, recursive);
    delete obj;
}

llir_if_form* mk_llir_if_form(const source_loc& origin,
        llir_form* test_form,
        llir_form* then_form,
        llir_form* else_form,
        llir_if_form* dest) {
    if (dest == nullptr) {
        dest = new llir_if_form;
    }
    return new(dest) llir_if_form {
        .header={.origin=origin, .tag=llir_if},
        .test_form=test_form,
        .then_form=then_form,
        .else_form=else_form
    };
}
void clear_llir_if_form(llir_if_form* obj, bool recursive) {
    if (recursive) {
        free_llir_form(obj->test_form);
        free_llir_form(obj->then_form);
        free_llir_form(obj->else_form);
    }
}
void free_llir_if_form(llir_if_form* obj, bool recursive) {
    clear_llir_if_form(obj, recursive);
    delete obj;
}


llir_const_form* mk_llir_const_form(const source_loc& origin,
        constant_id id,
        llir_const_form* dest) {
    return new llir_const_form{
        .header={.origin=origin, .tag=llir_const},
        .id=id
    };
}
void free_llir_const_form(llir_const_form* obj) {
    delete obj;
}

llir_fn_form* mk_llir_fn_form(const source_loc& origin,
        local_address num_pos_args,
        bool has_var_list_arg,
        bool has_var_table_arg,
        local_address req_args,
        llir_form* body) {
    return new llir_fn_form {
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
void clear_llir_fn_form(llir_fn_form* obj, bool recursive) {
    if (recursive) {
        auto m = obj->params.num_pos_args - obj->params.req_args;
        for (int i = 0; i < m; ++i) {
            free_llir_form(obj->params.init_forms[i]);
        }
        free_llir_form(obj->body);
    }
    delete obj->params.pos_args;
    delete obj->params.init_forms;
}
void free_llir_fn_form(llir_fn_form* obj, bool recursive) {
    clear_llir_fn_form(obj, recursive);
    delete obj;
}

llir_import_form* mk_llir_import_form(const source_loc& origin,
        symbol_id target) {
    return new llir_import_form {
        .header={.origin=origin, .tag=llir_import},
        .target=target
    };
}
void free_llir_import_form(llir_import_form* obj) {
    delete obj;
}

llir_set_key_form* mk_llir_set_key_form(const source_loc& origin,
        llir_form* target,
        llir_form* key,
        llir_form* value) {
    return new llir_set_key_form {
        .header={.origin=origin, .tag=llir_set_key},
        .target=target,
        .key=key,
        .value=value
    };
}
void clear_llir_set_key_form(llir_set_key_form* obj, bool recursive) {
    if (recursive) {
        free_llir_form(obj->target);
        free_llir_form(obj->key);
        free_llir_form(obj->value);
    }
}
void free_llir_set_key_form(llir_set_key_form* obj, bool recursive) {
    clear_llir_set_key_form(obj, recursive);
    delete obj;
}

llir_set_var_form* mk_llir_set_var_form(const source_loc& origin,
        symbol_id var,
        llir_form* value) {
    return new llir_set_var_form {
        .header={.origin=origin, .tag=llir_set_var},
        .var=var,
        .value=value
    };
}
void clear_llir_set_var_form(llir_set_var_form* obj, bool recursive) {
    if (recursive) {
        free_llir_form(obj->value);
    }
}
void free_llir_set_var_form(llir_set_var_form* obj, bool recursive) {
    clear_llir_set_var_form(obj, recursive);
    delete obj;
}

llir_var_form* mk_llir_var_form(const source_loc& origin,
        symbol_id name,
        llir_var_form* dest) {
    if (dest == nullptr) {
        dest = new llir_var_form;
    }
    return new(dest) llir_var_form{
        .header={.origin=origin, .tag=llir_var},
        .name=name
    };
}
void free_llir_var_form(llir_var_form* obj) {
    delete obj;
}

llir_with_form* mk_llir_with_form(const source_loc& origin,
        local_address num_vars,
        u32 body_length,
        llir_with_form* dest) {
    if (dest == nullptr) {
        dest = new llir_with_form;
    }
    return new(dest) llir_with_form{
        .header={.origin=origin, .tag=llir_with},
        .num_vars=num_vars,
        .vars=new symbol_id[num_vars],
        .value_forms=new llir_form*[num_vars],
        .body_length=body_length,
        .body=new llir_form*[body_length]
    };
}
void clear_llir_with_form(llir_with_form* obj, bool recursive) {
    if(recursive) {
        for (u32 i = 0; i < obj->num_vars; ++i) {
            free_llir_form(obj->value_forms[i]);
        }
        for (u32 i = 0; i < obj->body_length; ++i) {
            free_llir_form(obj->body[i]);
        }
    }
    delete obj->vars;
    delete obj->value_forms;
    delete obj->body;
}
void free_llir_with_form(llir_with_form* obj, bool recursive) {
    clear_llir_with_form(obj, recursive);
    delete obj;
}

void clear_llir_form(llir_form* obj, bool recursive) {
    switch (obj->tag) {
    case llir_def:
        clear_llir_def_form((llir_def_form*)obj, recursive);
        break;
    case llir_defmacro:
        clear_llir_defmacro_form((llir_defmacro_form*)obj, recursive);
        break;
    case llir_dot:
        clear_llir_dot_form((llir_dot_form*)obj, recursive);
        break;
    case llir_call:
        clear_llir_call_form((llir_call_form*)obj, recursive);
        break;
    case llir_const:
        break;
    case llir_fn:
        clear_llir_fn_form((llir_fn_form*)obj, recursive);
        break;
    case llir_if:
        clear_llir_if_form((llir_if_form*)obj, recursive);
        break;
    case llir_import:
        break;
    case llir_set_key:
        clear_llir_set_key_form((llir_set_key_form*)obj, recursive);
        break;
    case llir_set_var:
        clear_llir_set_var_form((llir_set_var_form*)obj, recursive);
        break;
    case llir_var:
        break;
    case llir_with:
        clear_llir_with_form((llir_with_form*)obj, recursive);
        break;
    }
}

void free_llir_form(llir_form* obj, bool recursive) {
    switch (obj->tag) {
    case llir_def:
        free_llir_def_form((llir_def_form*)obj, recursive);
        break;
    case llir_defmacro:
        free_llir_defmacro_form((llir_defmacro_form*)obj, recursive);
        break;
    case llir_dot:
        free_llir_dot_form((llir_dot_form*)obj, recursive);
        break;
    case llir_call:
        free_llir_call_form((llir_call_form*)obj, recursive);
        break;
    case llir_const:
        free_llir_const_form((llir_const_form*)obj);
        break;
    case llir_fn:
        free_llir_fn_form((llir_fn_form*)obj, recursive);
        break;
    case llir_if:
        free_llir_if_form((llir_if_form*)obj, recursive);
        break;
    case llir_import:
        free_llir_import_form((llir_import_form*)obj);
        break;
    case llir_set_key:
        free_llir_set_key_form((llir_set_key_form*)obj, recursive);
        break;
    case llir_set_var:
        free_llir_set_var_form((llir_set_var_form*)obj, recursive);
        break;
    case llir_var:
        free_llir_var_form((llir_var_form*)obj);
        break;
    case llir_with:
        free_llir_with_form((llir_with_form*)obj, recursive);
        break;
    }
}

static void write_indent(std::ostream& out, int offset) {
    for (int i = 0; i < offset; ++i) {
        out << ' ';
    }
}

static void print_llir_offset(llir_form* form,
        symbol_table& st,
        code_chunk* chunk,
        int offset,
        bool preindent) {
    std::ostream& out = std::cout;

    if (preindent) {
        write_indent(out, offset);
    }
    switch (form->tag) {
    case llir_def:
        {
            auto xdef = (llir_def_form*)form;
            out << "(DEF " << st[xdef->name] << '\n';
            print_llir_offset(xdef->value, st, chunk, offset + 2,
                    true);
            out << ')';
        }
        break;
    case llir_defmacro:
        break;
    case llir_dot:
        break;
    case llir_call:
        {
            auto xcall = (llir_call_form*)form;
            out << '(';
            print_llir_offset(xcall->callee, st, chunk, offset+1, false);
            for (int i = 0; i < xcall->num_args; ++i) {
                out << '\n';
                print_llir_offset(xcall->args[i], st, chunk, offset+1, true);
            }
        out << ')';
        }
        break;
    case llir_const:
        out << v_to_string(chunk->
                get_constant(((llir_const_form*)form)->id), &st);
        break;
    case llir_fn:
        break;
    case llir_if:
        {
            auto xif = (llir_if_form*)form;
            out << "(IF ";
            print_llir_offset(xif->test_form, st, chunk, offset+4, false);
            out << '\n';
            print_llir_offset(xif->then_form, st, chunk, offset+4, true);
            out << '\n';
            print_llir_offset(xif->else_form, st, chunk, offset+4, true);
            out << ')';
        }
        break;
    case llir_import:
        break;
    case llir_set_key:
        break;
    case llir_set_var:
        break;
    case llir_var:
        out << v_to_string(as_sym_value(((llir_var_form*)form)->name), &st);
        break;
    case llir_with:
        {
            auto xwith = (llir_with_form*)form;
            out << "(WITH ()"; // TODO: print vars
            for (u32 i = 0; i < xwith->body_length; ++i) {
                out << '\n';
                print_llir_offset(xwith->body[i], st, chunk, offset+2, true);
            }
            out << ')';
        }
        break;
    }
}

void print_llir(llir_form* f, symbol_table& st, code_chunk* chunk) {
    print_llir_offset(f, st, chunk, 0, false);
    std::cout <<  '\n';
}

}
