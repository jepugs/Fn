#include "compile.hpp"

#include <climits>

namespace fn {

using namespace fn_parse;

#define return_on_err if (err->happened) return

lexical_env extend_lex_env(lexical_env* parent, function_stub* new_func) {
    u8 bp;
    u8 sp = 0;
    if (new_func != nullptr) {
        bp = parent->sp;
    } else {
        sp = parent->sp;
        bp = parent->bp;
    }

    return lexical_env{
        .parent=parent,
        .is_call_frame=(new_func != nullptr),
        .enclosing_func=new_func,
        .sp=sp,
        .bp=bp
    };
}

optional<local_address> compiler::find_local(lexical_env* lex,
        bool* is_upval,
        symbol_id name) {

    // check the current local environment
    auto res = lex->vars.get(name);
    if (res.has_value()) {
        // found locally
        *is_upval = false;
        return *res;
    }
    res = lex->upvals.get(name);
    if (res.has_value()) {
        *is_upval = true;
        return *res;
    }

    if (lex->parent == nullptr) {
        return std::nullopt;
    }

    // this recursive call does a lot of work for us
    auto x = find_local(lex->parent, is_upval, name);
    if (!x.has_value()) {
        return std::nullopt;
    }

    // as the call stack unwinds, we have to add appropriate upvalues to each
    // function_stub involved. We only need to do this on call frames.
    if (lex->is_call_frame) {
        auto l = lex->enclosing_func->add_upvalue(*x, !(*is_upval));

        // add to the upvalues table
        lex->upvals.insert(name, l);
        *is_upval = true;
        return l;
    }

    return x;
}

void compiler::write_byte(u8 byte) {
    dest->write_byte(byte);
}
void compiler::write_short(u16 u) {
    dest->write_short(u);
}
void compiler::patch_short(u16 u, code_address where) {
    dest->write_short(u, where);
}
void compiler::patch_jump(i64 offset,
        code_address where,
        const source_loc& origin) {
    if (offset < SHRT_MIN || offset > SHRT_MAX) {
        c_fault(origin, "JMP distance won't fit in 16 bits");
        return;
    }
    i16 jmp_dist = (i16)offset;
    patch_short(static_cast<u16>(jmp_dist), where);
}

void compiler::compile_symbol(symbol_id sym) {
    auto id = dest->add_constant(vbox_symbol(sym));
    write_byte(OP_CONST);
    write_short(id);
}

void compiler::compile_apply(const llir_apply* llir,
        lexical_env* lex,
        bool tail) {
    auto start_sp = lex->sp;

    // compile positional arguments in ascending order
    for (u32 i = 0; i < llir->num_args; ++i) {
        compile_llir_generic(llir->args[i], lex, false);
        return_on_err;
    }

    // compile callee
    compile_llir_generic(llir->callee, lex, false);
    return_on_err;

    write_byte(tail ? OP_TAPPLY : OP_APPLY);
    // the instruction doesn't count the list at the end
    write_byte(llir->num_args - 1);
    lex->sp = 1+start_sp;
}

void compiler::compile_call(const llir_call* llir,
        lexical_env* lex,
        bool tail) {
    auto start_sp = lex->sp;

    // argument index
    bool dot_call = false;
    // check if this is a method call. If so, compile and insert the argument
    // first.
    auto callee = llir->callee;
    if (callee->tag == lt_dot) {
        dot_call = true;
        // insert the dot object
        auto l2 = (llir_dot*)callee;
        compile_llir_generic(l2->obj, lex, false);
        return_on_err;
    } else if (callee->tag == lt_var) {
        auto v = (llir_var*)callee;
        // FIXME: hardcoded global ID is bad
        if (v->name == symtab->intern("get")
                || v->name == symtab->intern("#/fn/builtin:get")) {
            // compile a get operation directly
            compile_llir_generic(llir->args[0], lex, false);
            for (u32 i = 1; i < llir->num_args; ++i) {
                compile_llir_generic(llir->args[i], lex, false);
                write_byte(OP_OBJ_GET);
                --lex->sp;
            }
            return;
        }
    }

    // compile positional arguments in ascending order
    for (u32 i = 0; i < llir->num_args; ++i) {
        compile_llir_generic(llir->args[i], lex, false);
        return_on_err;
    }

    // compile callee
    if (dot_call) {
        // put the callee on top of the stack for method lookup
        write_byte(OP_COPY);
        write_byte(llir->num_args);
        compile_symbol(((llir_dot*)callee)->key);
        write_byte(OP_METHOD);
    } else {
        compile_llir_generic(callee, lex, false);
        return_on_err;
    }
    if (tail) {
        write_byte(OP_TCALL);
    } else {
        write_byte(OP_CALL);
    }
    write_byte(llir->num_args + dot_call);
    lex->sp = 1+start_sp;
}

void compiler::compile_const(const llir_const* llir,
        lexical_env* lex) {
    write_byte(OP_CONST);
    write_short(llir->id);
    ++lex->sp;
}

void compiler::compile_def(const llir_def* llir,
        lexical_env* lex) {
    // TODO: check legal variable name
    write_byte(OP_CONST);
    write_short(dest->add_constant(vbox_symbol(llir->name)));
    write_byte(OP_COPY);
    write_byte(0);
    lex->sp += 2;

    compile_llir_generic(llir->value, lex, false);
    return_on_err;
    write_byte(OP_SET_GLOBAL);
    lex->sp -= 2;
}

void compiler::compile_defmacro(const llir_defmacro* llir,
        lexical_env* lex) {
    // TODO: check legal variable name
    write_byte(OP_CONST);
    write_short(dest->add_constant(vbox_symbol(llir->name)));
    write_byte(OP_COPY);
    write_byte(0);
    lex->sp += 2;

    compile_llir_generic(llir->macro_fun, lex, false);
    return_on_err;
    write_byte(OP_SET_MACRO);
    lex->sp -= 2;
}

void compiler::compile_dot(const llir_dot* llir,
        lexical_env* lex) {
    // FIXME: expander should probably catch this
    c_fault(llir->header.origin,
            "dot expressions can only occur as operators for functions.");
    // compile_llir_generic(llir->obj, lex, false);
    //return_on_err;

    // for (u32 i = 0; i < llir->num_keys; ++i) {
    //     compile_symbol(llir->keys[i]);
    //     write_byte(OP_OBJ_GET);
    // }
}

void compiler::compile_if(const llir_if* llir,
        lexical_env* lex,
        bool tail) {
    compile_llir_generic(llir->test, lex, false);
    return_on_err;

    i64 addr1 = dest->code.size;
    write_byte(OP_CJUMP);
    write_short(0);
    --lex->sp;

    compile_llir_generic(llir->then, lex, tail);
    return_on_err;

    i64 addr2 = dest->code.size;
    write_byte(OP_JUMP);
    write_short(0);

    --lex->sp; // if we're running this one, we didn't run the other
    compile_llir_generic(llir->elce, lex, tail);
    return_on_err;

    i64 end_addr = dest->code.size;
    patch_jump(addr2 - addr1, addr1 + 1, llir->header.origin);
    return_on_err;
    patch_jump(end_addr - addr2 - 3, addr2 + 1, llir->header.origin);
    // minus three since jump is relative to the end of the 3 byte instruction
    return_on_err;
}

void compiler::compile_fn(const llir_fn* llir,
        lexical_env* lex) {
    // write jump
    i64 start = dest->code.size;
    write_byte(OP_JUMP);
    write_short(0);

    // set up new lexical environment
    auto& params = llir->params;
    optional<symbol_id> var_list = std::nullopt;
    if (params.has_var_list_arg) {
        var_list = params.var_list_arg;
    }
    optional<symbol_id> var_table = std::nullopt;
    auto func_id = dest->add_function(params.num_pos_args,
            params.pos_args,
            params.req_args,
            var_list,
            var_table,
            llir->name);
    auto stub = dest->get_function(func_id);
    auto lex2 = extend_lex_env(lex, stub);
    // compile function body with a new lexical environment
    lex2.sp = 0;
    while (lex2.sp < params.num_pos_args) {
        lex2.vars.insert(params.pos_args[lex2.sp], lex2.sp);
        ++lex2.sp;
    }
    // variadic parameter
    if (params.has_var_list_arg) {
        lex2.vars.insert(params.var_list_arg, lex2.sp++);
    }
    // indicator parameters
    for (u32 i = params.req_args; i < params.num_pos_args; ++i) {
        lex2.vars.insert(symtab->intern("?" + (*symtab)[params.pos_args[i]]),
                lex2.sp++);
    }

    compile_llir_generic(llir->body, &lex2, true);
    return_on_err;
    write_byte(OP_RETURN);

    // jump over the function body
    i64 end_addr = dest->code.size;
    patch_jump(end_addr - start - 3, start + 1, llir->header.origin);
    return_on_err;

    // compile init forms
    auto init_len = params.num_pos_args - params.req_args;
    for (auto i = 0; i < init_len; ++i) {
        compile_llir_generic(params.inits[i], lex, false);
        return_on_err;
    }

    // write closure command
    write_byte(OP_CLOSURE);
    write_short(func_id);
    lex->sp -= init_len;
    ++lex->sp;
}

void compiler::compile_import(const llir_import* llir,
        lexical_env* lex) {
    compile_symbol(llir->target);
    write_byte(OP_IMPORT);
    compile_symbol(llir->target);
    ++lex->sp;
}

void compiler::compile_set(const llir_set* llir,
        lexical_env* lex) {
    if (llir->target->tag == lt_var) { // variable set
        auto var = (llir_var*)llir->target;
        bool is_upval;
        auto x = find_local(lex, &is_upval, var->name);
        // FIXME: set! should fail on globals
        if (!x.has_value()) { // global
            c_fault(llir->header.origin, "Attempt to set! a global value.");
            return;
        } else {
            compile_llir_generic(llir->value, lex, false);
            return_on_err;
            if (is_upval) {
                write_byte(OP_SET_UPVALUE);
                write_byte(*x);
            } else {
                write_byte(OP_SET_LOCAL);
                write_byte(*x);
            }
            --lex->sp;
        }
        write_byte(OP_NIL);
        ++lex->sp;
    } else if (llir->target->tag == lt_call) { // (set! (get ...) v)
        auto call = (llir_call*)llir->target;
        auto op = call->callee;
        if (op->tag != lt_var
                || call->num_args == 0
                || ((llir_var*)op)->name != symtab->intern("get")) {
            c_fault(llir->target->origin, "Malformed 1st argument to set!.");
        }
        // compile the first argument
        compile_llir_generic(call->args[0], lex, false);
        return_on_err;
        // iterate over the key forms, compiling as we go
        i32 i;
        for (i = 1; i+1 < call->num_args; ++i) {
            compile_llir_generic(call->args[i], lex, false);
            return_on_err;
            // all but last call will be OBJ_GET
            write_byte(OP_OBJ_GET);
            --lex->sp;
        }
        compile_llir_generic(call->args[i], lex, false);
        return_on_err;
        compile_llir_generic(llir->value, lex, false);
        return_on_err;
        write_byte(OP_OBJ_SET);
        write_byte(OP_NIL);
        lex->sp -= 2;
    } else {
        c_fault(llir->target->origin, "Malformed 1st argument to set!.");
    }
}

void compiler::compile_var(const llir_var* llir,
        lexical_env* lex) {
    auto str = symtab->symbol_name(llir->name);
    if (str == "nil") {
        write_byte(OP_NIL);
    } else if (str == "false") {
        write_byte(OP_FALSE);
    } else if (str == "true") {
        write_byte(OP_TRUE);
    } else if (str[0] == '#' && str[1] == '/') {
        compile_symbol(llir->name);
        write_byte(OP_BY_GUID);
    } else {
        bool is_upval;
        auto x = find_local(lex, &is_upval, llir->name);
        if (!x.has_value()) { // global
            compile_symbol(llir->name);
            write_byte(OP_GLOBAL);
        } else if (is_upval) { // upvalue
            write_byte(OP_UPVALUE);
            write_byte(*x);
        } else { // stack local
            write_byte(OP_LOCAL);
            write_byte(*x);
        }
    }
    ++lex->sp;
}

void compiler::compile_with(const llir_with* llir,
        lexical_env* lex,
        bool tail) {
    write_byte(OP_NIL);
    auto ret_place = lex->sp++;
    auto lex2 = extend_lex_env(lex);
    for (u32 i = 0; i < llir->num_vars; ++i) {
        // TODO: check name legality
        lex2.vars.insert(llir->vars[i], lex2.sp++);
        write_byte(OP_NIL); // initialize vars to nil
    }

    for (u32 i = 0; i < llir->num_vars; ++i) {
        compile_llir_generic(llir->values[i], &lex2, false);
        return_on_err;
        write_byte(OP_SET_LOCAL);
        write_byte(*lex2.vars.get(llir->vars[i]));
        --lex2.sp;
    }

    if (llir->body_length == 0) {
        write_byte(OP_NIL);
        ++lex2.sp;
    } else {
        u32 i = 0;
        for(i = 0; i+1 < llir->body_length; ++i) {
            compile_llir_generic(llir->body[i], &lex2, false);
            return_on_err;
            write_byte(OP_POP);
            --lex2.sp;
        }
        compile_llir_generic(llir->body[i], &lex2, tail);
        return_on_err;
    }
    write_byte(OP_SET_LOCAL);
    write_byte(ret_place);
    --lex2.sp;

    write_byte(OP_CLOSE);
    write_byte(lex2.sp - ret_place - 1);
}

void compiler::compile_llir_generic(const llir_form* llir,
        lexical_env* lex,
        bool tail) {
    auto old_loc = dest->location_of(dest->code.size);
    dest->add_source_loc(llir->origin);
    switch (llir->tag) {
    case lt_apply:
        compile_apply((llir_apply*)llir, lex, tail);
        break;
    case lt_call:
        compile_call((llir_call*)llir, lex, tail);
        break;
    case lt_const:
        compile_const((llir_const*)llir, lex);
        break;
    case lt_def:
        compile_def((llir_def*)llir, lex);
        break;
    case lt_defmacro:
        compile_defmacro((llir_defmacro*)llir, lex);
        break;
    case lt_dot:
        compile_dot((llir_dot*)llir, lex);
        break;
    case lt_if:
        compile_if((llir_if*)llir, lex, tail);
        break;
    case lt_fn:
        compile_fn((llir_fn*)llir, lex);
        break;
    case lt_import:
        compile_import((llir_import*)llir, lex);
        break;
    case lt_set:
        compile_set((llir_set*)llir, lex);
        break;
    case lt_var:
        compile_var((llir_var*)llir, lex);
        break;
    case lt_with:
        compile_with((llir_with*)llir, lex, tail);
        break;
    }
    dest->add_source_loc(old_loc);
}

void compiler::c_fault(const source_loc& origin, const string& message) {
    set_fault(err, origin, "compile", message);
}

void compiler::compile(const llir_form* llir, fault* err) {
    lexical_env lex;
    this->err = err;
    compile_llir_generic(llir, &lex);
    write_byte(OP_POP);
}

}
