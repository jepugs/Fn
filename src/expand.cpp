#include "expand.hpp"

#include "allocator.hpp"
#include "interpret.hpp"
#include "vm.hpp"

namespace fn {

using namespace fn_parse;

// Note: the istate* is currently unused, but when we eventually move array
// management into the allocator, this will change.
constant_id add_const(istate* S, function_tree* ft, value v) {
    auto x = ft->const_lookup.get(v);
    if (x.has_value()) {
        return *x;
    }
    ft->stub->const_arr.push_back(v);
    return ft->stub->const_arr.size - 1;
}

constant_id add_number_const(istate* S, function_tree* ft, f64 num) {
    return add_const(S, ft, vbox_number(num));
}

constant_id add_string_const(istate* S, function_tree* ft, const string& str) {
    push_string(S, str);
    auto res = add_const(S, ft, peek(S));
    pop(S);
    return res;
}

// expand symbols beginning with a colon to their global name
static symbol_id handle_colons(istate* S, symbol_id sid) {
    auto name = symname(S, sid);
    if (name[0] == ':') {
        auto name2 = resolve_sym(S, S->ns_id, intern(S, name.substr(1)));
        return intern(S, "#" + symname(S, name2));
    } else {
        return sid;
    }
}

static void push_quoted(istate* S, ast_form* form) {
    switch (form->kind) {
    case ak_number_atom:
        push(S, vbox_number(form->datum.num));
        break;
    case ak_string_atom:
        push_string(S, *form->datum.str);
        break;
    case ak_symbol_atom:
        push(S, vbox_symbol(handle_colons(S, form->datum.sym)));
        break;
    case ak_list:
        // FIXME: check for stack overflows
        for (u32 i = 0; i < form->list_length; ++i) {
            push_quoted(S, form->datum.list[i]);
        }
        pop_to_list(S, form->list_length);
        break;
    }
}

constant_id add_quoted_const(istate* S, function_tree* ft, ast_form* form) {
    push_quoted(S, form);
    auto res = add_const(S, ft, peek(S));
    pop(S);
    return res;
}

function_tree* add_sub_fun(istate* S, function_tree* ft) {
    auto fid = ft->stub->sub_funs.size;
    alloc_sub_stub(S, ft->stub);
    auto sub_tree = init_function_tree(S, ft->stub->sub_funs[fid]);
    ft->sub_funs.push_back(sub_tree);
    return sub_tree;
}

function_tree* init_function_tree(istate* S, function_stub* stub) {
    auto res = new function_tree;
    res->stub = stub;
    res->body = nullptr;
    return res;
}

void free_function_tree(istate* S, function_tree* ft) {
    if (ft->body != nullptr) {
        free_llir_form(ft->body);
    }
    for (auto s : ft->sub_funs) {
        free_function_tree(S, s);
    }
    delete ft;
}

bool expander::is_macro(symbol_id sym) {
    auto fqn = resolve_sym(S, S->ns_id, sym);
    return S->G->macro_tab.has_key(fqn);
}

bool expander::is_macro_call(ast_form* ast) {
    if (ast->kind == ak_list && ast->list_length > 0) {
        auto op = ast->datum.list[0];
        return op->kind == ak_symbol_atom && is_macro(op->datum.sym);
    }
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
        return (llir_form*)mk_llir_var(loc, intern("yes"));
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
    if (length < 3) {
        e_fault(loc, "apply requires at least 2 arguments.");
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

    auto value = expand_sub_fun(loc, lst[2], length-3, &lst[3], meta);
    if (!value) {
        return nullptr;
    }
    auto res = mk_llir_defmacro(loc, lst[1]->datum.sym, (llir_form*)value);
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

    auto value = expand_sub_fun(loc, lst[2], length-3, &lst[3], meta);
    if (!value) {
        return nullptr;
    }
    auto res = mk_llir_def(loc, lst[1]->datum.sym, (llir_form*)value);
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

bool expander::macroexpand_body(u32 length, ast_form** lst,
        dyn_array<ast_form*>* buf, expander_meta* meta) {
    for (u32 i = 0; i < length; ++i) {
        auto ast = lst[i];
        if (is_macro_call(ast)) {
            auto sym = ast->datum.list[0]->datum.sym;
            ast = macroexpand(ast->loc, sym, ast->list_length - 1,
                    &ast->datum.list[1], meta);
            if (!ast) {
                return false;
            }
            buf->push_back(ast);
        } else {
            buf->push_back(ast->copy());
        }
    }
    return true;
}

bool expander::flatten_do_body(u32 length,
        ast_form** lst,
        dyn_array<ast_form*>* buf,
        expander_meta* meta) {
    // expand macros first
    dyn_array<ast_form*> expanded;
    if (!macroexpand_body(length, lst, &expanded, meta)) {
        for (auto ast : expanded) {
            free_ast_form(ast);
        }
        return false;
    }
    for (auto ast : expanded) {
        if (is_do_inline(ast)) {
            flatten_do_body(ast->list_length-1, &ast->datum.list[1], buf, meta);
        } else {
            buf->push_back(ast->copy());
        }
        free_ast_form(ast);
    }
    return true;
}

llir_form* expander::expand_let_in_do(u32 length,
        ast_form** ast_body,
        expander_meta* meta) {
    auto let = ast_body[0];
    if (let->list_length == 0) {
        return (llir_form*)mk_llir_var(let->loc, intern("nil"));
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
    auto fn_form = (llir_form*)expand_sub_fun(loc, letfn_lst[2], letfn_len-3,
            &letfn_lst[3], meta);
    if (!fn_form) {
        return nullptr;
    }

    // generate llir for the with body
    dyn_array<llir_form*> bodys;
    if (!expand_do_recur(length - 1, &ast_body[1], &bodys, meta)) {
        free_llir_form(fn_form);
        return nullptr;
    }
    auto res = mk_llir_with(loc, 1, bodys.size);
    res->vars[0] = letfn_lst[1]->datum.sym;
    res->values[0] = fn_form;
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
    if (length == 0) {
        return true;
    }
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
    return expand_body(loc, length - 1, &lst[1], meta);
}

llir_form* expander::expand_body(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 0) {
        return (llir_form*)mk_llir_var(loc, intern("nil"));
    } else if (length == 1) {
        return expand_meta(lst[0], meta);
    }

    dyn_array<ast_form*> ast_buf;
    flatten_do_body(length, lst, &ast_buf, meta);
    llir_form* res;

    // check if first form is let or letfn to avoid wrapping with an empty with
    if (is_let(ast_buf[0])) {
        res = expand_let_in_do(ast_buf.size, ast_buf.data, meta);
    } else if (is_letfn(ast_buf[0])) {
        res = expand_letfn_in_do(ast_buf.size, ast_buf.data, meta);
    } else {
        dyn_array<llir_form*> llir_buf;
        if (!expand_do_recur(ast_buf.size, ast_buf.data, &llir_buf, meta)) {
            for (auto ast : ast_buf) {
                free_ast_form(ast);
            }
            return nullptr;
        }
        auto w = mk_llir_with(loc, 0, llir_buf.size);
        for (u32 i = 0; i < llir_buf.size; ++i) {
            w->body[i] = llir_buf[i];
        }
        res = (llir_form*) w;
    }
    for (auto ast : ast_buf) {
        free_ast_form(ast);
    }
    return res;
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

    // expand the thing in a new subtree
    auto sub_tree = add_sub_fun(S, ft);
    expander_meta meta2 = {.max_dollar_sym=-1};
    auto tmp = ft;
    ft = sub_tree;
    sub_tree->body = expand_meta(lst[1], &meta2);
    ft = tmp;
    if (!sub_tree->body) {
        return nullptr;
    }

    // add parameters to the new function
    auto m = meta2.max_dollar_sym;
    sub_tree->stub->num_params = m+1;
    sub_tree->stub->num_opt = 0;
    for (u32 i = 0; i < sub_tree->stub->num_params; ++i) {
        sub_tree->params.push_back(intern("$" + std::to_string(i)));
    }

    // function id
    auto fid = ft->sub_funs.size - 1;
    auto res = mk_llir_fn(loc, fid, 0);
    if (m >= 0) {
        // add in the $ symbol in the function body
        auto body2 = mk_llir_with(loc, 1, 1);
        body2->vars[0] = intern("$");
        body2->values[0] = (llir_form*)mk_llir_var(sub_tree->body->origin,
                intern("$0"));
        body2->body[0] = sub_tree->body;
        sub_tree->body = (llir_form*)body2;
    }

    return (llir_form*) res;
}

llir_form* expander::expand_dot(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length != 3) {
        e_fault(loc, "dot requires exactly 2 arguments.");
        return nullptr;
    } else if (!lst[2]->is_symbol()) {
        e_fault(loc, "dot keys must be symbols.");
        return nullptr;
    }

    auto x = expand_meta(lst[1], meta);
    if (!x) {
        return nullptr;
    }
    auto res = mk_llir_dot(loc, x, lst[2]->datum.sym);

    return (llir_form*) res;
}

llir_fn* expander::expand_sub_fun(const source_loc& loc,
        ast_form* params,
        u32 body_length,
        ast_form** body,
        expander_meta* meta) {
    if (params->kind != ak_list) {
        e_fault(params->loc, "fn requires a parameter list.");
        return nullptr;
    }

    // create a new function stub
    auto sub_tree = add_sub_fun(S, ft);

    auto tmp = ft;
    // set ft so that constants get written to the right place
    ft = sub_tree;
    // expand the body using expand_do
    sub_tree->body = expand_body(loc, body_length, body, meta);
    ft = tmp;
    if (!sub_tree->body) {
        return nullptr;
    }

    // process parameter list
    auto len = params->list_length;
    auto amp_sym = intern("&");
    auto& lst = params->datum.list;
    // FIXME: check for legal names

    dyn_array<symbol_id> positional;
    u32 num_pos;
    dyn_array<llir_form*> inits;
    // positional/required
    for (num_pos = 0; num_pos < len; ++num_pos) {
        auto& x = lst[num_pos];
        if (!x->is_symbol()) {
            if (x->kind != ak_list) {                
                e_fault(x->loc,
                        "Malformed item in parameter list.");
                return nullptr;
            }
            break;
        }
        auto& sym = x->datum.sym;
        if (sym == amp_sym) {
            break;
        }
        positional.push_back(sym);
    }
    u32 num_req = num_pos;

    // optional
    if (num_pos < len && lst[num_pos]->kind == ak_list) {
        // skip to the varargs test
        for (; num_pos < len; ++num_pos) {
            auto& x = lst[num_pos];
            if (x->kind == ak_symbol_atom) {
                if (x->datum.sym == amp_sym) {
                    break;
                } else {
                    e_fault(x->loc, "Malformed item in parameter list.");
                    return nullptr;
                }
            } else if (x->kind != ak_list
                    || x->list_length != 2
                    || !x->datum.list[0]->is_symbol()) {
                // clean up
                for (auto x : inits) {
                    free_llir_form(x);
                }
                e_fault(x->loc,
                        "Malformed optional parameter in parameter list.");
                return nullptr;
            }
            auto y = expand_meta(x->datum.list[1], meta);
            if (!y) {
                for (auto x : inits) {
                    free_llir_form(x);
                }
                return nullptr;
            }
            inits.push_back(y);
            positional.push_back(x->datum.list[0]->datum.sym);
        }
    }

    bool has_var_list = false;
    symbol_id vl_param;
    if (num_pos < len) {
        // at this point, there would have been an error already if anything
        // other than the ampersand symbol was next.
        if (len - num_pos != 2
            || lst[num_pos+1]->kind != ak_symbol_atom) {
            for (auto x : inits) {
                free_llir_form(x);
            }
            e_fault(lst[num_pos]->loc,
                    "Malformed variadic parameter in parameter list.");
            return nullptr;
        } else {
            has_var_list = true;
            vl_param = lst[num_pos+1]->datum.sym;
        }
    }

    // FIXME: check if we exceed maximum numbers anywhere
    auto sub_stub = sub_tree->stub;
    sub_stub->num_params = positional.size;
    sub_stub->num_opt = positional.size - num_req;
    sub_stub->vari = has_var_list;
    sub_tree->params = positional;
    if (has_var_list) {
        sub_tree->params.push_back(vl_param);
    }

    // function id is its index in the array
    constant_id fid = ft->sub_funs.size - 1;
    auto res = mk_llir_fn(loc, fid, positional.size - num_req);
    for (u32 i = 0; i < inits.size; ++i) {
        res->inits[i] = inits[i];
    }
    return res;
}

llir_form* expander::expand_fn(const source_loc& loc,
        u32 length,
        ast_form** lst,
        expander_meta* meta) {
    if (length == 1) {
        e_fault(loc, "fn requires a parameter list.");
        return nullptr;
    }

    return (llir_form*)expand_sub_fun(loc, lst[1], length-2, &lst[2], meta);
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
        return (llir_form*)mk_llir_var(loc, intern("no"));
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
        auto id = add_quoted_const(S, ft, form);
        return (llir_form*)mk_llir_const(form->loc, id);
    } else if (is_operator_list("unquote", form)) {
        if (form->list_length != 2) {
            e_fault(form->loc, "unquote requires exactly 1 argument.");
        }
        return expand_meta(form->datum.list[1], meta);
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
            list_args.size);
    for (u32 i = 0; i < list_args.size; ++i) {
        res->args[i] = list_args[i];
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
                conc_args.size);
        for (u32 i = 0; i < conc_args.size; ++i) {
            res->args[i] = conc_args[i];
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
    auto id = add_quoted_const(S, ft, lst[1]);
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
        free_llir_form(tar);
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

    dyn_array<llir_form*> args;
    u32 i;
    for (i = 1; i < len; ++i) {
        auto x = expand_meta(lst[i], meta);
        if (!x) {
            for (auto x : args) {
                free_llir_form(x);
            }
            return nullptr;
        }
        args.push_back(x);
    }

    llir_form* callee = expand_meta(lst[0], meta);
    // carefully crafted conditional so expand_meta is only called if !failed
    if (!callee) {
        for (auto x : args) {
            free_llir_form(x);
        }
        return nullptr;
    }

    auto res = mk_llir_call(loc, callee, args.size);
    memcpy(res->args, args.data, args.size*sizeof(llir_form*));

    return (llir_form*)res;
}

llir_form* expander::expand_symbol_list(ast_form* lst, expander_meta* meta) {
    auto& loc = lst->loc;
    auto sym = lst->datum.list[0]->datum.sym;
    // first check for macros
    if (is_macro(sym)) {
        auto form = macroexpand(loc, sym, lst->list_length - 1,
                &lst->datum.list[1], meta);
        if (!form) {
            return nullptr;
        }
        auto res = expand_meta(form, meta);
        free_ast_form(form);
        return res;
    }

    auto name = (*S->symtab)[sym];
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

ast_form* expander::macroexpand1(const source_loc& loc, symbol_id name,
        u32 num_args, ast_form** args, expander_meta* meta) {
    push_macro(S, resolve_sym(S, S->ns_id, name));
    for (u32 i = 0; i < num_args; ++i) {
        push_quoted(S, args[i]);
    }
    call(S, num_args);
    if (S->err_happened) {
        return nullptr;
    }
    return pop_syntax(S, loc);
}

ast_form* expander::macroexpand(const source_loc& loc, symbol_id name,
        u32 num_args, ast_form** args, expander_meta* meta) {
    auto ast = macroexpand1(loc, name, num_args, args, meta);
    // macroexpand recursively
    do {
        if (!ast) {
            return nullptr;
        } else if (is_macro_call(ast)) {
            auto tmp = ast;
            auto sym = ast->datum.list[0]->datum.sym;
            ast = macroexpand1(ast->loc, sym, ast->list_length - 1,
                    &ast->datum.list[1], meta);
            free_ast_form(tmp);
        } else {
            break;
        }
    } while (true);
    return ast;
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
    switch (ast->kind) {
    case ak_number_atom: {
        auto c = add_number_const(S, ft, ast->datum.num);
        return (llir_form*)mk_llir_const(ast->loc, c);
    }
    case ak_string_atom: {
        auto c = add_string_const(S, ft, *ast->datum.str);
        return (llir_form*)mk_llir_const(ast->loc, c);
    }
    case ak_symbol_atom:
        update_dollar_syms(S->symtab, ast->datum.sym, meta);
        return (llir_form*)mk_llir_var(ast->loc, handle_colons(S, ast->datum.sym));
    case ak_list:
        return expand_list(ast, meta);
    }
    // this is never reached, but it shuts up the compiler
    return nullptr;
}

symbol_id expander::intern(const string& str) {
    return fn::intern(S, str);
}

symbol_id expander::gensym() {
    return S->symtab->gensym();
}

string expander::symbol_name(symbol_id name) {
    return S->symtab->symbol_name(name);
}

bool expander::is_keyword(symbol_id sym) const {
    auto name = S->symtab->symbol_name(sym);
    return name[0] == ':';
}

void expander::e_fault(const source_loc& loc, const string& msg) {
    // FIXME: incorporate source location here
    ierror(S, msg);
}

void expand(istate* S, function_tree* ft, ast_form* ast) {
    expander ex{S, ft};
    expander_meta m;
    m.max_dollar_sym = -1;
    ft->body = ex.expand_meta(ast, &m);
}

}
