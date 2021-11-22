#include "compile.hpp"

#include <climits>

namespace fn {

using namespace fn_parse;

#define return_on_err if (err->has_error) return

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
        const source_loc& origin,
        compile_error* err) {
    if (offset < SHRT_MIN || offset > SHRT_MAX) {
        err->has_error = true;
        err->origin = origin;
        err->message = "jmp distance won't fit in 16 bits";
        return;
    }
    i16 jmp_dist = (i16)offset;
    patch_short(static_cast<u16>(jmp_dist), where);
}

void compiler::compile_symbol(symbol_id sym) {
    auto id = dest->add_constant(as_sym_value(sym));
    write_byte(OP_CONST);
    write_short(id);
}

void compiler::compile_llir(const llir_call_form* llir,
        lexical_env* lex,
        compile_error* err) {
    auto start_sp = lex->sp;
    // compile positional arguments in ascending order
    for (u32 i = 0; i < llir->num_pos_args; ++i) {
        compile_llir_generic(llir->pos_args[i], lex, err);
        return_on_err;
    }
    // TODO: compile keyword table
    write_byte(OP_TABLE);
    ++lex->sp;
    for (u32 i = 0; i < llir->num_kw_args; ++i) {
        // copy the table for the set command
        write_byte(OP_COPY);
        write_byte(0);
        ++lex->sp;
        auto& k = llir->kw_args[i];
        compile_symbol(k.nonkw_name);
        ++lex->sp;
        compile_llir_generic(k.value_form, lex, err);
        write_byte(OP_OBJ_SET);
        return_on_err;
        lex->sp -= 3;
    }

    // compile callee
    compile_llir_generic(llir->callee, lex, err);
    return_on_err;
    write_byte(OP_CALL);
    write_byte(llir->num_pos_args);
    lex->sp = 1+start_sp;
}

void compiler::compile_llir(const llir_const_form* llir,
        lexical_env* lex,
        compile_error* err) {
    write_byte(OP_CONST);
    write_short(llir->id);
    ++lex->sp;
}

void compiler::compile_llir(const llir_def_form* llir,
        lexical_env* lex,
        compile_error* err) {
    // TODO: check legal variable name
    write_byte(OP_CONST);
    write_short(dest->add_constant(as_sym_value(llir->name)));
    write_byte(OP_COPY);
    write_byte(0);
    lex->sp += 2;

    compile_llir_generic(llir->value, lex, err);
    return_on_err;
    write_byte(OP_SET_GLOBAL);
    lex->sp -= 2;
}

void compiler::compile_llir(const llir_defmacro_form* llir,
        lexical_env* lex,
        compile_error* err) {
    // TODO: check legal variable name
    write_byte(OP_CONST);
    write_short(dest->add_constant(as_sym_value(llir->name)));
    write_byte(OP_COPY);
    write_byte(0);
    lex->sp += 2;

    compile_llir_generic(llir->macro_fun, lex, err);
    return_on_err;
    write_byte(OP_SET_MACRO);
    lex->sp -= 2;
}

void compiler::compile_llir(const llir_dot_form* llir,
        lexical_env* lex,
        compile_error* err) {
    compile_llir_generic(llir->obj, lex, err);
    return_on_err;

    for (u32 i = 0; i < llir->num_keys; ++i) {
        compile_symbol(llir->keys[i]);
        write_byte(OP_OBJ_GET);
    }
}

void compiler::compile_llir(const llir_if_form* llir,
        lexical_env* lex,
        compile_error* err) {
    compile_llir_generic(llir->test_form, lex, err);
    return_on_err;

    i64 addr1 = dest->code_size;
    write_byte(OP_CJUMP);
    write_short(0);

    compile_llir_generic(llir->then_form, lex, err);
    return_on_err;

    i64 addr2 = dest->code_size;
    write_byte(OP_JUMP);
    write_short(0);
    --lex->sp;

    --lex->sp; // if we're running this one, we didn't run the other
    compile_llir_generic(llir->else_form, lex, err);
    return_on_err;

    i64 end_addr = dest->code_size;
    patch_jump(addr2 - addr1, addr1 + 1, llir->header.origin, err);
    return_on_err;
    patch_jump(end_addr - addr2 - 3, addr2 + 1, llir->header.origin, err);
    // minus three since jump is relative to the end of the 3 byte instruction
    return_on_err;
}

void compiler::compile_llir(const llir_fn_form* llir,
        lexical_env* lex,
        compile_error* err) {
    // write jump
    i64 start = dest->code_size;
    write_byte(OP_JUMP);
    write_short(0);

    // set up new lexical environment
    auto& params = llir->params;
    optional<symbol_id> var_list = std::nullopt;
    if (params.has_var_list_arg) {
        var_list = params.var_list_arg;
    }
    optional<symbol_id> var_table = std::nullopt;
    if (params.has_var_table_arg) {
        var_table = params.var_table_arg;
    }
    auto func_id = dest->add_function(params.num_pos_args,
            params.pos_args,
            params.req_args,
            var_list,
            var_table);
    auto stub = dest->get_function(func_id);
    auto lex2 = extend_lex_env(lex, stub);
    // compile function body with a new lexical environment
    for (u32 i = 0; i < params.num_pos_args; ++i) {
        lex2.vars.insert(params.pos_args[i], i);
    }
    lex2.sp = params.num_pos_args;
    // var list comes before var table, but there may be var table without var list
    if (params.has_var_list_arg) {
        lex2.vars.insert(params.var_list_arg, params.num_pos_args);
        ++lex2.sp;
        if (params.has_var_table_arg) {
            lex2.vars.insert(params.var_table_arg, params.num_pos_args+1);
            ++lex2.sp;
        }
    } else if (params.has_var_table_arg) {
        ++lex2.sp;
        lex2.vars.insert(params.var_table_arg, params.num_pos_args);
    }

    compile_llir_generic(llir->body, &lex2, err);
    return_on_err;
    write_byte(OP_RETURN);

    // jump over the function body
    i64 end_addr = dest->code_size;
    patch_jump(end_addr - start - 3, start + 1, llir->header.origin, err);
    return_on_err;


    // TODO compile init forms
    auto len = params.num_pos_args - params.req_args;
    for (auto i = 0; i < len; ++i) {
        compile_llir_generic(params.init_forms[i], lex, err);
        return_on_err;
    }

    // write closure command
    write_byte(OP_CLOSURE);
    write_short(func_id);
    ++lex->sp;
}

void compiler::compile_llir(const llir_set_form* llir,
        lexical_env* lex,
        compile_error* err) {
    if (llir->target->tag == llir_var) { // variable set
        auto var = (llir_var_form*)llir->target;
        bool is_upval;
        auto x = find_local(lex, &is_upval, var->name);
        // FIXME: set! should fail on globals
        if (!x.has_value()) { // global
            compile_symbol(var->name);
            ++lex->sp;
            compile_llir_generic(llir->value, lex, err);
            return_on_err;
            write_byte(OP_SET_GLOBAL);
            --lex->sp;
        } else {
            compile_llir_generic(llir->value, lex, err);
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
    } else if (llir->target->tag == llir_call) { // (set! (get ...) v)
        auto call_form = (llir_call_form*)llir->target;
        auto op = call_form->callee;
        if (op->tag != llir_var
                || call_form->num_kw_args != 0
                || call_form->num_pos_args == 0
                || ((llir_var_form*)op)->name != symtab->intern("get")) {
            err->has_error = true;
            err->origin = llir->target->origin;
            err->message = "Malformed 1st argument to set!.";
        }
        // compile the first argument
        compile_llir_generic(call_form->pos_args[0], lex, err);
        return_on_err;
        // iterate over the key forms, compiling as we go
        i32 i;
        for (i = 1; i+1 < call_form->num_pos_args; ++i) {
            compile_llir_generic(call_form->pos_args[i], lex, err);
            return_on_err;
            // all but last call will be OBJ_GET
            write_byte(OP_OBJ_GET);
            --lex->sp;
        }
        compile_llir_generic(call_form->pos_args[i], lex, err);
        return_on_err;
        compile_llir_generic(llir->value, lex, err);
        return_on_err;
    } else if (llir->target->tag == llir_dot) { // (set! (dot ...) v)
        // this is like the previous case, but easier since our keys are just
        // symbols
        auto dot_form = (llir_dot_form*)llir->target;
        compile_llir_generic(dot_form->obj, lex, err);
        return_on_err;
        // iterate over the key forms, compiling as we go
        i32 i;
        for (i = 0; i < dot_form->num_keys - 1; ++i) {
            compile_symbol(dot_form->keys[i]);
            // all but last call will be OBJ_GET
            write_byte(OP_OBJ_GET);
        }
        compile_symbol(dot_form->keys[i]);
        ++lex->sp;
        compile_llir_generic(llir->value, lex, err);
        return_on_err;
    } else {
        err->has_error = true;
        err->origin = llir->target->origin;
        err->message = "Malformed 1st argument to set!.";
    }
    write_byte(OP_OBJ_SET);
    write_byte(OP_NIL);
    lex->sp -= 2;
}

void compiler::compile_llir(const llir_var_form* llir,
        lexical_env* lex,
        compile_error* err) {
    auto str = symtab->symbol_name(llir->name);
    if (str == "nil") {
        write_byte(OP_NIL);
    } else if (str == "false") {
        write_byte(OP_FALSE);
    } else if (str == "true") {
        write_byte(OP_TRUE);
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

void compiler::compile_llir(const llir_with_form* llir,
        lexical_env* lex,
        compile_error* err) {
    write_byte(OP_NIL);
    auto ret_place = lex->sp++;
    auto lex2 = extend_lex_env(lex);
    for (u32 i = 0; i < llir->num_vars; ++i) {
        // TODO: check name legality
        lex2.vars.insert(llir->vars[i], lex2.sp++);
        write_byte(OP_NIL); // initialize vars to nil
    }

    for (u32 i = 0; i < llir->num_vars; ++i) {
        compile_llir_generic(llir->value_forms[i], &lex2, err);
        return_on_err;
        write_byte(OP_SET_LOCAL);
        write_byte(*lex2.vars.get(llir->vars[i]));
        --lex2.sp;
    }

    if (llir->body_length == 0) {
        write_byte(OP_NIL);
    } else {
        u32 i = 0;
        for(i = 0; i+1 < llir->body_length; ++i) {
            compile_llir_generic(llir->body[i], &lex2, err);
            return_on_err;
            write_byte(OP_POP);
            --lex2.sp;
        }
        compile_llir_generic(llir->body[i], &lex2, err);
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
        compile_error* err) {
    switch (llir->tag) {
    case llir_def:
        compile_llir((llir_def_form*)llir, lex, err);
        break;
    case llir_defmacro:
        compile_llir((llir_defmacro_form*)llir, lex, err);
        break;
    case llir_dot:
        compile_llir((llir_dot_form*)llir, lex, err);
        break;
    case llir_call:
        compile_llir((llir_call_form*)llir, lex, err);
        break;
    case llir_const:
        compile_llir((llir_const_form*)llir, lex, err);
        break;
    case llir_if:
        compile_llir((llir_if_form*)llir, lex, err);
        break;
    case llir_fn:
        compile_llir((llir_fn_form*)llir, lex, err);
        break;
    case llir_import:
        //compile_llir((llir_import_form*)llir, lex, err);
        break;
    case llir_set:
        compile_llir((llir_set_form*)llir, lex, err);
        break;
    case llir_var:
        compile_llir((llir_var_form*)llir, lex, err);
        break;
    case llir_with:
        compile_llir((llir_with_form*)llir, lex, err);
        break;
    }
}

void compiler::compile(const llir_form* llir, compile_error* err) {
    lexical_env lex;
    compile_llir_generic(llir, &lex, err);
    write_byte(OP_POP);
}

}
