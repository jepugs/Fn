#include "compile.hpp"

namespace fn {

using namespace fn_parse;

#define return_on_err if (err->has_error) return

lexical_env extend_lex_env(lexical_env* parent, function_stub* new_func) {
    u8 bp;
    u8 sp;
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
void compiler::patch_short(code_address where, u16 u) {
    dest->write_short(u, where);
}

void compiler::compile_llir(const llir_call_form* llir,
        lexical_env* lex,
        compile_error* err) {
    // compile positional arguments in ascending order
    for (u32 i = 0; i < llir->num_pos_args; ++i) {
        compile_llir_generic(llir->pos_args[i], lex, err);
        return_on_err;
    }
    // TODO: compile keyword table
    write_byte(OP_TABLE);
    ++lex->sp;

    // compile callee
    compile_llir_generic(llir->callee, lex, err);
    write_byte(OP_CALL);
    write_byte(llir->num_pos_args);
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

void compiler::compile_llir(const llir_if_form* llir,
        lexical_env* lex,
        compile_error* err) {
    compile_llir_generic(llir->test_form, lex, err);
    return_on_err;

    auto addr1 = dest->code_size;
    write_byte(OP_CJUMP);
    write_short(0);

    compile_llir_generic(llir->then_form, lex, err);
    return_on_err;

    auto addr2 = dest->code_size;
    write_byte(OP_JUMP);
    write_short(0);

    compile_llir_generic(llir->else_form, lex, err);
    return_on_err;

    auto end_addr = dest->code_size;
    patch_short(addr1 + 1, addr2 - addr1);
    patch_short(addr2 + 1, end_addr - addr2 - 3);
    // minus three since jump is relative to the end of the 3 byte instruction
}

void compiler::compile_llir(const llir_fn_form* llir,
        lexical_env* lex,
        compile_error* err) {
    err->origin = llir->header.origin;
    err->message = "compiling fn unsupported";
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
            write_byte(OP_CONST);
            write_short(dest->add_constant(as_sym_value(llir->name)));
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

void compiler::compile_llir_generic(const llir_form* llir,
        lexical_env* lex,
        compile_error* err) {
    switch (llir->tag) {
    case llir_def:
        compile_llir((llir_def_form*)llir, lex, err);
        break;
    case llir_defmacro:
        //compile_llir((llir_defmacro_form*)llir, lex, err);
        break;
    case llir_dot:
        //compile_llir((llir_dot_form*)llir, lex, err);
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
    case llir_import:
    case llir_set:
    case llir_var:
        compile_llir((llir_var_form*)llir, lex, err);
        break;
    case llir_with:
        break;
    }
}

void compiler::compile(const llir_form* llir, compile_error* err) {
    lexical_env lex;
    compile_llir_generic(llir, &lex, err);
    write_byte(OP_POP);
}

}
