#include "compile2.hpp"

namespace fn {

using namespace fn_parse;
using namespace fn_bytes;

local_table::local_table(local_table* parent, func_stub* func)
    : vars{}
    , parent{parent}
    , cur_func{func}
    , sp{0} {
    if (parent != nullptr) {
        sp = parent->sp;
    }
}

u8 local_table::add_upvalue(u32 levels, u8 pos) {
    auto call = this;
    while (call->cur_func == nullptr && call != nullptr) {
        call = call->parent;
    }

    // levels == 1 => this is a direct upvalue, so add it and return
    if (levels == 1) {
        return call->cur_func->get_upvalue(pos, true);
    }

    // levels > 1 => need to get the upvalue from an enclosing function
    u8 slot = call->parent->add_upvalue(levels - 1, pos);
    return call->cur_func->get_upvalue(slot, false);
}

void compiler::compile_subexpr(local_table& locals, const ast_node* expr) {
    if (expr->kind == ak_atom) {
        compile_atom(locals, *expr->datum.atom, expr->loc);
    } else if (expr->kind == ak_list) {
        compile_list(locals, *expr->datum.list, expr->loc);
    } else {
        error("Parser error state.", expr->loc);
        return;
    }
}


optional<local_addr> compiler::find_local(local_table& locals,
                                          bool* is_upval,
                                          symbol_id name) {
    local_table* l = &locals;
    optional<u8*> res;
    u32 levels = 0;
    // keep track of how many enclosing functions we need to go into
    while (true) {
        res = l->vars.get(name);
        if (res.has_value()) {
            break;
        }


        // here we're about to ascend to an enclosing function, so we need an upvalue
        if (l->cur_func != nullptr) {
            levels += 1;
        }
        if (l->parent == nullptr) {
            break;
        }
        l = l->parent;
    }

    if (levels > 0 && res.has_value()) {
        *is_upval = true;
        return locals.add_upvalue(levels, **res);
    } else if (res.has_value()) {
        *is_upval = false;
        return **res;
    }

    return std::nullopt;
}

void compiler::compile_atom(local_table& locals,
                            const ast_atom& atom,
                            const source_loc& loc) {
    const_id id;
    value v;
    const symbol* s;
    switch (atom.type) {
    case at_number:
        id = get_bytecode().add_const(as_value(atom.datum.num));
        constant(id);
        ++locals.sp;
        break;
    case at_string:
        v = vm->get_alloc().const_string(*atom.datum.str);
        id = get_bytecode().add_const(v);
        constant(id);
        ++locals.sp;
        break;
    case at_symbol:
        // TODO: check for special symbols
        s = &(get_symtab()[atom.datum.sym]);
        if (s->name == "null") {
            write_byte(OP_NULL);
            ++locals.sp;
        } else if(s->name == "true") {
            write_byte(OP_TRUE);
            ++locals.sp;
        } else if (s->name == "false") {
            write_byte(OP_FALSE);
            ++locals.sp;
        } else {
            compile_var(locals, atom.datum.sym, loc);
        }
        break;
    }
}

void compiler::compile_var(local_table& locals,
                           symbol_id sym,
                           const source_loc& loc) {
    bool up;
    auto l = find_local(locals, &up, sym);
    if (l.has_value()) {
        write_byte(up ? OP_UPVALUE : OP_LOCAL);
        write_byte(*l);
    } else {
        auto id = get_bytecode().add_const(as_sym_value(sym));
        write_byte(OP_CONST);
        write_short(id);
        write_byte(OP_GLOBAL);
    }
    ++locals.sp;
}

void compiler::compile_dot_obj(local_table& locals,
                               const vector<ast_node*>& dot_expr,
                               u8 all_but,
                               const source_loc& loc) {
    if (dot_expr.size() < 3) {
        error("Too few arguments to dot.", loc);
    }
    if (!dot_expr[1]->is_symbol()) {
        error("Arguments to dot must be symbols.", dot_expr[1]->loc);
    }
    compile_var(locals,
                dot_expr[1]->get_symbol(get_symtab()).id,
                dot_expr[1]->loc);

    // apply all but the last keys
    auto end = dot_expr.size() - all_but;
    for (u32 i = 2; i < end; ++i) {
        if (!dot_expr[i]->is_symbol()) {
            error("Arguments to dot must be symbols.", dot_expr[i]->loc);
        }
        auto id = get_bytecode()
            .add_const(as_sym_value(dot_expr[i]
                                    ->get_symbol(get_symtab()).id));
        write_byte(OP_CONST);
        write_short(id);
        write_byte(OP_OBJ_GET);
    }
}

void compiler::compile_list(local_table& locals,
                            const vector<ast_node*>& list,
                            const source_loc& loc) {
    if (list.size() == 0) {
        error("Encountered empty list.", loc);
        return;
    }
    if (list[0]->kind == ak_atom
        && list[0]->datum.atom->type == at_symbol) {
        auto sym = list[0]->datum.atom->datum.sym;
        const string& name = (get_symtab())[sym].name;
        if (name == "and") {
            compile_and(locals, list, list[0]->loc);
        } else if (name == "cond") {
            compile_cond(locals, list, list[0]->loc);
        } else if (name == "def") {
            compile_def(locals, list, list[0]->loc);
        // } else if (name == "defmacro") {
        //     compile_defmacro(locals, list, list[0]->loc);
        } else if (name == "defn") {
            compile_defn(locals, list, list[0]->loc);
        } else if (name == "do") {
            compile_do(locals, list, list[0]->loc);
        } else if (name == "dot") {
            compile_dot(locals, list, list[0]->loc);
        // } else if (name == "dollar-fn") {
        //     compile_dollar_fn(locals, list, list[0]->loc);
        } else if (name == "if") {
            compile_if(locals, list, list[0]->loc);
        // } else if (name == "import") {
        //     compile_import(locals, list, list[0]->loc);
        } else if (name == "fn") {
            compile_fn(locals, list, list[0]->loc);
        } else if (name == "let") {
            compile_let(locals, list, list[0]->loc);
        } else if (name == "letfn") {
            compile_letfn(locals, list, list[0]->loc);
        } else if (name == "or") {
            compile_or(locals, list, list[0]->loc);
        // } else if (name == "quasiquote") {
        //     compile_quasiquote(locals, list, list[0]->loc);
        } else if (name == "quote") {
            compile_quote(locals, list, list[0]->loc);
        // } else if (name == "unquote") {
        //     compile_unquote(locals, list, list[0]->loc);
        // } else if (name == "unquote-splicing") {
        //     compile_unquote_splicing(locals, list, list[0]->loc);
        } else if (name == "set!") {
            compile_set(locals, list, list[0]->loc);
        } else if (name == "with") {
            compile_with(locals, list, list[0]->loc);
        } else {
            compile_call(locals, list);
        }
    } else {
        compile_call(locals, list);
    }
}

void compiler::compile_call(local_table& locals,
                            const vector<ast_node*>& list) {
    auto base_sp = locals.sp;


    // compile the operator
    compile_subexpr(locals, list[0]);

    // table for keywords
    write_byte(OP_TABLE);
    ++locals.sp;

    u32 num_args = 0;
    u32 i;
    vector<symbol_id> kw; // keyword symbol id's we've seen
    for (i = 1; i < list.size(); ++i) {
        // TODO: check for keyword
        if(list[i]->is_symbol()) {
            auto& s = list[i]->get_symbol(get_symtab());
            if(s.name.length() > 0 && s.name[0] == ':') {
                break;
            } else {
                compile_subexpr(locals, list[i]);
                ++num_args;
            }
        } else {
            compile_subexpr(locals, list[i]);
            ++num_args;
        }
    }

    for (; i < list.size(); i += 2) {
        auto& s = list[i]->get_symbol(get_symtab());
        if(s.name.length() == 0 || s.name[0] != ':') {
            error("Non-keyword argument following keyword argument.",
                  list[i]->loc);
        }
        // if we' here, we're should be looking at a keyword argument
        for (auto x : kw) {
            if (x == s.id) {
                error("Duplicated keyword argument in call.",
                      list[i]->loc);
            }
        }
        kw.push_back(s.id);
        // keyword
        if (list.size() <= i + 1) {
            error("Keyword is missing its argument.", list[i]->loc);
        }
        // FIXME: two colons in a row at the start of a symbol name
        // should be an error.

        // convert this symbol to a non-keyword one
        auto key = get_symtab().intern(s.name.substr(1));

        // add the argument to the keyword table
        write_byte(OP_LOCAL);
        write_byte(base_sp + 1);
        ++locals.sp;
        write_byte(OP_CONST);
        write_short(get_bytecode().add_const(as_sym_value(key->id)));
        ++locals.sp;
        compile_subexpr(locals, list[i+1]);
        write_byte(OP_OBJ_SET);
        locals.sp -= 3;
    }
                
    if (num_args > 255) {
        error("Function call with more than 255 arguments.", list.back()->loc);
    }
    write_byte(OP_CALL);
    write_byte((u8)num_args);
    locals.sp = base_sp + 1;
}

void compiler::compile_body(local_table& locals,
                            const vector<fn_parse::ast_node*>& list,
                            u32 body_start) {
    auto start = locals.sp;
    write_byte(OP_NULL);
    ++locals.sp;

    u32 i;
    for (i = body_start; i < list.size()-1; ++i) {
        compile_subexpr(locals, list[i]);
        write_byte(OP_POP);
        --locals.sp;
    }
    // the if is to account for an empty body
    if (body_start < list.size()) {
        compile_subexpr(locals, list[i]); 
        write_byte(OP_SET_LOCAL);
        write_byte(start);
        --locals.sp;
        write_byte(OP_CLOSE);
        write_byte(locals.sp - start - 1);
        locals.sp = start + 1;
    }
}

void compiler::compile_function(local_table& locals,
                                const param_list& params,
                                const vector<ast_node*>& body_vec,
                                u32 body_start) {
    // jump past the function body to the closure opcode
    write_byte(OP_JUMP);
    auto patch_addr = get_bytecode().get_size();
    write_short(0);

    // positional arguments
    vector<symbol_id> args;

    // find optional index
    local_addr opt_ind;
    for (opt_ind = 0; opt_ind < params.positional.size(); ++opt_ind) {
        if(params.positional[opt_ind].init_form != nullptr) {
            break;
        }
        args.push_back(params.positional[opt_ind].sym);
    }
    for (auto i = opt_ind; i < params.positional.size(); ++i) {
        args.push_back(params.positional[i].sym);
    }

    bool vl = params.var_list.has_value();
    bool vt = params.var_table.has_value();
    auto func_id = get_bytecode()
        .add_function(args, opt_ind, vl, vt, vm->current_namespace());

    // create the new local environment
    local_table fn_locals{&locals, get_bytecode().get_function(func_id)};
    fn_locals.sp = 0;
    for (auto x : params.positional) {
        fn_locals.vars.insert(x.sym, fn_locals.sp);
        ++fn_locals.sp;
    }
    if (vl) {
        fn_locals.vars.insert(*params.var_list, fn_locals.sp);
        ++fn_locals.sp;
    }
    if (vt) {
        fn_locals.vars.insert(*params.var_table, fn_locals.sp);
        ++fn_locals.sp;
    }

    // compile the function body
    compile_body(fn_locals, body_vec, body_start);
    write_byte(OP_RETURN);

    // jump here from beginning
    patch_short(patch_addr, get_bytecode().get_size()-patch_addr-2);

    // compile initial values
    for(auto i = opt_ind; i < params.positional.size(); ++i) {
        compile_subexpr(locals, params.positional[i].init_form);
    }

    // create the function object
    write_byte(OP_CLOSURE);
    write_short(func_id);
    ++locals.sp;
}

void compiler::compile_and(local_table& locals,
                           const vector<fn_parse::ast_node*>& list,
                           const source_loc& loc) {
    forward_list<bc_addr> patch_locs;
    for (u32 i = 1; i < list.size(); ++i) {
        compile_subexpr(locals, list[i]);
        // skip to end jump on false
        write_byte(OP_CJUMP);
        write_short(0);
        --locals.sp;
        patch_locs.push_front(cur_addr());
    }
    write_byte(OP_TRUE);
    write_byte(OP_JUMP);
    write_short(1);
    auto end_addr = cur_addr();
    for (auto u : patch_locs) {
        patch_short(u - 2, end_addr - u);
    }
    write_byte(OP_FALSE);
    ++locals.sp;
}

void compiler::compile_cond(local_table& locals,
                           const vector<fn_parse::ast_node*>& list,
                           const source_loc& loc) {
    if (list.size() % 2 != 1) {
        error("Odd number of arguments to cond", loc);
    }
    bc_addr patch_loc;
    forward_list<bc_addr> patch_to_end;
    for (u32 i = 1; i < list.size(); i += 2) {
        compile_subexpr(locals, list[i]);
        write_byte(OP_CJUMP);
        --locals.sp;
        write_short(0);
        patch_loc = cur_addr();

        compile_subexpr(locals, list[i+1]);
        write_byte(OP_JUMP);
        write_short(0);
        patch_to_end.push_front(cur_addr());
        --locals.sp;

        // this is for the CJUMP to the next branch
        patch_short(patch_loc - 2, (u16) (cur_addr() - patch_loc));
    }
    write_byte(OP_NULL);
    ++locals.sp;

    auto end = cur_addr();
    for (auto a : patch_to_end) {
        patch_short(a - 2, end - a);
    }
}

void compiler::compile_def(local_table& locals,
                           const vector<ast_node*>& list,
                           const source_loc& loc) {
    if (list.size() != 3) {
        error("Wrong number of arguments to def.", loc);
    }

    if (!list[1]->is_symbol()) {
        error("First argument to def must be a symbol.", loc);
    }

    auto sym = list[1]->datum.atom->datum.sym;
    // TODO: check for illegal symbol names
    constant(get_bytecode().add_const(as_sym_value(sym)));
    ++locals.sp;
    compile_subexpr(locals, list[2]);
    write_byte(OP_SET_GLOBAL);
}

void compiler::compile_defn(local_table& locals,
                            const vector<ast_node*>& list,
                            const source_loc& loc) {
    if (list.size() < 4) {
        error("Too few arguments to defn.", loc);
    }

    if (!list[1]->is_symbol()) {
        error("First argument to defn must be a symbol.", loc);
    }

    auto sym = list[1]->datum.atom->datum.sym;
    // TODO: check for illegal symbol names
    constant(get_bytecode().add_const(as_sym_value(sym)));
    ++locals.sp;

    auto params = parse_params(get_symtab(), *list[2]);
    compile_function(locals, params, list, 3);

    write_byte(OP_SET_GLOBAL);
    --locals.sp;
}

void compiler::compile_do(local_table& locals,
                          const vector<ast_node*>& list,
                          const source_loc& loc) {
    if (list.size() == 1) {
        write_byte(OP_NULL);
        ++locals.sp;
        return;
    }

    local_table new_locals{&locals};
    compile_body(new_locals, list, 1);
}

void compiler::compile_dot(local_table& locals,
                           const vector<ast_node*>& list,
                           const source_loc& loc) {
    compile_dot_obj(locals, list, 0, loc);
}


void compiler::compile_fn(local_table& locals,
                          const vector<ast_node*>& list,
                          const source_loc& loc) {
    if (list.size() <= 2) {
        error("Too few arguments to fn.", loc);
    }

    // parse parameters and set up the function stub
    auto params = parse_params(get_symtab(), *list[1]);
    compile_function(locals, params, list, 2);
}


void compiler::compile_if(local_table& locals,
                          const vector<ast_node*>& list,
                          const source_loc& loc) {
    if (list.size() != 4) {
        error("Wrong number of arguments to if.", loc);
    }
    compile_subexpr(locals, list[1]);

    write_byte(OP_CJUMP);
    write_short(0);
    --locals.sp;

    auto then_addr = cur_addr();
    compile_subexpr(locals, list[2]);
    write_byte(OP_JUMP);
    write_short(0);

    // put the stack pointer back since only one expression will be evaluated
    --locals.sp;
    auto else_addr = cur_addr();
    compile_subexpr(locals, list[3]);

    auto end_addr = cur_addr();
    patch_short(then_addr - 2, else_addr - then_addr);
    patch_short(else_addr - 2, end_addr - else_addr);
}

void compiler::compile_let(local_table& locals,
                           const vector<ast_node*>& list,
                           const source_loc& loc) {
    if (locals.parent == nullptr) {
        error("let cannot occur at the top level.", loc);
    }

    // check we have an even number of arguments
    if ((list.size() & 1) != 1) {
        error("Wrong number of arguments to let.", loc);
    }

    // collect symbol names first (in order to allow recursive definitions)
    vector<symbol_id> names;
    for (u32 i = 1; i < list.size(); i += 2) {
        if (!list[i]->is_symbol()) {
            error("Local variable name not a symbol.", list[i]->loc);
        }
        auto sym = list[i]->get_symbol(get_symtab()).id;
        if (locals.vars.get(sym).has_value()) {
            error("Local variable already exists.", list[i]->loc);
        }
        locals.vars.insert(sym, locals.sp);
        names.push_back(sym);
        write_byte(OP_NULL);
        ++locals.sp;
    }

    // bind symbols
    for (u32 i = 1; i < list.size(); i += 2) {
        auto sym = names[(i-1)/2];
        compile_subexpr(locals, list[i+1]);
        write_byte(OP_SET_LOCAL);
        write_byte(**locals.vars.get(sym));
        --locals.sp;
    }

    // return null
    write_byte(OP_NULL);
    ++locals.sp;
}

void compiler::compile_letfn(local_table& locals,
                           const vector<ast_node*>& list,
                           const source_loc& loc) {
    if (locals.parent == nullptr) {
        error("Let cannot occur at the top level.", loc);
    }


    // check we have an even number of arguments
    if (list.size() < 4) {
        error("Too few arguments to letfn.", loc);
    }

    if (!list[1]->is_symbol()) {
        error("Name in letfn must be a symbol.", list[1]->loc);
    }

    auto sym = list[1]->datum.atom->datum.sym;
    auto pos = locals.sp++;
    // initial value null (in case of recursive reads)
    write_byte(OP_NULL);
    locals.vars.insert(sym, pos);

    auto params = parse_params(get_symtab(), *list[2]);
    compile_function(locals, params, list, 3);

    write_byte(OP_SET_LOCAL);
    write_byte(pos);
    write_byte(OP_NULL);
}

void compiler::compile_or(local_table& locals,
                          const vector<fn_parse::ast_node*>& list,
                          const source_loc& loc) {
    forward_list<bc_addr> patch_locs;
    for (u32 i = 1; i < list.size(); ++i) {
        compile_subexpr(locals, list[i]);
        // skip the next jump on false
        write_byte(OP_CJUMP);
        write_short(3);
        --locals.sp;
        write_byte(OP_JUMP);
        write_short(0);
        patch_locs.push_front(cur_addr());
    }
    write_byte(OP_FALSE);
    write_byte(OP_JUMP);
    write_short(1);
    auto end_addr = cur_addr();
    for (auto u : patch_locs) {
        patch_short(u - 2, end_addr - u);
    }
    write_byte(OP_TRUE);
    ++locals.sp;
}

void compiler::compile_quote(local_table& locals,
                             const vector<fn_parse::ast_node*>& list,
                             const source_loc& loc) {
    if (list.size() != 2) {
        error("Wrong number of arguments to quote", loc);
    }
    auto v = vm->get_alloc().const_quote(list[1]);
    auto id = vm->get_bytecode().add_const(v);
    write_byte(OP_CONST);
    write_short(id);
    ++locals.sp;
}

void compiler::compile_set(local_table& locals,
                           const vector<fn_parse::ast_node*>& list,
                           const source_loc& loc)  {
    // check we have an even number of arguments
    if (list.size() != 3) {
        error("Wrong number of arguments to set!", loc);
    }
    if (list[1]->is_symbol()) {
        bool up;
        auto sym = list[1]->datum.atom->datum.sym;
        auto l = find_local(locals, &up, sym);
        if (l.has_value()) {
            compile_subexpr(locals, list[2]);
            write_byte(up ? OP_SET_UPVALUE : OP_SET_LOCAL);
            write_byte(*l);
        } else {
            auto id = get_bytecode().add_const(as_sym_value(sym));
            write_byte(OP_CONST);
            write_short(id);
            ++locals.sp;
            compile_subexpr(locals, list[2]);
            write_byte(OP_SET_GLOBAL);
            write_byte(OP_POP);
            --locals.sp;
        }
        write_byte(OP_NULL);
    } else if (list[1]->kind == ak_list) {
        // check if it's a dot
        auto op = (*list[1]->datum.list)[0];
        if (op->is_symbol()) {
           auto& sym = op->get_symbol(get_symtab());
           if (sym.name != "dot") {
               error("Illegal place in set! operation.", list[1]->loc);
           }

           // compile the dot expression up to the last key
           compile_dot_obj(locals, *list[1]->datum.list, 1, list[1]->loc);
           auto last = list[1]->datum.list->back();
           if (!last->is_symbol()) {
               error("Arguments to dot must be symbols.", last->loc);
           }

           // last key for the set operation
           auto id = get_bytecode()
               .add_const(as_sym_value(last->get_symbol(get_symtab()).id));
           write_byte(OP_CONST);
           write_short(id);
           ++locals.sp;

           // compute the value
           compile_subexpr(locals, list[2]);
           write_byte(OP_OBJ_SET);

           // return null
           write_byte(OP_NULL);
           locals.sp -= 2;
        } else {
            error("Illegal place in set! operation.", list[1]->loc);
        }
    } else {
        error("Illegal place in set! operation.", list[1]->loc);
    }
}

void compiler::compile_with(local_table& locals,
                            const vector<fn_parse::ast_node*>& list,
                            const source_loc& loc)  {
    if (list[1]->kind != ak_list) {
        error("Malformed with binding form.", list[1]->loc);
    }

    // a place for the result
    write_byte(OP_NULL);
    ++locals.sp;

    // create the local environment
    local_table new_locals{locals};
    auto& bindings = *list[1]->datum.list;
    vector<symbol_id> names;
    for (u32 i = 0; i < bindings.size(); i += 2) {
        if (bindings.size() <= i+1) {
            error("Odd number of arguments in with binding form.", loc);
        }
        if (!bindings[i]->is_symbol()) {
            error("with binding name not a symbol.", bindings[i]->loc);
        }
        auto sym = bindings[i]->get_symbol(get_symtab()).id;
        new_locals.vars.insert(sym, new_locals.sp);
        names.push_back(sym);
        write_byte(OP_NULL);
        ++new_locals.sp;
    }

    // variable values
    for (u32 i = 0; i < bindings.size(); i += 2) {
        compile_subexpr(new_locals, bindings[i+1]);
        write_byte(OP_SET_LOCAL);
        write_byte(**new_locals.vars.get(names[i/2]));
        --new_locals.sp;
    }

    // body
    compile_body(new_locals, list, 2);

    // put the result where it belongs
    write_byte(OP_SET_LOCAL);
    write_byte(locals.sp - 1);
    --new_locals.sp;
    write_byte(OP_CLOSE);
    write_byte(new_locals.sp - locals.sp);
}

void compiler::compile_expr() {
    local_table l;
    auto expr = parse_node(*sc, get_symtab());
    compile_subexpr(l, expr);
    delete expr;
    write_byte(OP_POP);
}

void compiler::compile_to_eof() {
    while (!sc->eof()) {
        compile_expr();
    }
}


}
