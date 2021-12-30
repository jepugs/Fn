#include "llir.hpp"

namespace fn {

llir_apply* mk_llir_apply(const source_loc& origin,
        llir_form* callee,
        local_address num_args) {
    return new llir_apply{
        .header={.origin=origin, .tag=lt_apply},
        .callee=callee,
        .num_args=num_args,
        .args=new llir_form*[num_args],
    };
}
void clear_llir_apply(llir_apply* obj) {
    for (u32 i = 0; i < obj->num_args; ++i) {
        free_llir_form(obj->args[i]);
    }
    delete[] obj->args;
    free_llir_form(obj->callee);
}
void free_llir_apply(llir_apply* obj) {
    clear_llir_apply(obj);
    delete obj;
}


llir_call* mk_llir_call(const source_loc& origin,
        llir_form* callee,
        local_address num_args) {
    return new llir_call {
        .header={.origin=origin, .tag=lt_call},
        .callee=callee,
        .num_args=num_args,
        .args=new llir_form*[num_args]
    };
}
void clear_llir_call(llir_call* obj) {
    free_llir_form(obj->callee);
    for (int i = 0; i < obj->num_args; ++i) {
        free_llir_form(obj->args[i]);
    }
    delete[] obj->args;
}
void free_llir_call(llir_call* obj) {
    clear_llir_call(obj);
    delete obj;
}

llir_const* mk_llir_const(const source_loc& origin,
        constant_id id,
        llir_const* dest) {
    return new llir_const{
        .header={.origin=origin, .tag=lt_const},
        .id=id
    };
}
void free_llir_const(llir_const* obj) {
    delete obj;
}

llir_def* mk_llir_def(const source_loc& origin,
        symbol_id name,
        llir_form* value,
        llir_def* dest) {
    if (dest == nullptr) {
        dest = new llir_def;
    }
    return new(dest) llir_def {
        .header={.origin=origin, .tag=lt_def},
        .name=name,
        .value=value
    };
}
void clear_llir_def(llir_def* obj) {
    free_llir_form(obj->value);
}
void free_llir_def(llir_def* obj) {
    clear_llir_def(obj);
    delete obj;
}

llir_defmacro* mk_llir_defmacro(const source_loc& origin,
        symbol_id name,
        llir_form* macro_fun,
        llir_defmacro* dest) {
    if (dest == nullptr) {
        dest = new llir_defmacro;
    }
    return new(dest) llir_defmacro {
        .header={.origin=origin, .tag=lt_defmacro},
        .name=name,
        .macro_fun=macro_fun
    };
}
void clear_llir_defmacro(llir_defmacro* obj) {
    free_llir_form(obj->macro_fun);
}
void free_llir_defmacro(llir_defmacro* obj) {
    clear_llir_defmacro(obj);
    delete obj;
}

llir_dot* mk_llir_dot(const source_loc& origin,
        llir_form* obj,
        symbol_id key,
        llir_dot* dest) {
    if (dest == nullptr) {
        dest = new llir_dot;
    }
    (*dest) = {
        .header={.origin=origin, .tag=lt_dot},
        .obj=obj,
        .key=key
    };
    return dest;
}
void clear_llir_dot(llir_dot* obj) {
    free_llir_form(obj->obj);
}
void free_llir_dot(llir_dot* obj) {
    clear_llir_dot(obj);
    delete obj;
}

llir_if* mk_llir_if(const source_loc& origin,
        llir_form* test,
        llir_form* then,
        llir_form* elce,
        llir_if* dest) {
    if (dest == nullptr) {
        dest = new llir_if;
    }
    return new(dest) llir_if {
        .header={.origin=origin, .tag=lt_if},
        .test=test,
        .then=then,
        .elce=elce
    };
}
void clear_llir_if(llir_if* obj) {
    free_llir_form(obj->test);
    free_llir_form(obj->then);
    free_llir_form(obj->elce);
}
void free_llir_if(llir_if* obj) {
    clear_llir_if(obj);
    delete obj;
}

llir_fn* mk_llir_fn(const source_loc& origin,
        local_address num_pos_args,
        bool has_var_list_arg,
        local_address req_args,
        const string& name,
        llir_form* body,
        llir_fn* dest) {
    if (dest == nullptr) {
        dest = new llir_fn;
    }
    return new(dest) llir_fn {
        .header={.origin=origin, .tag=lt_fn},
        .params={
            .num_pos_args=num_pos_args,
            .pos_args=new symbol_id[num_pos_args],
            .has_var_list_arg=has_var_list_arg,
            .req_args=req_args,
            .inits=new llir_form*[num_pos_args-req_args]
        },
        .name=name,
        .body=body
    };
}
llir_fn* mk_llir_fn(const source_loc& origin,
        const llir_fn_params& params,
        const string& name,
        llir_form* body,
        llir_fn* dest) {
    if (dest == nullptr) {
        dest = new llir_fn;
    }
    return new(dest) llir_fn {
        .header={.origin=origin, .tag=lt_fn},
        .params=params,
        .name=name,
        .body=body
    };

}
void clear_llir_fn(llir_fn* obj) {
    auto m = obj->params.num_pos_args - obj->params.req_args;
    for (int i = 0; i < m; ++i) {
        free_llir_form(obj->params.inits[i]);
    }
    free_llir_form(obj->body);
    delete[] obj->params.pos_args;
    delete[] obj->params.inits;
}
void free_llir_fn(llir_fn* obj) {
    clear_llir_fn(obj);
    delete obj;
}

llir_import* mk_llir_import(const source_loc& origin,
        symbol_id target,
        llir_import* dest) {
    if (dest == nullptr) {
        dest = new llir_import;
    }
    return new(dest) llir_import {
        .header={.origin=origin, .tag=lt_import},
        .target=target
    };
}
void free_llir_import(llir_import* obj) {
    delete obj;
}

llir_set* mk_llir_set(const source_loc& origin,
        llir_form* target,
        llir_form* value,
        llir_set* dest) {
    if (dest == nullptr) {
        dest = new llir_set;
    }
    return new(dest) llir_set {
        .header={.origin=origin, .tag=lt_set},
        .target=target,
        .value=value
    };
}
void clear_llir_set(llir_set* obj) {
    free_llir_form(obj->target);
    free_llir_form(obj->value);
}
void free_llir_set(llir_set* obj) {
    clear_llir_set(obj);
    delete obj;
}

llir_var* mk_llir_var(const source_loc& origin,
        symbol_id name,
        llir_var* dest) {
    if (dest == nullptr) {
        dest = new llir_var;
    }
    return new(dest) llir_var{
        .header={.origin=origin, .tag=lt_var},
        .name=name
    };
}
void free_llir_var(llir_var* obj) {
    delete obj;
}

llir_with* mk_llir_with(const source_loc& origin,
        local_address num_vars,
        u32 body_length,
        llir_with* dest) {
    if (dest == nullptr) {
        dest = new llir_with;
    }
    return new(dest) llir_with{
        .header={.origin=origin, .tag=lt_with},
        .num_vars=num_vars,
        .vars=new symbol_id[num_vars],
        .values=new llir_form*[num_vars],
        .body_length=body_length,
        .body=new llir_form*[body_length]
    };
}
void clear_llir_with(llir_with* obj) {
    for (u32 i = 0; i < obj->num_vars; ++i) {
        free_llir_form(obj->values[i]);
    }
    for (u32 i = 0; i < obj->body_length; ++i) {
        free_llir_form(obj->body[i]);
    }
    delete[] obj->vars;
    delete[] obj->values;
    delete[] obj->body;
}
void free_llir_with(llir_with* obj) {
    clear_llir_with(obj);
    delete obj;
}

void clear_llir_form(llir_form* obj) {
    switch (obj->tag) {
    case lt_apply:
        clear_llir_apply((llir_apply*)obj);
        break;
    case lt_def:
        clear_llir_def((llir_def*)obj);
        break;
    case lt_defmacro:
        clear_llir_defmacro((llir_defmacro*)obj);
        break;
    case lt_dot:
        clear_llir_dot((llir_dot*)obj);
        break;
    case lt_call:
        clear_llir_call((llir_call*)obj);
        break;
    case lt_const:
        break;
    case lt_fn:
        clear_llir_fn((llir_fn*)obj);
        break;
    case lt_if:
        clear_llir_if((llir_if*)obj);
        break;
    case lt_import:
        break;
    case lt_set:
        clear_llir_set((llir_set*)obj);
        break;
    case lt_var:
        break;
    case lt_with:
        clear_llir_with((llir_with*)obj);
        break;
    }
}

void free_llir_form(llir_form* obj) {
    switch (obj->tag) {
    case lt_apply:
        free_llir_apply((llir_apply*)obj);
        break;
    case lt_call:
        free_llir_call((llir_call*)obj);
        break;
    case lt_const:
        free_llir_const((llir_const*)obj);
        break;
    case lt_def:
        free_llir_def((llir_def*)obj);
        break;
    case lt_defmacro:
        free_llir_defmacro((llir_defmacro*)obj);
        break;
    case lt_dot:
        free_llir_dot((llir_dot*)obj);
        break;
    case lt_fn:
        free_llir_fn((llir_fn*)obj);
        break;
    case lt_if:
        free_llir_if((llir_if*)obj);
        break;
    case lt_import:
        free_llir_import((llir_import*)obj);
        break;
    case lt_set:
        free_llir_set((llir_set*)obj);
        break;
    case lt_var:
        free_llir_var((llir_var*)obj);
        break;
    case lt_with:
        free_llir_with((llir_with*)obj);
        break;
    }
}

static void write_indent(std::ostream& out, int offset) {
    for (int i = 0; i < offset; ++i) {
        out << ' ';
    }
}

static string print_llir_offset(llir_form* form,
        symbol_table& st,
        code_chunk* chunk,
        int offset,
        bool preindent) {
    std::ostringstream out;

    if (preindent) {
        write_indent(out, offset);
    }
    switch (form->tag) {
        // FIXME: Write this code!
    case lt_apply:
        out << "(APPLY )";
        break;
    case lt_def:
        {
            auto xdef = (llir_def*)form;
            out << "(DEF " << st[xdef->name] << '\n';
            print_llir_offset(xdef->value, st, chunk, offset + 2,
                    true);
            out << ')';
        }
        break;
    case lt_defmacro:
        {
            auto xdefm = (llir_defmacro*)form;
            out << "(DEFMACRO " << st[xdefm->name] << '\n';
            print_llir_offset(xdefm->macro_fun, st, chunk, offset + 2,
                    true);
            out << ')';
        }
        break;
    case lt_dot:
        {
            auto xdot = (llir_dot*) form;
            out << "(DOT ";
            print_llir_offset(xdot->obj, st, chunk, offset + 5, false);
            out << '\n';
            write_indent(out, offset+4);
            out << ' ' << st[xdot->key] << ')';
        }
        break;
    case lt_call:
        {
            auto xcall = (llir_call*)form;
            out << '(';
            int i = 0;
            int noffset = offset+2;
            if (xcall->callee->tag == lt_var) {
                auto sym = ((llir_var*)xcall->callee)->name;
                auto str = v_to_string(vbox_symbol(sym), &st);
                out << str << ' ';
                noffset += str.size();

                if (xcall->num_args > 0) {
                    out << print_llir_offset(xcall->args[0], st, chunk,
                            noffset, false);
                }
                i = 1;
            } else {
                out << print_llir_offset(xcall->callee, st, chunk, noffset,
                        false);
            }
            for (; i < xcall->num_args; ++i) {
                out << '\n'
                    << print_llir_offset(xcall->args[i], st, chunk,
                            noffset, true);
            }

        out << ')';
        }
        break;
    case lt_const:
        out << v_to_string(chunk->
                get_constant(((llir_const*)form)->id), &st);
        break;
    case lt_fn:
        {
            auto xfn = (llir_fn*)form;
            out << "(FN (";
            // params
            auto& params = xfn->params;
            for (u32 i = 0; i < params.req_args; ++i) {
                out << st.nice_name(params.pos_args[i]) << ' ';
            }
            for (u32 i = params.req_args; i < params.num_pos_args; ++i) {
                out << '(' << st.nice_name(params.pos_args[i]) << ") ";
            }
            if (params.has_var_list_arg) {
                out << "& " << st.nice_name(params.var_list_arg) << ' ';
            }
            // body
            out << ")\n"
                << print_llir_offset(xfn->body, st, chunk, offset+2, true)
                << ')';
        }
        break;
    case lt_if:
        {
            auto xif = (llir_if*)form;
            out << "(IF "
                << print_llir_offset(xif->test, st, chunk, offset+4, false)
                << '\n'
                << print_llir_offset(xif->then, st, chunk, offset+4, true)
                << '\n'
                << print_llir_offset(xif->elce, st, chunk, offset+4, true)
                << ')';
        }
        break;
    case lt_import:
        // TODO: support for alias, unqualified imports
        out << "(IMPORT "
            << st[((llir_import*)form)->target]
            << ')';
        break;
    case lt_set:
        {
            auto xset = (llir_set*)form;
            out << "(SET! "
                << print_llir_offset(xset->target, st, chunk, offset + 6,
                        false)
                << '\n'
                << print_llir_offset(xset->value, st, chunk, offset + 6,
                        true)
                << ')';
        }
        break;
    case lt_var:
        {
            auto var = ((llir_var*)form)->name;
            if (st.is_gensym(var)) {
                out << st.gensym_name(var);
            } else {
                out << st[var];
            }
        }
        break;
    case lt_with:
        {
            auto xwith = (llir_with*)form;
            out << "(WITH (";
            // print vars
            if (xwith->num_vars > 0) {
                i64 i = 0;
                // have to do this out here to avoid printing the last '\n'
                auto name = st[xwith->vars[i]];
                if (st.is_gensym(xwith->vars[i])) {
                    name = st.gensym_name(xwith->vars[i]);
                }
                out << name << ' '
                    << print_llir_offset(xwith->values[i], st, chunk,
                            offset + 8 + name.size(), false);

                for (i = 1; i < xwith->num_vars; ++i) {
                    out << '\n';
                    auto name = st[xwith->vars[i]];
                    if (st.is_gensym(xwith->vars[i])) {
                        name = st.gensym_name(xwith->vars[i]);
                    }
                    write_indent(out, offset + 7);
                    out << name << ' '
                        << print_llir_offset(xwith->values[i], st, chunk,
                                offset + 8 + name.size(), false);
                }
            }
            out << ')';
                
            // print body
            for (u32 i = 0; i < xwith->body_length; ++i) {
                out << '\n'
                    << print_llir_offset(xwith->body[i], st, chunk, offset+2,
                            true);
            }
            out << ')';
        }
        break;
    }
    return out.str();
}

string print_llir(llir_form* f, symbol_table& st, code_chunk* chunk) {
    return print_llir_offset(f, st, chunk, 0, false) + string{"\n"};
}

}
