#include "expand.hpp"

#include "interpret.hpp"

namespace fn {

using namespace fn_parse;

bool expander::is_macro(symbol_id sym) {
    // todo: check this chunk's namespace in global_env
    return false;
}

bool expander::is_operator_list(const string& op_name, ast_form* ast) {
    if (ast->kind != ak_list) {
        return false;
    } else if (ast->list_length == 0) {
        return false;
    }
    auto op = ast->datum.list[0];
    if (op->kind != ak_symbol_atom) {
        return false;
    }
    return op->datum.sym == inter->get_symtab()->intern(op_name);
}

// Note: and, cond, and or expanders take advantage of the fact that they don't
// look at the first argument of the list at all, so we can actually invoke them
// recursively while just incrementing the lst pointer to iterate.
llir_form* expander::expand_and(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        return (llir_form*)mk_llir_var_form(loc, intern("true"));
    } else if (length == 2) {
        return expand_meta(lst[1], meta);
    }

    auto x = expand_meta(lst[1], meta);
    if (!x) {
        return nullptr;
    }
    auto y = expand_and(loc, length - 1, &lst[1], meta);
    if (!y) {
        free_llir_form(x);
        return nullptr;
    }

    // this symbol will hold the value of x
    auto sym = gensym();
    auto sym_form = (llir_form*)mk_llir_var_form(loc, sym);
    auto conditional = mk_llir_if_form(loc, sym_form, y,
            (llir_form*)mk_llir_var_form(loc, sym));
            //copy_llir_form(sym_form));

    auto res = mk_llir_with_form(loc, 1, 1);
    res->body[0] = (llir_form*)conditional;
    // assign sym to the value of x
    res->vars[0] = sym;
    res->value_forms[0] = x;
    return (llir_form*)res;
}

// see note for expand_and
llir_form* expander::expand_cond(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length % 2 != 1) {
        meta->err.message = "Odd number of arguments to cond.";
        meta->err.origin = loc;
        return nullptr;
    } else if (length == 1) {
        return (llir_form*)mk_llir_var_form(loc, intern("nil"));
    }

    auto x = expand_meta(lst[1], meta);
    if (!x) {
        return nullptr;
    }
    auto y = expand_meta(lst[2], meta);
    if (!y) {
        free_llir_form(x);
        return nullptr;
    }
    auto z = expand_cond(loc, length-2, &lst[2], meta);
    if (!z) {
        free_llir_form(x);
        free_llir_form(y);
        return nullptr;
    }

    return (llir_form*)mk_llir_if_form(loc, x, y, z);
}

llir_form* expander::expand_def(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 3) {
        meta->err.message = "def requires exactly 2 arguments.";
        meta->err.origin = loc;
        return nullptr;
    }
    if (lst[1]->kind != ak_symbol_atom) {
        meta->err.message = "First argument to def not a symbol.";
        meta->err.origin = loc;
        return nullptr;
    }

    auto x = expand_meta(lst[2], meta);
    if (!x) {
        return nullptr;
    }

    return (llir_form*)mk_llir_def_form(loc, lst[1]->datum.sym, x);
}

bool expander::is_do_inline(ast_form* ast) {
    return is_operator_list("do-inline", ast);
}

bool expander::is_let(ast_form* ast) {
    return is_operator_list("let", ast);
}

void expander::flatten_do_body(u32 length,
        ast_form** lst,
        vector<ast_form*>& buf,
        expander_meta* meta) {
    for (u32 i = 1; i < length; ++i) {
        if (is_do_inline(lst[i])) {
            // this will just splice in the other forms
            flatten_do_body(lst[i]->list_length, lst[i]->datum.list, buf, meta);
        } else {
            buf.push_back(lst[i]);
        }
    }
}

llir_form* expander::expand_let_in_do(u32 length,
        ast_form** ast_body,
        expander_meta* meta) {
    auto let_form = ast_body[0];
    if (let_form->list_length == 0) {
        return (llir_form*)mk_llir_var_form(let_form->loc,
                inter->get_symtab()->intern("nil"));
    } else if (let_form->list_length % 2 != 1) {
        meta->err.origin = let_form->loc;
        meta->err.message = "Odd number of arguments to let.";
        return nullptr;
    }

    // collect variables and init forms
    auto len = (let_form->list_length - 1) / 2;

    meta->err.origin = let_form->loc;
    meta->err.message = "let not implemented :(";
    return nullptr;
}

// This function fills out the provided vector with llir forms that make up the
// do body. ast_body here should point to lst[1] of the original do form.
// Returns false on error. This also cleans up all the forms in the buffer, so
// if passing in a nonempty buffer, your llir_form*s will get deleted on error.
bool expander::expand_do_recur(u32 length,
        ast_form** ast_body,
        vector<llir_form*>& buf,
        expander_meta* meta) {
    for (u32 i = 0; i < length; ++i) {
        auto ast = ast_body[i];
        if (is_let(ast)) {
            auto body = expand_let_in_do(length-i, &ast_body[i], meta);
            if (!body) {
                for (auto y : buf) {
                    free_llir_form(y);
                }
                return false;
            }
            buf.push_back(body);
            return true;
        } else {
            auto x = expand_meta(ast, meta);
            if (!x) {
                for (auto y : buf) {
                    free_llir_form(y);
                }
                return false;
            }
            buf.push_back(x);
        }
    }
    return true;
}

llir_form* expander::expand_do(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        return (llir_form*)mk_llir_var_form(loc,
                inter->get_symtab()->intern("nil"));
    } else if (length == 2) {
        return expand_meta(lst[1], meta);
    }
    // TODO: implement
    // - flatten out do-inline forms
    // - introduce new lexical environments with each let
    // - execute all other expressions in order
    vector<ast_form*> ast_buf;
    flatten_do_body(length, lst, ast_buf, meta);
    vector<llir_form*> llir_buf;
    if (!expand_do_recur(ast_buf.size(), ast_buf.data(), llir_buf, meta)) {
        return nullptr;
    }
    auto res = mk_llir_with_form(loc, 0, llir_buf.size());
    for (u32 i = 0; i < llir_buf.size(); ++i) {
        res->body[i] = llir_buf[i];
    }
    return (llir_form*)res;
}

llir_form* expander::expand_if(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 4) {
        meta->err.message = "if requires exactly 3 arguments.";
        return nullptr;
    }

    auto x = expand_meta(lst[1], meta);
    if (!x) {
        return nullptr;
    }
    auto y = expand_meta(lst[2], meta);
    if (!y) {
        free_llir_form(x);
        return nullptr;
    }
    auto z = expand_meta(lst[3], meta);
    if (!z) {
        free_llir_form(y);
        free_llir_form(x);
        return nullptr;
    }

    return (llir_form*)mk_llir_if_form(loc, x, y, z);
}


llir_form* expander::expand_call(ast_form* lst, expander_meta* meta) {
    // function call
    auto op = lst->datum.list[0];
    auto& loc = lst->loc;

    auto callee = expand_meta(op, meta);
    if (!callee) {
        return nullptr;
    }
    auto res = mk_llir_call_form(loc, callee, lst->list_length-1);
    for (auto i = 0; i < res->num_args; ++i) {
        auto arg = expand_meta(lst->datum.list[i+1], meta);
        if (!arg) { // error
            // clean up
            for (auto j = 0; j < i; ++j) {
                free_llir_form(res->args[j]);
            }
            free_llir_call_form(res, false);
            free_llir_form(callee);

            res = nullptr;
            break;
        }
        res->args[i] = arg;
    }
    return (llir_form*)res;
}

llir_form* expander::expand_symbol_list(ast_form* lst, expander_meta* meta) {
    auto& loc = lst->loc;
    auto sym = lst->datum.list[0]->datum.sym;
    // first check for macros
    if (is_macro(sym)) {
        auto ast = inter->macroexpand(chunk->ns_id, lst);
        auto res = expand_meta(ast, meta);
        free_ast_form(ast);
        return res;
    }

    auto name = (*inter->get_symtab())[sym];
    // special forms
    if (name == "and") {
        return expand_and(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "cond") {
        return expand_cond(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "def") {
        return expand_def(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "do") {
        return expand_do(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "if") {
        return expand_if(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "fn") {
    //     return expand_fn(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "let") {
    //     return expand_let(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "or") {
    //     return expand_or(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "set!") {
    //     return expand_set(loc, lst->list_length, lst->datum.list, meta);
    }

    // function calls
    return expand_call(lst, meta);
}

llir_form* expander::expand_list(ast_form* lst, expander_meta* meta) {
    auto& loc = lst->loc;
    if (lst->list_length == 0) {
        meta->err.origin = loc;
        meta->err.message = "() is not a legal expression";
        return nullptr;
    }

    auto op = lst->datum.list[0];
    switch (op->kind) {
    case ak_symbol_atom:
        return expand_symbol_list(lst, meta);
    case ak_number_atom:
        meta->err.origin = loc;
        meta->err.message = "Cannot call numbers as functions.";
        return nullptr;
    case ak_string_atom:
        meta->err.origin = loc;
        meta->err.message = "Cannot call strings as functions.";
        return nullptr;
    default:
        break;
    }

    return expand_call(lst, meta);
}

// used to update expander_meta. This checks if symbol_id is a dollar-symbol and
// updates the maximum dollar place value in the expander_meta* if so.
static void update_dollar_syms(symbol_table* st,
        symbol_id sym,
        expander_meta* meta) {
    auto name = (*st)[sym];
    i16 value;
    if (name.size() == 0 || name[0] != '$') {
        value = -1;
    } else if (name.size() == 1) {
        value = 0;
    } else if (name[1] == '0') {
        // no leading zeroes are allowed, i.e. $0 is the only $-symbol whose
        // name begins with $0.
        if (name.size() != 2) {
            value = -1; // not a $-symbol
        } else {
            value = 0; // exactly $0
        }
    } else {
        size_t len;
        auto l = stoul(name.substr(1), &len);
        if (len == name.size() - 1 && l <= max_local_address) {
            value = (i16)l;
        }
    }
    if (value > meta->max_dollar_sym) {
        meta->max_dollar_sym = value;
    }
}

llir_form* expander::expand_meta(ast_form* ast, expander_meta* meta) {
    constant_id c;
    auto& loc = ast->loc;
    auto ws = inter->get_alloc()->add_working_set();
    switch (ast->kind) {
    case ak_number_atom:
        c = chunk->add_constant(as_value(ast->datum.num));
        return (llir_form*)mk_llir_const_form(loc, c);
    case ak_string_atom:
        c = chunk->add_constant(ws.add_string(ast->datum.str->data));
        return (llir_form*)mk_llir_const_form(loc, c);
    case ak_symbol_atom:
        update_dollar_syms(inter->get_symtab(), ast->datum.sym, meta);
        return (llir_form*)mk_llir_var_form(loc, ast->datum.sym);
    case ak_list:
        return expand_list(ast, meta);
    }
    // this is never reached, but it shuts up the compiler
    return nullptr;
}

symbol_id expander::intern(const string& str) {
    return inter->get_symtab()->intern(str);
}

symbol_id expander::gensym() {
    return inter->get_symtab()->gensym();
}

expander::expander(interpreter* inter, code_chunk* const_chunk)
    : inter{inter}
    , chunk{const_chunk} {
}

llir_form* expander::expand(ast_form* ast, expand_error* err) {
    expander_meta m;
    m.max_dollar_sym = -1;
    auto res = expand_meta(ast, &m);
    if (!res) {
        *err = m.err;
    }
    return res;
}

}
