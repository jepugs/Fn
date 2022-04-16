#include "vm.hpp"

#include "config.h"

#include "allocator.hpp"
#include "bytes.hpp"
#include "namespace.hpp"
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

#define code_byte(S, where) (S->callee->stub->code[where])
#define code_short(S, where) (*((u16*)&S->callee->stub->code[where]))
#define code_u32(S, where) (*((u32*)&S->callee->stub->code[where]))
// #define code_byte(S, where) (S->code[where])
// #define code_short(S, where) (*((u16*)&S->code[where]))
// #define code_u32(S, where) (*((u32*)&S->code[where]))

#define push(S, v) S->stack[S->sp] = v;++S->sp;
#define peek(S, i) (S->stack[S->sp-((i))-1])

static void add_trace_frame(istate* S, fn_function* callee, u32 pc) {
    S->stack_trace.push_back(
            trace_frame{
                .callee = callee,
                .pc = pc
            });
}

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
static inline void create_fun(istate* S, u32 enclosing, constant_id fid) {
    // this sets up upvalues, and initializes initvals to nil
    // TODO: uncomment this
    alloc_fun(S, enclosing, fid);
    auto fun = vfunction(S->stack[S->sp - 1]);
    // set up initvals
    auto num_opt = fun->stub->num_opt;
    for (u32 i = 0; i < num_opt; ++i) {
        fun->init_vals[i] = S->stack[S->sp - 1 - num_opt + i];
    }
    // move the function to the appropriate place on the stack
    S->stack[S->sp - 1 - num_opt] = S->stack[S->sp - 1];
    S->sp = S->sp - num_opt;
}

static inline bool do_import(istate* S, symbol_id name, symbol_id alias) {
    auto src = get_ns(S, name);
    auto dest = get_ns(S, S->ns_id);
    if (src == nullptr) {
        ierror(S, "do_import() failed: can't find namespace to import\n");
        return false;
    }
    if (dest == nullptr) {
        ierror(S, "do_import() failed: current namespace doesn't exist\n");
        return false;
    }
    copy_defs(S, dest, src, symname(S, alias) + ":");
    return true;
}

// on success returns true and sets place on stack to the method. On failure
// returns false.
static inline bool get_method(istate* S, value obj, value key, u32 place) {
    auto m = get_metatable(S, obj);
    if (!vis_table(m)) {
        return false;
    }
    auto x = table_get(vtable(m), key);
    if (!x) {
        return false;
    }
    S->stack[place] = x[1];
    return true;
}

static inline bool arrange_call_stack(istate* S, u32 n) {
    auto num_params = S->callee->stub->num_params;
    auto num_opt = S->callee->stub->num_opt;
    auto vari = S->callee->stub->vari;
    u32 min_args = num_params - num_opt;
    if (n < min_args) {
        std::ostringstream os;
        os << "Too few arguments in call to function";
        if (S->callee && S->callee->stub->name) {
            os << " " << convert_fn_string(S->callee->stub->name);
        }
        os << ".";
        ierror(S, os.str());
        return false;
    } else if (n > num_params) {
        // handle variadic parameter
        if (vari) {
            pop_to_list(S, n - num_params);
        } else {
            ierror(S, "Too many arguments in function call.");
            return false;
        }
        // indicator args are all true in this case
        for (u32 i = min_args; i < num_params; ++i) {
            push(S, V_YES);
        }
    } else {
        for (u32 i = n; i < num_params; ++i) {
            push(S, S->callee->init_vals[i]);
        }
        if (vari) {
            push(S, V_EMPTY);
        }
        // push indicator args
        u32 m = num_params < n ? num_params : n;
        for (u32 i = min_args; i < m; ++i) {
            push(S, V_YES);
        }
        for (u32 i = n; i < num_params; ++i) {
            push(S, V_NO);
        }
    }
    return true;
}

static inline void foreign_call(istate* S, fn_function* fun, u32 n, u32 pc) {
    auto save_bp = S->bp;
    bool restore_callee = S->callee;
    S->bp = S->sp - n;
    fun->stub->foreign(S);
    if (has_error(S)) {
        // add the foreign function frame as well as the caller frame
        add_trace_frame(S, vfunction(S->stack[S->bp - 1]), 0);
        add_trace_frame(S, S->callee, pc);
        return;
    }
    S->stack[S->bp-1] = peek(S, 0);
    S->sp = S->bp;  // with the return value, this is the new stack pointer
    S->bp = save_bp;
    S->callee = restore_callee ? vfunction(S->stack[save_bp - 1]) : nullptr;
}

// unroll a list on top of the stack (i.e. place its elements in order on the
// stack). Returns the length of the list
static inline u32 unroll_list(istate* S) {
    u32 n = 0;
    while (peek(S,0) != V_EMPTY) {
        push(S, vtail(peek(S, 0)));
        S->stack[S->sp - 2] = vhead(S->stack[S->sp - 2]);
        ++n;
    }
    --S->sp;
    return n;
}

static void icall(istate* S, u32 n, u32 pc) {
    // update the call frame
    auto callee = peek(S, n);
    while (!vis_function(callee)) {
        if (vis_symbol(callee)) {
            // method call
            if (n == 0) {
                add_trace_frame(S, S->callee, pc);
                ierror(S, "Method call requires a self argument.");
                return;
            }
            if (!get_method(S, peek(S, n-1), callee, S->sp - n - 1)) {
                add_trace_frame(S, S->callee, pc);
                ierror(S, "Method lookup failed.");
                return;
            }
        } else if (vis_table(callee)) {
            u32 i;
            for (i = 0; i < n; ++i) {
                S->stack[S->sp - i] = S->stack[S->sp - i - 1];
            }
            ++S->sp;
            ++n;
            if (!get_method(S, callee,
                            vbox_symbol(cached_sym(S, SC___CALL)),
                            S->sp - n - 1)) {
                add_trace_frame(S, S->callee, pc);
                ierror(S, "Method lookup failed.");
                return;
            }
        } else {
            add_trace_frame(S, S->callee, pc);
            ierror(S, "Cannot call provided value.");
            return;
        }
        callee = peek(S, n);
    }

    auto fun = vfunction(callee);
    // FIXME: ensure minimum stack space available
    if (fun->stub->foreign) {
        if (S->sp + n + FOREIGN_MIN_STACK >= STACK_SIZE) {
            add_trace_frame(S, S->callee, pc);
            ierror(S, "Not enough stack space for call.");
            return;
        }
        foreign_call(S, fun, n, pc);
    } else {
        auto save_bp = S->bp;
        bool restore_callee = S->callee;
        S->callee = fun;
        S->bp = S->sp - n;
        if (S->bp + fun->stub->space >= STACK_SIZE) {
            // Can't complete function call for lack of stack space
            add_trace_frame(S, vfunction(S->stack[save_bp-1]), S->pc);
            ierror(S, "Not enough stack space for call.");
            return;
        }
        if (!arrange_call_stack(S, n)) {
            return;
        }
        execute_fun(S);
        if (has_error(S)) {
            if (restore_callee) {
                // notice we add the stack trace for the calling function, not
                // the callee. The callee is added at the error origin
                add_trace_frame(S, vfunction(S->stack[save_bp-1]), pc);
                return;
            }
        }
        // return value
        S->stack[S->bp-1] = peek(S, 0);
        S->sp = S->bp;  // with the return value, this is the new stack pointer
        S->bp = save_bp;
        S->callee = restore_callee ? vfunction(S->stack[save_bp - 1]) : nullptr;
    }
}

void call(istate* S, u8 n) {
    icall(S, n, 0);
}

static inline bool tail_call(istate* S, u8 n, u32* pc) {
    auto callee = peek(S, n);
    while (!vis_function(callee)) {
        if (vis_symbol(callee)) {
            // method call
            if (n == 0) {
                add_trace_frame(S, S->callee, *pc);
                ierror(S, "Method call requires a self argument.");
                return false;
            }
            if (!get_method(S, peek(S, n-1), callee, S->sp - n - 1)) {
                add_trace_frame(S, S->callee, *pc);
                ierror(S, "Method lookup failed.");
                return false;
            }
        } else if (vis_table(callee)) {
            u32 i;
            for (i = 0; i < n; ++i) {
                S->stack[S->sp - i] = S->stack[S->sp - i - 1];
            }
            ++S->sp;
            ++n;
            if (!get_method(S, callee,
                            vbox_symbol(cached_sym(S, SC___CALL)),
                            S->sp - n - 1)) {
                add_trace_frame(S, S->callee, *pc);
                ierror(S, "Method lookup failed.");
                return false;
            }
        } else {
            add_trace_frame(S, S->callee, *pc);
            ierror(S, "Cannot call provided value.");
            return false;
        }
        callee = peek(S, n);
    }

    auto fun = vfunction(callee);
    if (fun->stub->foreign) {
        foreign_call(S, fun, n, *pc);
        return true;
    }
    // set these so the GC can't get 'em before we're done
    S->callee = fun;
    S->stack[S->bp - 1] = callee;
    close_upvals(S, S->bp);
    // move the new call information to the base pointer
    for (u32 i = 0; i < n; ++i) {
        S->stack[S->bp + i] = S->stack[S->sp - n + i];
    }
    S->sp = S->bp + n;
    if (!arrange_call_stack(S, n)) {
        return false;
    }
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
            push(S, S->stack[S->bp + code_byte(S, pc++)]);
            break;
        case OP_SET_LOCAL:
            S->stack[S->bp+code_byte(S, pc++)] = peek(S, 0);
            --S->sp;
            break;
        case OP_COPY:
            push(S, S->stack[S->sp - code_byte(S, pc++) - 1]);
            break;
        case OP_UPVALUE: {
            auto u = S->callee->upvals[code_byte(S, pc++)];
            if (u->closed) {
                push(S, u->datum.val);
            } else {
                push(S, S->stack[u->datum.pos]);
            }
        }
            break;
        case OP_SET_UPVALUE: {
            auto u = S->callee->upvals[code_byte(S, pc++)];
            if (u->closed) {
                u->datum.val = peek(S, 0);
                if (vhas_header(peek(S, 0))) {
                    write_guard(get_gc_card(&u->h), vheader(peek(S,0)));
                }
            } else {
                S->stack[u->datum.pos] = peek(S, 0);
            }
            --S->sp;
        }
            break;
        case OP_CLOSURE: {
            auto fid = code_short(S, pc);
            pc += 2;
            create_fun(S, S->bp-1, fid);
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
            auto id = code_u32(S, pc);
            pc += 4;
            auto v = S->G->def_arr[id];
            if (v == V_UNIN) {
                add_trace_frame(S, S->callee, pc - 5);
                if (id >= S->G->def_ids.size) {
                    ierror(S, "Global variable with invalid ID.\n");
                } else {
                    ierror(S, "Failed to find global variable " +
                            symname(S, S->G->def_ids[id]));
                }
                return;
            }
            push(S, v);

        }
            break;
        case OP_SET_GLOBAL: {
            auto id = code_u32(S, pc);
            pc += 4;
            S->G->def_arr[id] = peek(S, 0);
            S->stack[S->sp-1] = V_NIL;
        }
            break;
        case OP_OBJ_GET: {
            if (!vis_table(peek(S, 1))) {
                add_trace_frame(S, S->callee, pc - 1);
                ierror(S, "obj-get target is not a table.");
                return;
            }
            auto x = table_get(vtable(peek(S, 1)), peek(S, 0));
            S->sp -= 2;
            if (x) {
                push(S, x[1]);
            } else {
                push(S, V_NIL);
            }
        }
            break;
        case OP_OBJ_SET: {
            if (!vis_table(peek(S, 2))) {
                add_trace_frame(S, S->callee, pc - 1);
                ierror(S, "obj-set target is not a table.");
                return;
            }
            table_insert(S, S->sp - 3, S->sp - 2, S->sp - 1);
            S->stack[S->sp - 3] = peek(S, 0);
            S->sp -= 2;
        }
            break;
        case OP_MACRO: {
            auto id = code_short(S, pc);
            pc += 2;
            auto fqn = vsymbol(S->callee->stub->const_arr[id]);
            auto x = S->G->macro_tab.get2(fqn);
            if (!x) {
                add_trace_frame(S, S->callee, pc - 3);
                ierror(S, "Failed to find global variable " + (*S->symtab)[fqn]);
                return;
            }
            push(S, vbox_function(x->val));
        }
            break;
        case OP_SET_MACRO: {
            // FIXME: check whether the new macro is a function and the name is
            // a symbol.
            auto id = code_short(S, pc);
            pc += 2;
            auto fqn = S->callee->stub->const_arr[id];
            set_macro(S, vsymbol(fqn), vfunction(peek(S, 0)));
            S->stack[S->sp-1] = fqn;
        }
            break;
        case OP_CONST:
            push(S, S->callee->stub->const_arr[code_short(S, pc)]);
            pc += 2;
            break;
        case OP_NIL:
            push(S, V_NIL);
            break;
        case OP_NO:
            push(S, V_NO);
            break;
        case OP_YES:
            push(S, V_YES);
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
            icall(S, code_byte(S, pc), pc - 1);
            pc++;
            if (has_error(S)) {
                return;
            }
            break;
        case OP_TCALL:
            if (!tail_call(S, code_byte(S, pc++), &pc)) {
                add_trace_frame(S, S->callee, pc - 1);
                return;
            }
            break;
        case OP_CALLM: {
            auto num_args = code_byte(S, pc++);
            auto sym = peek(S, num_args);
            auto tab = peek(S, num_args-1);
            if (!get_method(S, tab, sym, S->sp - num_args - 1)) {
                add_trace_frame(S, S->callee, pc - 2);
                ierror(S, "Method lookup failed.");
                return;
            }
            icall(S, num_args, pc - 2);
            if (has_error(S)) {
                return;
            }
        }
            break;
        case OP_TCALLM: {
            auto num_args = code_byte(S, pc++);
            auto sym = peek(S, num_args);
            auto tab = peek(S, num_args-1);
            if (!get_method(S, tab, sym, S->sp - num_args - 1)) {
                add_trace_frame(S, S->callee, pc - 2);
                ierror(S, "Method lookup failed.");
                return;
            }
            if (!tail_call(S, num_args, &pc)) {
                add_trace_frame(S, S->callee, pc - 2);
                return;
            }
        }
            break;
        case OP_APPLY: {
            // unroll the list on top of the stack
            if (!vis_list(peek(S, 0))) {
                add_trace_frame(S, S->callee, pc - 1);
                ierror(S, "Final argument to apply must be a list.");
                return;
            }
            auto n = code_byte(S, pc++) + unroll_list(S);
            icall(S, n, pc - 2);
            if (has_error(S)) {
                return;
            }
        }
            break;
        case OP_TAPPLY: {
            // unroll the list on top of the stack
            if (!vis_list(peek(S, 0))) {
                add_trace_frame(S, S->callee, pc - 1);
                ierror(S, "Final argument to apply must be a list.");
                return;
            }
            auto n = code_byte(S, pc++) + unroll_list(S);
            if (!tail_call(S, n, &pc)) {
                add_trace_frame(S, S->callee, pc - 2);
                return;
            }
        }
            break;

        case OP_RETURN:
            // close upvalues and exit the loop. The icall() function will handle
            // moving the return value.
            close_upvals(S, S->bp);
            return;
            break;

        case OP_IMPORT:
            if (!vis_symbol(peek(S, 1)) || !vis_symbol(peek(S, 0))) {
                add_trace_frame(S, S->callee, pc - 1);
                ierror(S, "import arguments must be symbols\n");
                return;
            }
            if (!do_import(S, vsymbol(peek(S, 1)), vsymbol(peek(S, 0)))) {
                add_trace_frame(S, S->callee, pc - 1);
                return;
            }
            pop(S, 2);
            break;

        case OP_LIST:
            pop_to_list(S, code_byte(S, pc++));
            break;
        }
    }
}

}
