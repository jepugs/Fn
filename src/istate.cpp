#include "allocator.hpp"
#include "istate.hpp"
#include "parse.hpp"
#include "vm.hpp"
#include <cstring>

namespace fn {

istate* init_istate() {
    auto res = new istate;
    res->alloc = new allocator{res};
    // TODO: allocate this through the allocator instead
    res->symtab = new symbol_table;
    res->ns_id = intern(res, "fn/user");
    res->pc = 0;
    res->bp = 0;
    res->sp = 0;
    res->fun = nullptr;
    res->err_happened = false;
    res->err_msg = nullptr;
    // set up namespace
    res->ns = new fn_namespace{res->ns_id};
    res->globals.insert(res->ns_id, res->ns);
    return res;
}

void free_istate(istate* S) {
    for (auto e : S->globals) {
        delete e->val;
    }
    delete S->alloc;
    delete S->symtab;
    if (S->err_happened) {
        free(S->err_msg);
    }
    delete S;
}

void ierror(istate* S, const string& message) {
    auto len = message.length();
    if (S->err_msg != nullptr) {
        free(S->err_msg);
    }
    S->err_msg = (char*)malloc(len+1);
    memcpy(S->err_msg, message.c_str(), len+1);
    S->err_happened = true;
}

void push(istate* S, value v) {
    S->stack[S->sp++] = v;
}

void pop(istate* S) {
    --S->sp;
}

void pop(istate* S, u32 n) {
    S->sp -= n;
}

value peek(istate* S) {
    return S->stack[S->sp - 1];
}

value peek(istate* S, u32 offset) {
    return S->stack[S->sp - offset - 1];
}

value get(istate* S, u32 index) {
    return S->stack[S->bp + index];
}

void set(istate* S, u32 index, value v) {
    S->stack[S->bp + index] = v;
}

symbol_id intern(istate* S, const string& str) {
    return S->symtab->intern(str);
}

symbol_id gensym(istate* S) {
    return S->symtab->gensym();
}

void push_number(istate* S, f64 num) {
    push(S, vbox_number(num));
}
void push_string(istate* S, u32 size) {
    push_nil(S);
    alloc_string(S, &S->stack[S->sp - 1], size);
}
void push_string(istate* S, const string& str)  {
    push_nil(S);
    alloc_string(S, &S->stack[S->sp - 1], str);
}
void push_sym(istate* S, symbol_id sym) {
    push(S, vbox_symbol(sym));
}
void push_nil(istate* S) {
    push(S, V_NIL);
}
void push_true(istate* S) {
    push(S, V_TRUE);
}
void push_false(istate* S) {
    push(S, V_FALSE);
}

void push_cons(istate* S, value hd, value tl) {
    push_nil(S);
    alloc_cons(S, &S->stack[S->sp - 1], hd, tl);
}

void push_table(istate* S) {
    push_nil(S);
    alloc_table(S, &S->stack[S->sp - 1]);
}

void pop_to_list(istate* S, u32 n) {
    push(S, V_EMPTY);
    for (u32 i = 0; i < n; ++i) {
        alloc_cons(S, &S->stack[S->sp - 2], S->stack[S->sp - 2],
                S->stack[S->sp - 1]);
        pop(S);
    }
}

void push_empty_fun(istate* S) {
    push_nil(S);
    alloc_empty_fun(S, &S->stack[S->sp - 1], S->ns_id, S->ns);
}

void push_foreign_fun(istate* S,
        void (*foreign)(istate*),
        const string& params) {
    fault err;
    auto forms = fn_parse::parse_string(params, S->symtab, &err);
    if (err.happened) {
        for (auto f : forms) {
            free_ast_form(f);
        }
        ierror(S, err.message);
        return;
    }
    auto& p = forms[0];
    if (p->kind != fn_parse::ak_list) {

        ierror(S, "Malformed parameter list for foreign function.");
        return;
    }
    u8 num_args = p->list_length;
    bool vari = false;
    // check for var arg
    if (num_args >= 2) {
        auto x = p->datum.list[num_args - 2];
        if (x->kind == fn_parse::ak_symbol_atom
                && x->datum.sym == intern(S, "&")) {
            vari = true;
            num_args -= 2;
        }
    }
    for (auto f : forms) {
        free_ast_form(f);
    }
    push_nil(S);
    alloc_foreign_fun(S, &S->stack[S->sp - 1], foreign, num_args, vari,
            S->ns_id, S->ns);
}

void print_top(istate* S) {
    std::cout << v_to_string(peek(S), S->symtab, true) << '\n';
}

}
