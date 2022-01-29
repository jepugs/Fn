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

// macros to inline common operations. Note that the ones with an S argument
// must have the istate variable passed in directly or it could be computed
// multiple times.
#define cur_fun() (vfunction(S->stack[S->bp-1]))
#define code_byte(S, where) (S->code[where])
#define code_short(S, where) (*((u16*)&S->code[where]))
#define push(S, v) S->stack[S->sp] = v;++S->sp;
#define peek(S, i) (S->stack[S->sp-((i))-1])

static inline void close_upvals(istate* S, u32 min_addr) {
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
static inline void create_fun(istate* S, fn_function* enclosing, constant_id fid) {
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
    S->stack[S->sp - 1 - stub->num_opt] = S->stack[S->sp - 1];
    S->sp = S->sp - stub->num_opt;
}

// on success returns true and sets place on stack to the method. On failure
// returns false.
static inline bool get_method(istate* S, fn_table* tab, value key, u32 place) {
    auto m = tab->metatable;
    if (!vis_table(m)) {
        return false;
    }
    auto x = vtable(m)->contents.get(key);
    if (!x.has_value()) {
        return false;
    }
    S->stack[place] = *x;
    return true;
}

static inline bool arrange_call_stack(istate* S, fn_function* callee, u32 n) {
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
    return true;
}

static inline void foreign_call(istate* S, fn_function* fun, u32 n) {
    auto save_bp = S->bp;
    S->bp = S->sp - n;
    arrange_call_stack(S, fun, n);
    fun->stub->foreign(S);
    S->stack[S->bp-1] = peek(S, 0);
    S->sp = S->bp;  // with the return value, this is the new stack pointer
    S->bp = save_bp;
}

void call(istate* S, u32 n) {
    // update the call frame
    auto callee = peek(S, n);
    if (!vis_function(callee)) {
        ierror(S, "Attempt to call non-function value.");
        return;
    }
    auto fun = vfunction(callee);

    // FIXME: ensure minimum stack space available
    if (fun->stub->foreign) {
        foreign_call(S, fun, n);
    } else {
        auto save_bp = S->bp;
        auto save_code = S->code;
        S->bp = S->sp - n;
        arrange_call_stack(S, fun, n);
        S->code = fun->stub->code.data;
        execute_fun(S);
        if (S->err_happened) {
            std::ostringstream os;
            auto c = instr_loc(fun->stub, S->pc - 1);
            os << "At (" << c->loc.line << "," << c->loc.col << ") in "
               << c->loc.filename << ":  \n" << S->err_msg;
            ierror(S, os.str());
            return;
        }
        // return value
        S->stack[S->bp-1] = peek(S, 0);
        S->sp = S->bp;  // with the return value, this is the new stack pointer
        S->bp = save_bp;
        S->code = save_code;
    }
}

static inline bool tail_call(istate* S, u8 n, u32* pc) {
    auto callee = peek(S, n);
    if (!vis_function(callee)) {
        ierror(S, "Attempt to call non-function value.");
        return false;
    }
    auto fun = vfunction(callee);
    if (fun->stub->foreign) {
        foreign_call(S, fun, n);
        return true;
    }
    close_upvals(S, S->bp);
    // move the new call information to the base pointer
    S->stack[S->bp - 1] = callee;
    for (u32 i = 0; i < n; ++i) {
        S->stack[S->bp + i] = S->stack[S->sp - n + i];
    }
    S->sp = S->bp + n;
    if (!arrange_call_stack(S, vfunction(callee), n)) {
        return false;
    }
    S->code = fun->stub->code.data;
    *pc = 0;
    return true;
}

void execute_fun(istate* S) {
    u32 pc = 0;
    // main interpreter loop
    while (true) {
        switch (code_byte(S, pc++)) {
        case OP_NOP:
            break;
        case OP_POP:
            --S->sp;
            break;
        case OP_LOCAL:
            push(S, get(S, code_byte(S, pc++)));
            break;
        case OP_SET_LOCAL:
            S->stack[S->bp+code_byte(S, pc++)] = peek(S, 0);
            --S->sp;
            break;
        case OP_COPY:
            push(S, S->stack[S->sp - code_byte(S, pc++) - 1]);
            break;
        case OP_UPVALUE: {
            auto u = cur_fun()->upvals[code_byte(S, pc++)];
            if (u->closed) {
                push(S, u->datum.val);
            } else {
                push(S, S->stack[u->datum.pos]);
            }
        }
            break;
        case OP_SET_UPVALUE: {
            auto u = cur_fun()->upvals[code_byte(S, pc++)];
            if (u->closed) {
                u->datum.val = peek(S, 0);
            } else {
                S->stack[u->datum.pos] = peek(S, 0);
            }
            --S->sp;
        }
            break;
        case OP_CLOSURE: {
            auto fid = code_short(S, pc);
            pc += 2;
            create_fun(S, cur_fun(), fid);
        }
            break;
        case OP_CLOSE: {
            auto num = code_byte(S, pc++);
            // computes highest stack address to close
            auto new_sp = S->sp - num;
            close_upvals(S, new_sp);
            S->stack[new_sp] = S->stack[S->sp-1];
            S->sp = new_sp + 1;
        }
            break;
        case OP_GLOBAL: {
            auto id = code_short(S, pc);
            pc += 2;
            auto fqn = vsymbol(cur_fun()->stub->const_arr[id]);
            if (!push_global(S, fqn)) {
                ierror(S, "Failed to find global variable " + (*S->symtab)[fqn]);
                S->pc = pc;
                return;
            }
        }
            break;
        case OP_SET_GLOBAL: {
            auto id = code_short(S, pc);
            pc += 2;
            auto fqn = cur_fun()->stub->const_arr[id];
            set_global(S, vsymbol(fqn), peek(S, 0));
            S->stack[S->sp-1] = fqn;
        }
            break;
        case OP_OBJ_GET: {
            if (!vis_table(peek(S, 1))) {
                ierror(S, "obj-get target is not a table.");
                S->pc = pc;
                return;
            }
            auto x = vtable(peek(S, 1))->contents.get(peek(S, 0));
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
                S->pc = pc;
                return;
            }
            vtable(peek(S, 2))->contents.insert(peek(S, 1), peek(S, 0));
            S->stack[S->sp - 3] = peek(S, 0);
            S->sp -= 2;
        }
            break;

        case OP_CONST:
            push(S, cur_fun()->stub->const_arr[code_short(S, pc)]);
            pc += 2;
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
            auto u = code_short(S, pc);
            pc += 2 + *((i16*)&u);
        }
            break;
        case OP_CJUMP:
            if (!vtruth(peek(S, 0))) {
                auto u = code_short(S, pc);
                pc += 2 + *((i16*)&u);
            } else {
                pc += 2;
            }
            --S->sp;
            break;
        case OP_CALL:
            call(S, code_byte(S, pc++));
            if (S->err_happened) {
                S->pc = pc;
                return;
            }
            break;
        case OP_TCALL:
            if (!tail_call(S, code_byte(S, pc++), &pc)) {
                S->pc = pc;
                return;
            }
            break;
        case OP_CALLM: {
            auto num_args = code_byte(S, pc++);
            auto sym = peek(S, num_args+1);
            auto tab = peek(S, num_args);
            if (!vis_table(tab)) {
                ierror(S, "Method call operand not a table.");
                S->pc = pc;
                return;
            }
            if (!get_method(S, vtable(tab), sym, S->sp - num_args - 2)) {
                ierror(S, "Method lookup failed.");
                S->pc = pc;
                return;
            }
            call(S, num_args+1);
            if (S->err_happened) {
                S->pc = pc;
                return;
            }
        }
            break;
        case OP_TCALLM: {
            auto num_args = code_byte(S, pc++);
            auto sym = peek(S, num_args+1);
            auto tab = peek(S, num_args);
            if (!vis_table(tab)) {
                ierror(S, "Method call operand not a table.");
                return;
            }
            if (!get_method(S, vtable(tab), sym, S->sp - num_args - 2)) {
                ierror(S, "Method lookup failed.");
                return;
            }
            if (!tail_call(S, num_args+1, &pc)) {
                return;
            }
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
