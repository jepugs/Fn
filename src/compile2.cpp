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
    const symbol* s;
    switch (atom.type) {
    case at_number:
        id = get_bytecode().num_const(atom.datum.num);
        constant(id);
        ++locals.sp;
        break;
    case at_string:
        id = get_bytecode().str_const(*atom.datum.str);
        constant(id);
        ++locals.sp;
        break;
    case at_symbol:
        // TODO: check for special symbols
        s = &(get_symtab()[atom.datum.sym]);
        if (s->name == "null") {
            write_byte(OP_NULL);
        } else if(s->name == "true") {
            write_byte(OP_TRUE);
        } else if (s->name == "false") {
            write_byte(OP_FALSE);
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
        auto id = get_bytecode().sym_const(sym);
        write_byte(OP_CONST);
        write_short(id);
        write_byte(OP_GLOBAL);
    }
    ++locals.sp;
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
        // } else if (name == "cond") {
        //     compile_cond(locals, list, list[0]->loc);
        } else if (name == "def") {
            compile_def(locals, list, list[0]->loc);
        // } else if (name == "defmacro") {
        //     compile_defmacro(locals, list, list[0]->loc);
        // } else if (name == "defn") {
        //     compile_defn(locals, list, list[0]->loc);
        } else if (name == "do") {
            compile_do(locals, list, list[0]->loc);
        // } else if (name == "dot") {
        //     compile_dot(locals, list, list[0]->loc);
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
        // } else if (name == "letfn") {
        //     compile_letfn(locals, list, list[0]->loc);
        } else if (name == "or") {
            compile_or(locals, list, list[0]->loc);
        // } else if (name == "quasiquote") {
        //     compile_quasiquote(locals, list, list[0]->loc);
        // } else if (name == "quote") {
        //     compile_quote(locals, list, list[0]->loc);
        // } else if (name == "unquote") {
        //     compile_unquote(locals, list, list[0]->loc);
        // } else if (name == "unquote-splicing") {
        //     compile_unquote_splicing(locals, list, list[0]->loc);
        // } else if (name == "set!") {
        //     compile_set(locals, list, list[0]->loc);
        // } else if (name == "with") {
        //     compile_with(locals, list, list[0]->loc);
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
                write_short(get_bytecode().sym_const(key->id));
                ++locals.sp;
                compile_subexpr(locals, list[i+1]);
                write_byte(OP_OBJ_SET);
                locals.sp -= 3;

                // increment i an additional time
                ++i;
            } else {
                compile_subexpr(locals, list[i]);
                ++num_args;
            }
        } else {
            compile_subexpr(locals, list[i]);
            ++num_args;
        }
    }
    if (num_args > 255) {
        error("Function call with more than 255 arguments.", list.back()->loc);
    }
    write_byte(OP_CALL);
    write_byte((u8)num_args);
    locals.sp = base_sp + 1;
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
    constant(get_bytecode().sym_const(sym));
    ++locals.sp;
    compile_subexpr(locals, list[2]);
    write_byte(OP_SET_GLOBAL);
}

void compiler::compile_do(local_table& locals,
                          const vector<ast_node*>& list,
                          const source_loc& loc) {
    if (list.size() == 1) {
        write_byte(OP_NULL);
        ++locals.sp;
        return;
    }

    u32 i;
    for (i = 1; i < list.size()-1; ++i) {
        compile_subexpr(locals, list[i]);
        write_byte(OP_POP);
        --locals.sp;
    }
    compile_subexpr(locals, list[i]);
}

void compiler::compile_fn(local_table& locals,
                          const vector<ast_node*>& list,
                          const source_loc& loc) {
    if (list.size() <= 2) {
        error("Too few arguments to fn.", loc);
    }

    // jump past the function body to the closure opcode
    write_byte(OP_JUMP);
    auto patch_addr = get_bytecode().get_size();
    write_short(0);

    // parse parameters and set up the function stub
    auto params = parse_params(get_symtab(), *list[1]);

    vector<symbol_id> vars;
    for (auto x : params.positional) {
        vars.push_back(x.sym);
    }

    bool vl = params.var_list.has_value();
    bool vt = params.var_table.has_value();
    auto func_id = get_bytecode()
        .add_function(vars, vl, vt, vm->current_namespace());

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
    u32 i;
    for (i = 2; i < list.size()-1; ++i) {
        compile_subexpr(fn_locals, list[i]);
        write_byte(OP_POP);
    }
    compile_subexpr(fn_locals, list[i]);
    write_byte(OP_RETURN);

    // create the function object
    patch_short(patch_addr, get_bytecode().get_size() - patch_addr - 2);
    write_byte(OP_CLOSURE);
    write_short(func_id);
    ++locals.sp;
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
    // TODO: fix something about this
    // if (locals.parent == nullptr) {
    //     error("Let cannot occur at the top level.", loc);
    // }


    // check we have an even number of arguments
    if ((list.size() & 1) != 1) {
        error("Wrong number of arguments to let.", loc);
    }

    for (u32 i = 1; i < list.size(); i += 2) {
        if (!list[i]->is_symbol()) {
            error("Names in let must be symbols.", list[i]->loc);
        }

        auto sym = list[i]->datum.atom->datum.sym;
        auto loc = locals.sp++;
        // initial value null (in case of recursive reads)
        write_byte(OP_NULL);
        locals.vars.insert(sym, loc);
        compile_subexpr(locals, list[i+1]);
        write_byte(OP_SET_LOCAL);
        write_byte(loc);
        write_byte(OP_NULL);
    }
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
