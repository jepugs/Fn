#include "expand.hpp"

#include "interpret.hpp"

namespace fn {

using namespace fn_parse;

bool expander::is_macro(symbol_id sym) {
    auto ns = inter->get_global_env()->get_ns(chunk->ns_id);
    if (!ns.has_value()) {
        return false;
    } else {
        return (*ns)->get_macro(sym).has_value();
    }
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
        return (llir_form*)mk_llir_var(loc, intern("true"));
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
    // create two forms for symbol b/c of how the if is deleted
    auto sym_form = (llir_form*)mk_llir_var(loc, sym);
    auto sym_form2 = (llir_form*)mk_llir_var(loc, sym);
    auto conditional = mk_llir_if(loc, sym_form, y, sym_form2);

    auto res = mk_llir_with(loc, 1, 1);
    res->body[0] = (llir_form*)conditional;
    // assign sym to the value of x
    res->vars[0] = sym;
    res->values[0] = x;
    return (llir_form*)res;
}

llir_form* expander::expand_apply(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 4) {
        e_fault(loc, "apply requires at least 3 arguments.");
        return nullptr;
    }

    auto callee = expand_meta(lst[1], meta);
    if (!callee) {
        return nullptr;
    }

    dyn_array<llir_form*> args;
    for (u32 i = 2; i < length; ++i) {
        auto x = expand_meta(lst[i], meta);
        if (!x) {
            for (auto y : args) {
                free_llir_form(y);
            }
            free_llir_form(callee);
            return nullptr;
        }
        args.push_back(x);
    }

    auto res = mk_llir_apply(loc, callee, length - 2);
    for (u32 i = 0; i + 2 < length; ++i) {
        res->args[i] = args[i];
    }
    return (llir_form*) res;
}

// see note for expand_and
llir_form* expander::expand_cond(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if ((length & 1) != 1) {
        e_fault(loc, "Odd number of arguments to cond.");
        return nullptr;
    } else if (length == 1) {
        return (llir_form*)mk_llir_var(loc, intern("nil"));
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

    return (llir_form*)mk_llir_if(loc, x, y, z);
}

llir_form* expander::expand_def(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 3) {
        e_fault(loc, "def requires exactly 2 arguments.");
        return nullptr;
    }
    if (lst[1]->kind != ak_symbol_atom) {
        e_fault(lst[1]->loc, "First argument to def not a symbol.");
        return nullptr;
    }

    auto x = expand_meta(lst[2], meta);
    if (!x) {
        return nullptr;
    }

    return (llir_form*)mk_llir_def(loc, lst[1]->datum.sym, x);
}

// see comments for expand_defn
llir_form* expander::expand_defmacro(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        e_fault(loc, "defmacro requires at least 2 arguments.");
        return nullptr;
    } else if (!lst[1]->is_symbol()) {
        e_fault(lst[1]->loc, "First argument to defmacro must be a symbol.");
        return nullptr;
    }

    auto body = expand_do(loc, length-2, &lst[2], meta);
    if(!body) {
        return nullptr;
    }

    llir_fn_params params;
    if(!expand_params(lst[2], &params, meta)) {
        free_llir_form(body);
        return nullptr;
    }

    auto value = (llir_form*)mk_llir_fn(loc, params,
            symbol_name(lst[1]->datum.sym), body);
    auto res = mk_llir_defmacro(loc, lst[1]->datum.sym, value);
    return (llir_form*)res;
}

llir_form* expander::expand_defn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        e_fault(loc, "defn requires at least 2 arguments.");
        return nullptr;
    } else if (!lst[1]->is_symbol()) {
        e_fault(loc, "First argument to defn must be a symbol.");
        return nullptr;
    }

    // do expects the first element to an operator, so we put &lst[2] since we
    // have the first body expression at &lst[3]
    auto body = expand_do(loc, length-2, &lst[2], meta);
    if(!body) {
        return nullptr;
    }

    llir_fn_params params;
    if(!expand_params(lst[2], &params, meta)) {
        free_llir_form(body);
        return nullptr;
    }

    auto value = (llir_form*)mk_llir_fn(loc, params,
            symbol_name(lst[1]->datum.sym), body);
    auto res = mk_llir_def(loc, lst[1]->datum.sym, value);
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
        dyn_array<ast_form*>* buf,
        expander_meta* meta) {
    for (u32 i = 1; i < length; ++i) {
        if (is_do_inline(lst[i])) {
            // this will just splice in the other forms
            flatten_do_body(lst[i]->list_length, lst[i]->datum.list, buf, meta);
        } else {
            buf->push_back(lst[i]);
        }
    }
}

llir_form* expander::expand_let_in_do(u32 length,
        ast_form** ast_body,
        expander_meta* meta) {
    auto let = ast_body[0];
    if (let->list_length == 0) {
        return (llir_form*)mk_llir_var(let->loc,
                inter->get_symtab()->intern("nil"));
    } else if ((let->list_length & 1) != 1) {
        e_fault(let->loc, "Odd number of arguments to let.");
        return nullptr;
    }

    // body
    dyn_array<llir_form*> bodys;
    if (!expand_do_recur(length - 1, &ast_body[1], &bodys, meta)) {
        return nullptr;
    }

    auto num_vars = (let->list_length - 1) / 2;
    auto res = mk_llir_with(let->loc, num_vars, bodys.size);

    // collect variables and init forms
    for (u32 i = 0; i < num_vars; ++i) {
        auto var = let->datum.list[2*i+1];
        if (var->kind != ak_symbol_atom) {
            e_fault(var->loc, "let binding names must be symbols.");
            for (u32 j = 0; j < i; ++j) {
                free_llir_form(res->values[i]);
            }
            for (auto x : bodys) {
                free_llir_form(x);
            }
            return nullptr;
        }

        res->vars[i] = var->datum.sym;
        auto x = expand_meta(let->datum.list[2*i+2], meta);
        if (!x) {
            for (u32 j = 0; j < i; ++j) {
                free_llir_form(res->values[i]);
            }
            for (auto x : bodys) {
                free_llir_form(x);
            }
            return nullptr;
        }
        res->values[i] = x;
    }

    for (u32 i = 0; i < bodys.size; ++i) {
        res->body[i] = bodys[i];
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
        e_fault(loc, "letfn requires at least two arguments.");
        return nullptr;
    } else if (!letfn_lst[1]->is_symbol()) {
        e_fault(letfn_lst[1]->loc, "letfn name must be a symbol.");
        return nullptr;
    }

    // generate llir for the new function
    auto sym = letfn_lst[1]->datum.sym;
    auto fn_body = expand_do(loc, letfn_len-2, &letfn_lst[2],meta);
    if (!fn_body) {
        return nullptr;
    }

    llir_fn_params params;
    if (!expand_params(letfn_lst[2], &params, meta)) {
        free_llir_form(fn_body);
        return nullptr;
    }

    // FIXME: function names should reflect surrounding functions. Should also
    // do this for other types of functions (even anonymous ones)
    auto fn_llir = (llir_form*)mk_llir_fn(loc, params,
            ":"+symbol_name(sym), fn_body);

    // generate llir for the with body
    dyn_array<llir_form*> bodys;
    if (!expand_do_recur(length - 1, &ast_body[1], &bodys, meta)) {
        free_llir_form(fn_llir);
        return nullptr;
    }
    auto res = mk_llir_with(loc, 1, bodys.size);
    res->vars[0] = sym;
    res->values[0] = fn_llir;
    for (u32 i = 0; i < bodys.size; ++i) {
        res->body[i] = bodys[i];
    }

    return (llir_form*) res;
}


// This function fills out the provided vector with llir forms that make up the
// do body. ast_body here should point to lst[1] of the original do form.
// Returns false on error. This also cleans up all the forms in the buffer, so
// if passing in a nonempty buffer, your llir_form*s will get deleted on error.
bool expander::expand_do_recur(u32 length,
        ast_form** ast_body,
        dyn_array<llir_form*>* buf,
        expander_meta* meta) {
    for (u32 i = 0; i < length; ++i) {
        auto ast = ast_body[i];
        if (is_let(ast)) {
            auto body = expand_let_in_do(length-i, &ast_body[i], meta);
            if (!body) {
                for (auto y : *buf) {
                    free_llir_form(y);
                }
                return false;
            }
            buf->push_back(body);
            return true;
        } else if (is_letfn(ast)) {
            auto body = expand_letfn_in_do(length-i, &ast_body[i], meta);
            if (!body) {
                for (auto y : *buf) {
                    free_llir_form(y);
                }
                return false;
            }
            buf->push_back(body);
            return true;
        } else {
            auto x = expand_meta(ast, meta);
            if (!x) {
                for (auto y : *buf) {
                    free_llir_form(y);
                }
                return false;
            }
            buf->push_back(x);
        }
    }
    return true;
}

llir_form* expander::expand_do(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        return (llir_form*)mk_llir_var(loc, intern("nil"));
    } else if (length == 2) {
        return expand_meta(lst[1], meta);
    }

    dyn_array<ast_form*> ast_buf;
    flatten_do_body(length, lst, &ast_buf, meta);

    // check if first form is let or letfn to avoid wrapping with an empty with
    if (length == 0) {
        return (llir_form*)mk_llir_var(loc, intern("nil"));
    } else if (is_let(ast_buf[0])) {
        return expand_let_in_do(ast_buf.size, ast_buf.data, meta);
    } else if (is_letfn(ast_buf[0])) {
        return expand_letfn_in_do(ast_buf.size, ast_buf.data, meta);
    }
    dyn_array<llir_form*> llir_buf;
    if (!expand_do_recur(ast_buf.size, ast_buf.data, &llir_buf, meta)) {
        return nullptr;
    }
    auto res = mk_llir_with(loc, 0, llir_buf.size);
    for (u32 i = 0; i < llir_buf.size; ++i) {
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
        e_fault(loc, "dollar-fn requires exactly 1 argument.");
        return nullptr;
    }

    expander_meta meta2 = {.max_dollar_sym=-1};
    auto body = expand_meta(lst[1], &meta2);
    if (!body) {
        return nullptr;
    }

    auto m = meta2.max_dollar_sym;
    if (m >= 0) {
        auto body2 = mk_llir_with(body->origin, 1, 1);
        body2->vars[0] = intern("$");
        body2->values[0] = (llir_form*)mk_llir_var(body->origin, intern("$0"));
        body2->body[0] = body;
        body = (llir_form*) body2;
    }

    auto res = mk_llir_fn(loc, m+1, false, false, m+1, "", body);
    for (i32 i = 0; i < m+1; ++i) {
        res->params.pos_args[i] = intern("$" + std::to_string(i));
    }

    return (llir_form*) res;
}

llir_form* expander::expand_dot(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        e_fault(loc, "dot requires at least 2 arguments.");
        return nullptr;
    }

    auto x = expand_meta(lst[1], meta);
    if (!x) {
        return nullptr;
    }

    auto res = mk_llir_dot(loc, x, length - 2);
    for (u32 i = 0; i + 2 < length; ++i) {
        if (!lst[i+2]->is_symbol()) {
            e_fault(loc, "dot keys must be symbols.");
            free_llir_dot(res);
            return nullptr;
        } else {
            res->keys[i] = lst[i+2]->datum.sym;
        }
    }
    return (llir_form*) res;
}

bool expander::expand_params(ast_form* ast,
        llir_fn_params* params,
        expander_meta* meta) {
    const auto& loc = ast->loc;
    if (ast->kind != ak_list) {
        e_fault(loc, "fn requires a parameter list.");
        return false;
    }

    auto len = ast->list_length;
    auto amp_sym = intern("&");
    auto colamp_sym = intern(":&");
    auto& lst = ast->datum.list;

    dyn_array<symbol_id> positional;
    u32 num_pos;
    dyn_array<llir_form*> inits;
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
            for (auto x : inits) {
                free_llir_form(x);
            }
            e_fault(x->loc,
                    "Malformed optional parameter in parameter list.");
            return false;
        }
        auto y = expand_meta(x->datum.list[1], meta);
        if (!y) {
            for (auto x : inits) {
                free_llir_form(x);
            }
            return false;
        }
        inits.push_back(y);
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
        for (auto x : inits) {
            free_llir_form(x);
        }
        e_fault(loc, "Malformed parameter list.");
        return false;
    }

    // TODO: check if we exceed maximum arguments
    params->num_pos_args = num_pos;
    params->pos_args = new symbol_id[num_pos];
    params->req_args = num_req;
    params->inits = new llir_form*[num_pos - num_req];
    for (u32 i = 0; i < positional.size; ++i) {
        params->pos_args[i] = positional[i];
    }
    for (u32 i = 0; i < inits.size; ++i) {
        params->inits[i] = inits[i];
    }
    return true;
}

llir_form* expander::expand_fn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        e_fault(loc, "fn requires a parameter list.");
        return nullptr;
    }
    llir_form* body;
    if (length == 2) {
        body = (llir_form*)mk_llir_var(loc, intern("nil"));
    } else {
        body = (llir_form*)expand_do(loc, length-1, &lst[1], meta);
        if (!body) {
            return nullptr;
        }
    }

    llir_fn_params params;
    if (!expand_params(lst[1], &params, meta)) {
        return nullptr;
    }

    return (llir_form*)mk_llir_fn(loc, params, "", body);
}

llir_form* expander::expand_if(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 4) {
        e_fault(loc, "if requires exactly 3 arguments.");
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

    return (llir_form*)mk_llir_if(loc, x, y, z);
}

llir_form* expander::expand_import(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 2) {
        e_fault(loc, "import requires exactly 1 argument.");
        return nullptr;
    } else if (lst[1]->kind != ak_symbol_atom) {
        e_fault(loc, "import argument must be a symbol.");
        return nullptr;
    }
    return (llir_form*)mk_llir_import(loc, lst[1]->datum.sym);
}

llir_form* expander::expand_let(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if ((length & 1) != 1) {
        e_fault(loc, "Odd number of arguments to let.");
        return nullptr;
    }
    // unlike letfn, we actually have to evaluate code in case there are
    // side-effects.
    for (u32 i = 1; i < length; i += 2) {
    }
    auto num_vars = (length - 1) / 2;
    auto res = mk_llir_with(loc, num_vars, 0);

    // collect variables and init forms
    for (u32 i = 0; i < num_vars; ++i) {
        auto var = lst[2*i+1];
        if (var->kind != ak_symbol_atom) {
            e_fault(var->loc, "let binding names must be symbols.");
            for (u32 j = 0; j < i; ++j) {
                free_llir_form(res->values[i]);
            }
            return nullptr;
        }

        res->vars[i] = var->datum.sym;
        auto x = expand_meta(lst[2*i+2], meta);
        if (!x) {
            for (u32 j = 0; j < i; ++j) {
                free_llir_form(res->values[i]);
            }
            return nullptr;
        }
        res->values[i] = x;
    }
    return (llir_form*)res;
}

llir_form* expander::expand_letfn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length < 3) {
        e_fault(loc, "letfn requires at least 2 arguments.");
        return nullptr;
    }
    // There's no way for the created function to leave the letfn, so this means
    // we can actually skip everything and just return nil
    return (llir_form*)mk_llir_var(loc, intern("nil"));
}

llir_form* expander::expand_or(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        return (llir_form*)mk_llir_var(loc, intern("false"));
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
    auto sym_form = (llir_form*)mk_llir_var(loc, sym);
    auto sym_form2 = (llir_form*)mk_llir_var(loc, sym);
    auto conditional= mk_llir_if(loc, sym_form, sym_form2, y);

    auto res = mk_llir_with(loc, 1, 1);
    res->body[0] = (llir_form*)conditional;
    // assign sym to the value of x
    res->vars[0] = sym;
    res->values[0] = x;
    return (llir_form*)res;
}

bool expander::is_unquote(ast_form* ast) {
    return is_operator_list("unquote", ast);
}

bool expander::is_unquote_splicing(ast_form* ast) {
    return is_operator_list("unquote-splicing", ast);
}

llir_form* expander::quasiquote_expand_recur(ast_form* form,
        expander_meta* meta) {
    if (form->kind != ak_list) {
        auto ws = inter->get_alloc()->add_working_set();
        auto v = inter->ast_to_value(&ws, form);
        auto id = chunk->add_constant(v);
        return (llir_form*)mk_llir_const(form->loc, id);
    } else {
        return expand_quasiquote_list(form->loc, form->list_length,
                form->datum.list, meta);
    }
}

// next list to concatenate
llir_form* expander::quasiquote_next_conc_arg(const source_loc& loc,
        u32 length,
        ast_form** lst,
        u32* stopped_at,
        expander_meta* meta) {
    dyn_array<llir_form*> list_args;

    if (is_unquote_splicing(lst[0])) {
        if (lst[0]->list_length != 2) {
            e_fault(loc, "unquote-splicing requires exactly 1 argument.");
            return nullptr;
        }
        *stopped_at = 1;
        return expand_meta(lst[0]->datum.list[1], meta);
    }

    u32 i;
    for (i = 0; i < length; ++i) {
        if (is_unquote(lst[i])) {
            if (lst[i]->list_length != 2) {
                e_fault(loc, "unquote requires exactly 1 argument.");
                for (auto x : list_args) {
                    free_llir_form(x);
                }
                return nullptr;
            }

            auto x = expand_meta(lst[i]->datum.list[1], meta);
            if (!x) {
                return nullptr;
            }
            list_args.push_back(x);
        } else if (is_unquote_splicing(lst[i])) {
            break;
        } else {
            auto x = quasiquote_expand_recur(lst[i], meta);
            if (!x) {
                return nullptr;
            }
            list_args.push_back(x);
        }
    }
    *stopped_at = i;

    auto res = mk_llir_call(loc, (llir_form*)mk_llir_var(loc, intern("List")),
            list_args.size, 0);
    for (u32 i = 0; i < list_args.size; ++i) {
        res->pos_args[i] = list_args[i];
    }
    return (llir_form*) res;
}

llir_form* expander::expand_quasiquote_list(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    dyn_array<llir_form*> conc_args;
    u32 i = 0;
    while (i < length) {
        u32 stopped_at;
        auto x = quasiquote_next_conc_arg(loc, length-i, &lst[i],
                &stopped_at, meta);
        if (!x) {
            for (auto y : conc_args) {
                free_llir_form(y);
            }
            return nullptr;
        }
        conc_args.push_back(x);
        i += stopped_at;
    }

    if (conc_args.size == 1) {
        auto res = conc_args[0];
        return (llir_form*) res;
    } else {
        auto res = mk_llir_call(loc,
                (llir_form*)mk_llir_var(loc, intern("concat")),
                conc_args.size, 0);
        for (u32 i = 0; i < conc_args.size; ++i) {
            res->pos_args[i] = conc_args[i];
        }
        return (llir_form*) res;
    }
}

llir_form* expander::expand_quasiquote(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 2) {
        e_fault(loc, "quasiquote requires exactly 1 argument.");
        return nullptr;
    }
    if (lst[1]->kind != ak_list) {
        return expand_quote(loc, length, lst, meta);
    } else {
        return expand_quasiquote_list(loc, lst[1]->list_length,
                lst[1]->datum.list, meta);
    }
}

llir_form* expander::expand_quote(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 2) {
        e_fault(loc, "quote requires exactly 1 argument.");
        return nullptr;
    }
    auto ws = inter->get_alloc()->add_working_set();
    auto v = inter->ast_to_value(&ws, lst[1]);
    auto id = chunk->add_constant(v);
    return (llir_form*)mk_llir_const(loc, id);
}

llir_form* expander::expand_set(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 3) {
        e_fault(loc, "set! requires exactly 2 argument.");
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

    return (llir_form*)mk_llir_set(loc, tar, val);
}

llir_form* expander::expand_unquote(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    e_fault(loc, "unquote outside of quasiquote.");
    return nullptr;
}

llir_form* expander::expand_unquote_splicing(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    e_fault(loc, "unquote-splicing outside of quasiquote.");
    return nullptr;
}

llir_form* expander::expand_with(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (lst[1]->kind != ak_list) {
        e_fault(loc, "with binding form must be a list.");
        return nullptr;
    } else if ((lst[1]->list_length & 1) != 0) {
        e_fault(loc, "Odd number of items in with binding form.");
        return nullptr;
    }

    auto lst2 = lst[1]->datum.list;
    u8 num_vars = (u8)(lst[1]->list_length/2);
    auto res = mk_llir_with(loc, num_vars, length - 2);

    // bindings
    for (u32 i = 0; i < num_vars; ++i) {
        if (!lst2[2*i]->is_symbol()) {
            e_fault(loc, "with binding names must be symbols.");
            return nullptr;
        }

        res->vars[i] = lst2[2*i]->datum.sym;
        auto x = expand_meta(lst2[2*i + 1], meta);
        if (x == nullptr) {
            for (u32 j = 0; j < i; ++ j) {
                free_llir_form(res->values[i]);
            }
            return nullptr;
        }

        res->values[i] = x;
    }

    // body
    for (u32 i = 2; i < length; ++i) {
        auto x = expand_meta(lst[i], meta);
        if (x == nullptr) {
            for (u32 j = 2; j < i; ++ j) {
                free_llir_form(res->body[j]);
            }
            return nullptr;
        }
        res->body[i-2] = x;
    }

    return (llir_form*) res;
}

llir_form* expander::expand_call(const source_loc& loc,
        u32 len,
        ast_form** lst,
        expander_meta* meta) {
    dyn_array<llir_form*> pos_args;
    dyn_array<llir_kw_arg> kw_args;
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
    if (failed) {
        for (auto x : pos_args) {
            free_llir_form(x);
        }
        return nullptr;
    }
    for (; i < len; i += 2) {
        if (lst[i]->kind == ak_symbol_atom
                && is_keyword(lst[i]->datum.sym)) {
            if (i + 1 >= len) {
                e_fault(lst[i]->loc, "Keyword without any argument.");
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
                    e_fault(lst[i]->loc, "Duplicate keyword argument.");
                    failed = true;
                    break;
                }
            }
            kw_args.push_back(llir_kw_arg{sym,x});
        } else {
            e_fault(lst[i]->loc,
                    "Positional arguments cannot follow keyword arguments.");
            failed = true;
            break;
        }
    }

    llir_form* callee;
    // carefully crafted conditional so expand_meta is only called if !failed
    if (failed || !(callee = expand_meta(lst[0], meta))) {
        for (auto x : kw_args) {
            free_llir_form(x.value);
        }
        for (auto x : pos_args) {
            free_llir_form(x);
        }
        return nullptr;
    }
    auto res = mk_llir_call(loc, callee, pos_args.size, kw_args.size);
    memcpy(res->pos_args, pos_args.data, pos_args.size*sizeof(llir_form*));
    memcpy(res->kw_args, kw_args.data, kw_args.size*sizeof(llir_kw_arg));

    return (llir_form*)res;
}

llir_form* expander::expand_symbol_list(ast_form* lst, expander_meta* meta) {
    auto& loc = lst->loc;
    auto sym = lst->datum.list[0]->datum.sym;
    // first check for macros
    if (is_macro(sym)) {
        auto ast = inter->expand_macro(sym, chunk->ns_id,
                lst->list_length - 1, &lst->datum.list[1], loc, err);
        if (!ast) {
            err->message = "(During macroexpansion): " + err->message;
            return nullptr;
        }
        auto res = expand_meta(ast, meta);
        free_ast_form(ast);
        return res;
    }

    auto name = (*inter->get_symtab())[sym];
    // special forms
    if (name == "and") {
        return expand_and(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "apply") {
        return expand_apply(loc, lst->list_length, lst->datum.list, meta);
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
    } else if (name == "let") {
        return expand_let(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "letfn") {
        return expand_letfn(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "or") {
        return expand_or(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "quasiquote") {
        return expand_quasiquote(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "quote") {
        return expand_quote(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "unquote") {
        return expand_unquote(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "unquote-splicing") {
        return expand_unquote_splicing(loc, lst->list_length,
                lst->datum.list, meta);
    } else if (name == "set!") {
        return expand_set(loc, lst->list_length, lst->datum.list, meta);
    } else if (name == "with") {
        return expand_with(loc, lst->list_length, lst->datum.list, meta);
    }

    // function calls
    return expand_call(lst->loc, lst->list_length, lst->datum.list, meta);
}

llir_form* expander::expand_list(ast_form* lst, expander_meta* meta) {
    auto& loc = lst->loc;
    if (lst->list_length == 0) {
        e_fault(loc, "() is not a legal expression");
        return nullptr;
    }

    auto op = lst->datum.list[0];
    switch (op->kind) {
    case ak_symbol_atom:
        return expand_symbol_list(lst, meta);
    case ak_number_atom:
        e_fault(loc, "Cannot call numbers as functions.");
        return nullptr;
    case ak_string_atom:
        e_fault(loc, "Cannot call strings as functions.");
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
    i16 value = -1;
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
        c = chunk->add_constant(vbox_number(ast->datum.num));
        return (llir_form*)mk_llir_const(loc, c);
    case ak_string_atom:
        c = chunk->add_constant(ws.add_string(ast->datum.str->data));
        return (llir_form*)mk_llir_const(loc, c);
    case ak_symbol_atom:
        update_dollar_syms(inter->get_symtab(), ast->datum.sym, meta);
        return (llir_form*)mk_llir_var(loc, ast->datum.sym);
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

string expander::symbol_name(symbol_id name) {
    return inter->get_symtab()->symbol_name(name);
}

bool expander::is_keyword(symbol_id sym) const {
    auto name = inter->get_symtab()->symbol_name(sym);
    return name[0] == ':';
}

void expander::e_fault(const source_loc& loc, const string& msg) {
    set_fault(err, loc, "expand", msg);
}

expander::expander(interpreter* inter, code_chunk* const_chunk)
    : inter{inter}
    , chunk{const_chunk} {
}

llir_form* expander::expand(ast_form* ast, fault* err) {
    this->err = err;
    expander_meta m;
    m.max_dollar_sym = -1;
    return expand_meta(ast, &m);
}

}
