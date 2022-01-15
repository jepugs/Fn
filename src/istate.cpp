#include "istate.hpp"
#include <cstring>

istate* init_istate() {
    auto res = new istate;
    res->alloc = new allocator{res};
    // TODO: allocate this through the allocator instead
    res->symtab = new symbol_table;
    res->ns_id = intern(res, "fn/user");
    res->pc = 0;
    res->stack_base = &res->stack[0];
    res->stack_top = &res->stack[0];
    res->uv_head = nullptr;
    res->err_happened = false;
    res->err_msg = nullptr;
    return res;
}

void free_istate(istate* S) {
    delete S->alloc;
    delete S->symbol_table;
    delete S;
}

void ierror(istate* S, const char* message) {
    auto len = strlen(message);
    S->err_msg = (char*)malloc(len+1);
    memcpy(S->err_msg, message, len+1);
    S->err_happened = true;
}

void ierror(istate* S, const string& message) {
    auto len = message.length();
    S->err_msg = (char*)malloc(len+1);
    memcpy(S->err_msg, message.c_str(), len+1);
    S->err_happened = true;
}

void push(istate* S, value v) {
    *(S->stack_top++) = v;
}

void pop(istate* S) {
    --S->stack_top;
}

void pop(istate* S, u32 n) {
    for (u32 i = 0; i < n; ++i) {
        --S->stack_top;
    }
}

value peek(istate* S) {
    return *(S->stack_top - offset - i);
}

value peek(istate* S, u32 offset) {
    return *(S->stack_top - offset - 1);
}

value get(istate* S, u32 index) {
    return S->stack_base[index];
}

value set(istate* S, u32 index, value v) {
    S->stack_base[index] = v;
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
    push_nil();
    alloc_string(S->alloc, S->stack_top - 1, size);
}
void push_string(istate* S, const string& str)  {
    push_nil();
    alloc_string(S->alloc, S->stack_top - 1, str);
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

void pop_to_list(istate* S, u32 n) {
    push(S, V_EMPTY);
    for (u32 i = 0; i < n; ++i) {
        alloc_cons(S->alloc, S->stack_top - 1, *(S->stack_top-2),
                *(S->stack_top-1));
    }
}

static bool arrange_call_stack(istate* S, fn_function* callee, u32 n) {
    auto stub = callee->stub;
    u32 min_args = stub->num_params - stub->num_opt;
    if (n < min_args) {
        ierror(S, "Too few arguments in function call.");
        return false;
    } else if (!stub->vari && n > stub->num_params) {
        ierror(S, "Too many arguments in function call.");
        return false;
    }

    u32 i;
    for (i = n; i < stub->num_params; ++i) {
        push(S, callee->init_vals[i]);
    }
    // handle variadic parameter
    if (stub->vari) {
        pop_to_list(S, n - stub->num_params);
    }
    // push indicator args
    u32 m = stub->num_params < n ? stub->num_params : n;
    for (i = min_args; i < m; ++i) {
        push(S, V_TRUE);
    }
    for (i = n; i < stub->num_params; ++i) {
        push(S, V_FALSE);
    }
}

void call(istate* S, u32 n) {
    // update the call frame
    auto save_pc = S->pc;
    auto save_base = S->stack_base;
    auto save_ns_id = S->ns_id;
    auto save_ns = S->ns;
    S->pc = 0;
    S->stack_base = S->stack_top - n;

    auto callee = peek(S, n);
    if (!vis_function(callee)) {
        ierror(S, "Attempt to call non-function value.");
        return;
    }
    S->ns_id = ns_id = vfunction(callee)->stub->ns_id;
    S->ns = ns = vfunction(callee)->stub->ns;
    arrange_call_stack(S, vfunction(callee), n);

    execute_fun(S, vfunction(callee));

    S->pc = save_pc;
    S->stack_base = save_base;
    S->ns_id = save_ns_id;
    S->ns = save_ns;
}
