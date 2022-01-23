#include "vm.hpp"

#include "config.h"

#include "allocator.hpp"
#include "bytes.hpp"
#include "namespace.hpp"
#include "ffi/fn_handle.hpp"
#include "values.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>


namespace fn {

bool push_from_guid(istate* S, symbol_id guid) {
    auto x = S->by_guid.get(guid);
    if (x.has_value()) {
        push(S, *x);
        return true;
    }
    return false;
}

bool push_global(istate* S, symbol_id name) {
    auto x = S->ns->get(name);
    if (x.has_value()) {
        push(S, *x);
        return true;
    }
    return false;
}

void mutate_global(istate* S, symbol_id name, value v) {
    auto ns_str = (*S->symtab)[S->ns_id];
    auto var_str = (*S->symtab)[name];
    auto guid_str = "#/" + ns_str + ":" + var_str;

    S->ns->set(name, v);
    S->by_guid.insert(intern(S, guid_str), v);
}

static u16 read_short(dyn_array<u8>& code, u32 ip) {
    return *((u16*)&code[ip]);
}


static void close_upvals(istate* S, u32 min_addr) {
    u32 i = S->open_upvals.size;
    while (i > 0) {
        auto u = S->open_upvals[i-1];
        if (u->datum.pos < min_addr) {
            break;
        }
        auto val = S->stack[u->datum.pos];
        u->datum.val = val;
        u->closed = true;
        --i;
    }
    S->open_upvals.resize(i);
}


// function creation requires initvals to be present on the stack. After popping
// initvals, the new function is pushed to the top of the stack. Upvalues are
// opened or copied from the enclosing function as needed.
static void create_fun(istate* S, fn_function* enclosing, constant_id fid) {
    auto stub = enclosing->stub->sub_funs[fid];
    push(S, V_NIL);
    // this sets up upvalues, and initializes initvals to nil
    alloc_fun(S, &S->stack[S->sp - 1], enclosing, stub);
    auto fun = vfunction(S->stack[S->sp - 1]);
    // set up initvals
    for (u32 i = 0; i < stub->num_opt; ++i) {
        fun->init_vals[i] = S->stack[S->sp - 1 - stub->num_opt + i];
    }
    // move the function to the appropriate place on the stack
    S->stack[S->sp - 1 - stub->num_opt] = peek(S);
    S->sp = S->sp - stub->num_opt;
}

void execute_fun(istate* S, fn_function* fun) {
    auto stub = fun->stub;
    auto& code = fun->stub->code;

    // main interpreter loop
    while (true) {
        switch (code[S->pc++]) {
        case OP_NOP:
            break;
        case OP_POP:
            --S->sp;
            break;
        case OP_LOCAL:
            push(S, get(S, code[S->pc++]));
            break;
        case OP_SET_LOCAL:
            set(S, code[S->pc++], peek(S));
            --S->sp;
            break;
        case OP_COPY:
            push(S, peek(S, code[S->pc++]));
            break;
        case OP_UPVALUE: {
            auto u = fun->upvals[code[S->pc++]];
            if (u->closed) {
                push(S, u->datum.val);
            } else {
                push(S, S->stack[u->datum.pos]);
            }
        }
            break;
        case OP_SET_UPVALUE: {
            auto u = fun->upvals[code[S->pc++]];
            if (u->closed) {
                u->datum.val = peek(S);
            } else {
                S->stack[u->datum.pos] = peek(S);
            }
            --S->sp;
        }
            break;
        case OP_CLOSURE: {
            auto fid = read_short(code, S->pc);
            S->pc += 2;
            create_fun(S, fun, fid);
        }
            break;
        case OP_CLOSE: {
            auto num = code[S->pc++];
            // computes highest stack address to close
            auto new_sp = S->sp - num;
            close_upvals(S, new_sp);
            S->sp = new_sp;
        }
            break;
        case OP_GLOBAL: {
            auto sym = vsymbol(peek(S));
            // this is ok since symbols aren't GC'd
            --S->sp;
            if (!push_global(S, sym)) {
                ierror(S, "Failed to find global variable.");
                return;
            }
        }
            break;
        case OP_SET_GLOBAL:
            mutate_global(S, vsymbol(peek(S, 1)), peek(S));
            // leave the ID in place by only popping once
            --S->sp;
            break;
        case OP_OBJ_GET: {
            if (!vis_table(peek(S, 1))) {
                ierror(S, "obj-get target is not a table.");
                return;
            }
            auto x = vtable(peek(S, 1))->contents.get(peek(S));
            S->sp -= 2;
            if (x.has_value()) {
                push(S, *x);
            } else {
                push(S, V_NIL);
            }
        }
            break;
        case OP_OBJ_SET: {
            if (!vis_table(peek(S, 2))) {
                ierror(S, "obj-set target is not a table.");
                return;
            }
            vtable(peek(S, 2))->contents.insert(peek(S, 1), peek(S));
            S->stack[S->sp - 3] = peek(S);
            S->sp -= 2;
        }
            break;

        case OP_CONST:
            push(S, stub->const_arr[read_short(code, S->pc)]);
            S->pc += 2;
            break;
        case OP_NIL:
            push(S, V_NIL);
            break;
        case OP_FALSE:
            push(S, V_FALSE);
            break;
        case OP_TRUE:
            push(S, V_TRUE);
            break;

        case OP_JUMP: {
            auto u = read_short(code, S->pc);
            S->pc += 2 + *((i16*)&u);
        }
            break;
        case OP_CJUMP:
            if (!vtruth(peek(S))) {
                auto u = read_short(code, S->pc);
                S->pc += 2 + *((i16*)&u);
            } else {
                S->pc += 2;
            }
            --S->sp;
            break;
        case OP_CALL:
            call(S, code[S->pc++]);
            if (S->err_happened) {
                return;
            }
            break;

        case OP_RETURN:
            // close upvalues and exit the loop. The call() function will handle
            // moving the return value.
            close_upvals(S, S->bp);
            return;
            break;
        }
    }
}

}
