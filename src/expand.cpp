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
    return op->datum.sym == intern(op_name);
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
    // All this recursion! Someone should go back to Lisp >:(    i'm trying
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

// see comments for expand_defn
llir_form* expander::expand_defmacro(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        meta->err.origin = loc;
        meta->err.message = "defmacro requires at least 2 arguments.";
        return nullptr;
    } else if (!lst[1]->is_symbol()) {
        meta->err.origin = loc;
        meta->err.message = "First argument to defmacro must be a symbol.";
        return nullptr;
    }

    auto body = expand_do(loc, length-2, &lst[2], meta);
    if(!body) {
        return nullptr;
    }

    llir_fn_params params;
    if(!expand_params(loc, lst[2], &params, meta)) {
        free_llir_form(body);
        return nullptr;
    }

    auto value = (llir_form*)mk_llir_fn_form(loc, params, body);
    auto res = mk_llir_defmacro_form(loc, lst[1]->datum.sym, value);
    return (llir_form*)res;
}

llir_form* expander::expand_defn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        meta->err.origin = loc;
        meta->err.message = "defn requires at least 2 arguments.";
        return nullptr;
    } else if (!lst[1]->is_symbol()) {
        meta->err.origin = loc;
        meta->err.message = "First argument to defn must be a symbol.";
        return nullptr;
    }

    // do expects the first element to an operator, so we put &lst[2] since we
    // have the first body expression at &lst[3]
    auto body = expand_do(loc, length-2, &lst[2], meta);
    if(!body) {
        return nullptr;
    }

    llir_fn_params params;
    if(!expand_params(loc, lst[2], &params, meta)) {
        free_llir_form(body);
        return nullptr;
    }

    auto value = (llir_form*)mk_llir_fn_form(loc, params, body);
    auto res = mk_llir_def_form(loc, lst[1]->datum.sym, value);
    return (llir_form*)res;
}

bool expander::is_do_inline(ast_form* ast) {
    return is_operator_list("do-inline", ast);
}

bool expander::is_let(ast_form* ast) {
    return is_operator_list("let", ast);
}

bool expander::is_letfn(ast_form* ast) {
    return is_operator_list("letfn", ast);
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

    // body
    vector<llir_form*> body_forms;
    if (!expand_do_recur(length - 1, &ast_body[1], body_forms, meta)) {
        return nullptr;
    }

    auto num_vars = (let_form->list_length - 1) / 2;
    auto res = mk_llir_with_form(let_form->loc, num_vars, body_forms.size());

    // collect variables and init forms
    for (u32 i = 0; i < num_vars; ++i) {
        auto var = let_form->datum.list[2*i+1];
        if (var->kind != ak_symbol_atom) {
            meta->err.origin = var->loc;
            meta->err.message = "Variable names must be symbols.";
            for (u32 j = 0; j < i; ++j) {
                free_llir_form(res->value_forms[i]);
            }
            for (auto x : body_forms) {
                free_llir_form(x);
            }
            return nullptr;
        }

        res->vars[i] = var->datum.sym;
        auto x = expand_meta(let_form->datum.list[2*i+2], meta);
        if (!x) {
            for (u32 j = 0; j < i; ++j) {
                free_llir_form(res->value_forms[i]);
            }
            for (auto x : body_forms) {
                free_llir_form(x);
            }
            return nullptr;
        }
        res->value_forms[i] = x;
    }

    for (u32 i = 0; i < body_forms.size(); ++i) {
        res->body[i] = body_forms[i];
    }

    return (llir_form*) res;
}

llir_form* expander::expand_letfn_in_do(u32 length,
        ast_form** ast_body,
        expander_meta* meta) {
    auto letfn_lst = ast_body[0]->datum.list;
    auto letfn_len = ast_body[0]->list_length;
    auto& loc = ast_body[0]->loc;
    if (letfn_len < 3) {
        meta->err.origin = loc;
        meta->err.message = "letfn requires at least two arguments.";
        return nullptr;
    } else if (!letfn_lst[1]->is_symbol()) {
        meta->err.origin = loc;
        meta->err.message = "letfn name must be a symbol.";
        return nullptr;
    }

    // generate llir for the new function
    auto sym = letfn_lst[1]->datum.sym;
    llir_fn_params params;
    if (!expand_params(loc, letfn_lst[2], &params, meta)) {
        return nullptr;
    }
    auto fn_body = expand_do(loc, letfn_len-2, &letfn_lst[2],meta);
    if (!fn_body) {
        return nullptr;
    }
    auto fn_llir = (llir_form*)mk_llir_fn_form(loc, params, fn_body);

    // generate llir for the with body
    vector<llir_form*> body_forms;
    if (!expand_do_recur(length - 1, &ast_body[1], body_forms, meta)) {
        free_llir_form(fn_llir);
        return nullptr;
    }
    auto res = mk_llir_with_form(loc, 1, body_forms.size());
    res->vars[0] = sym;
    res->value_forms[0] = fn_llir;
    for (u32 i = 0; i < body_forms.size(); ++i) {
        res->body[i] = body_forms[i];
    }

    return (llir_form*) res;
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
        } else if (is_letfn(ast)) {
            auto body = expand_letfn_in_do(length-i, &ast_body[i], meta);
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
        return (llir_form*)mk_llir_var_form(loc, intern("nil"));
    } else if (length == 2) {
        return expand_meta(lst[1], meta);
    }

    vector<ast_form*> ast_buf;
    flatten_do_body(length, lst, ast_buf, meta);

    // TODO: check if first form is let or letfn
    if (length == 0) {
        return (llir_form*)mk_llir_var_form(loc, intern("nil"));
    } else if (is_let(ast_buf[0])) {
        return expand_let_in_do(ast_buf.size(), ast_buf.data(), meta);
    }
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

llir_form* expander::expand_do_inline(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    // if this hasn't been automatically inlined, just treat it as a normal do.
    return expand_do(loc, length, lst, meta);
}

llir_form* expander::expand_dollar_fn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 2) {
        meta->err.origin = loc;
        meta->err.message = "dollar-fn requires exactly 1 argument.";
        return nullptr;
    }

    expander_meta meta2 = {.max_dollar_sym=-1};
    auto body = expand_meta(lst[1], &meta2);
    if (!body) {
        meta->err = meta2.err;
        return nullptr;
    }

    auto m = meta2.max_dollar_sym;
    if (m >= 0) {
        auto body2 = mk_llir_with_form(body->origin, 1, 1);
        body2->vars[0] = intern("$");
        body2->value_forms[0] = (llir_form*)mk_llir_var_form(loc, intern("$0"));
        body2->body[0] = body;
        body = (llir_form*) body2;
    }

    auto res = mk_llir_fn_form(loc, m+1, false, false, m+1, body);
    for (i32 i = 0; i <= m+1; ++i) {
        res->params.pos_args[i] = intern("$" + std::to_string(i));
    }

    return (llir_form*) res;
}

llir_form* expander::expand_dot(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        meta->err.origin = loc;
        meta->err.message = "dot requires at least 2 arguments.";
        return nullptr;
    }

    auto x = expand_meta(lst[1], meta);
    if (!x) {
        return nullptr;
    }

    auto res = mk_llir_dot_form(loc, x, length - 2);
    for (u32 i = 0; i + 2 < length; ++i) {
        if (!lst[i+2]->is_symbol()) {
            meta->err.origin = loc;
            meta->err.message = "dot keys must be symbols.";
            free_llir_dot_form(res);
            return nullptr;
        } else {
            res->keys[i] = lst[i+2]->datum.sym;
        }
    }
    return (llir_form*) res;
}

bool expander::expand_params(const source_loc& loc,
        ast_form* ast,
        llir_fn_params* params,
        expander_meta* meta) {
    if (ast->kind != ak_list) {
        meta->err.origin = loc;
        meta->err.message = "fn requires a parameter list.";
        return false;
    }

    auto len = ast->list_length;
    auto amp_sym = intern("&");
    auto colamp_sym = intern(":&");
    auto& lst = ast->datum.list;

    vector<symbol_id> positional;
    u32 num_pos;
    vector<llir_form*> init_forms;
    // positional/required
    for (num_pos = 0; num_pos < len; ++num_pos) {
        auto& x = lst[num_pos];
        if (!x->is_symbol()) {
            break;
        }
        auto& sym = x->datum.sym;
        if (sym == amp_sym || sym == colamp_sym) {
            break;
        }
        positional.push_back(sym);
    }
    u32 num_req = num_pos;

    // positional/optional
    for (; num_pos < len; ++num_pos) {
        auto& x = lst[num_pos];
        if (x->kind != ak_list) {
            break;
        } else if (x->list_length != 2 || !x->datum.list[0]->is_symbol()) {
            // clean up
            for (auto x : init_forms) {
                free_llir_form(x);
            }
            meta->err.origin = x->loc;
            meta->err.message = "Malformed optional parameter in parameter list.";
            return false;
        }
        auto y = expand_meta(x->datum.list[1], meta);
        if (!y) {
            for (auto x : init_forms) {
                free_llir_form(x);
            }
            return false;
        }
        init_forms.push_back(y);
        positional.push_back(x->datum.list[0]->datum.sym);
    }

    // var args
    params->has_var_list_arg = false;
    params->has_var_table_arg = false;

    // this was a doozy to write. I tried to avoid backtracking or
    // doublechecking as much as possible. That's why the code is so explicit
    // and gross. Plz lmk if there's a much better way to accomplish this kind
    // of control flow. -- Jack
    bool malformed = false;
    if (num_pos < len) {
        auto x = lst[num_pos];
        if (x->is_symbol()
                && (x->datum.sym == amp_sym || x->datum.sym == colamp_sym)
                && (len == 2 + num_pos || len == 4 + num_pos)) {
            for (u32 i = num_pos; i + 1 < len; ++i) {
                if (!lst[i]->is_symbol()) {
                    malformed = true;
                    break;
                }
            }

            if (malformed) {
                // do nothing
            } else if (len == 2 + num_pos) {
                auto var = lst[num_pos+1]->datum.sym;
                if (x->datum.sym == amp_sym) {
                    params->has_var_list_arg = true;
                    params->has_var_table_arg = false;
                    params->var_list_arg = var;
                } else if (x->datum.sym == colamp_sym) {
                    params->has_var_list_arg = false;
                    params->has_var_table_arg = true;
                    params->var_table_arg = var;
                } else {
                    malformed = true;
                }
            } else if (len == 4 + num_pos) {
                auto var1 = lst[num_pos+1]->datum.sym;
                auto var2 = lst[num_pos+3]->datum.sym;
                if (x->datum.sym == amp_sym
                        && lst[num_pos+2]->datum.sym == colamp_sym) {
                    params->has_var_list_arg = true;
                    params->has_var_table_arg = true;
                    params->var_list_arg = var1;
                    params->var_table_arg = var2;
                } else if (x->datum.sym == colamp_sym
                        && lst[num_pos+2]->datum.sym == amp_sym) {
                    params->has_var_list_arg = true;
                    params->has_var_table_arg = true;
                    params->var_list_arg = var2;
                    params->var_table_arg = var1;
                } else {
                    malformed = true;
                }
            }
        } else {
            malformed = true;
        }
    }

    if (malformed) {
        for (auto x : init_forms) {
            free_llir_form(x);
        }
        meta->err.origin = loc;
        meta->err.message = "Malformed parameter list.";
        return false;
    }

    // TODO: check if we exceed maximum arguments
    params->num_pos_args = num_pos;
    params->pos_args = new symbol_id[num_pos];
    params->req_args = num_req;
    params->init_forms = new llir_form*[num_pos - num_req];
    for (u32 i = 0; i < positional.size(); ++i) {
        params->pos_args[i] = positional[i];
    }
    for (u32 i = 0; i < init_forms.size(); ++i) {
        params->init_forms[i] = init_forms[i];
    }
    return true;
}

llir_form* expander::expand_fn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        meta->err.origin = loc;
        meta->err.message = "fn requires a parameter list.";
        return nullptr;
    }
    llir_fn_params params;
    if (!expand_params(loc, lst[1], &params, meta)) {
        return nullptr;
    }

    llir_form* body;
    if (length == 2) {
        body = (llir_form*)mk_llir_var_form(loc, intern("nil"));
    } else {
        body = (llir_form*)expand_do(loc, length-1, &lst[1], meta);
    }
    return (llir_form*)mk_llir_fn_form(loc, params, body);
}

llir_form* expander::expand_if(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 4) {
        meta->err.origin = loc;
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

llir_form* expander::expand_import(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 2) {
        meta->err.origin = loc;
        meta->err.message = "import requires exactly 1 argument.";
        return nullptr;
    } else if (lst[1]->kind != ak_symbol_atom) {
        meta->err.origin = loc;
        meta->err.message = "import argument must be a symbol.";
        return nullptr;
    }
    return (llir_form*)mk_llir_import_form(loc, lst[1]->datum.sym);
}

llir_form* expander::expand_or(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        return (llir_form*)mk_llir_var_form(loc, intern("false"));
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
    auto sym_form2 = (llir_form*)mk_llir_var_form(loc, sym);
    auto conditional= mk_llir_if_form(loc, sym_form, sym_form2, y);

    auto res = mk_llir_with_form(loc, 1, 1);
    res->body[0] = (llir_form*)conditional;
    // assign sym to the value of x
    res->vars[0] = sym;
    res->value_forms[0] = x;
    return (llir_form*)res;
}

llir_form* expander::expand_quote(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 2) {
        meta->err.origin = loc;
        meta->err.message = "quote requires exactly 1 argument.";
        return nullptr;
    }
    auto ws = inter->get_alloc()->add_working_set();
    auto v = inter->ast_to_value(&ws, lst[1]);
    auto id = chunk->add_constant(v);
    return (llir_form*)mk_llir_const_form(loc, id);
}

llir_form* expander::expand_set(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 3) {
        meta->err.origin = loc;
        meta->err.message = "set! requires exactly 2 argument.";
        return nullptr;
    }
    auto tar = expand_meta(lst[1], meta);
    if (!tar) {
        return nullptr;
    }
    auto val = expand_meta(lst[2], meta);
    if (!val) {
        return nullptr;
    }

    return (llir_form*)mk_llir_set_form(loc, tar, val);
}

llir_form* expander::expand_call(const source_loc& loc,
        u32 len,
        ast_form** lst,
        expander_meta* meta) {
    vector<llir_form*> pos_args;
    vector<llir_kw_arg> kw_args;
    u32 i;
    bool failed = false;
    for (i = 1; i < len; ++i) {
        if (lst[i]->kind == ak_symbol_atom
                && is_keyword(lst[i]->datum.sym)) {
            // first keyword
            break;
        } else { // positional argument
            auto x = expand_meta(lst[i], meta);
            if (!x) {
                failed = true;
                break;
            }
            pos_args.push_back(x);
        }
    }
    for (; i < len; i += 2) {
        if (lst[i]->kind == ak_symbol_atom
                && is_keyword(lst[i]->datum.sym)) {
            if (i + 1 >= len) {
                meta->err.origin = lst[i]->loc;
                meta->err.message = "Keyword without any argument.";
                failed = true;
                break;
            }
            auto x = expand_meta(lst[i+1], meta);
            if (!x) {
                failed = true;
                break;
            }
            auto name = inter->get_symtab()->symbol_name(lst[i]->datum.sym);
            auto sym = intern(name.substr(1));
            // check for duplicates
            for (auto k : kw_args) {
                if (k.nonkw_name == sym) {
                    meta->err.origin = lst[i]->loc;
                    meta->err.message = "Duplicate keyword argument.";
                    failed = true;
                    break;
                }
            }
            kw_args.push_back(llir_kw_arg{sym,x});
        } else {
            meta->err.origin = lst[i]->loc;
            meta->err.message = "Positional arguments cannot follow keyword "
                    "arguments.";
            failed = true;
            break;
        }
    }

    llir_form* callee;
    // carefully crafted conditional so expand_meta is only called if !failed
    if (failed || !(callee = expand_meta(lst[0], meta))) {
        for (auto x : kw_args) {
            free_llir_form(x.value_form);
        }
        for (auto x : pos_args) {
            free_llir_form(x);
        }
        return nullptr;
    }
    auto res = mk_llir_call_form(loc, callee, pos_args.size(), kw_args.size());
    memcpy(res->pos_args, pos_args.data(), pos_args.size()*sizeof(llir_form*));
    memcpy(res->kw_args, kw_args.data(), kw_args.size()*sizeof(llir_kw_arg));

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
    } else if (name == "defmacro") {
        return expand_defmacro(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "defn") {
        return expand_defn(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "do") {
        return expand_do(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "do-inline") {
        return expand_do_inline(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "dot") {
        return expand_dot(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "dollar-fn") {
        return expand_dollar_fn(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "if") {
        return expand_if(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "import") {
        return expand_import(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "fn") {
        return expand_fn(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "let") {
    //     return expand_let(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "letfn") {
    //     return expand_letfn(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "or") {
        return expand_or(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "quasiquote") {
    //     return expand_quasiquote(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "quote") {
        return expand_quote(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "unquote") {
    //     return expand_unquote(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "unquote-splicing") {
    //     return expand_unquote_splicing(loc, lst->list_length,
    //             lst->datum.list, meta);
    } else if (name == "set!") {
        return expand_set(loc, lst->list_length, lst->datum.list, meta);
    // } else if (name == "with") {
    //     return expand_with(loc, lst->list_length, lst->datum.list, meta);
    }

    // function calls
    return expand_call(lst->loc, lst->list_length, lst->datum.list, meta);
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

    return expand_call(lst->loc, lst->list_length, lst->datum.list, meta);
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

bool expander::is_keyword(symbol_id sym) const {
    auto name = inter->get_symtab()->symbol_name(sym);
    return name[0] == ':';
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
