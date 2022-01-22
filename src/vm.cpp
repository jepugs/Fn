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

// void vm_thread::runtime_error(const string& msg) const {
//     // FIXME: this sort of information should probably not go to the user. We
//     // could log it instead if we had a logger.
//     string s = "{ip:" + std::to_string(ip) + "} ";
//     if (callee && callee->stub->name.size() > 0) {
//         s = s + "(In function: " + callee->stub->name + ") ";
//     }
//     s = s + msg;
//     set_fault(err, chunk->location_of(ip), "vm", s);
//     throw runtime_exception{};
// }


// code_address vm_thread::make_tcall(fn_function* func) {
//     auto stub = func->stub;
//     chunk = stub->chunk;
//     callee = func;

//     auto num_opt = stub->pos_params.size - stub->req_args;
//     // stack pointer (equal to number of parameter variables)
//     u8 sp = static_cast<u8>(stub->pos_params.size
//             + num_opt // for indicators
//             + stub->vl_param.has_value());
//     bp = stack->get_pointer() - sp;

//     return stub->addr;
// }

// code_address vm_thread::tcall(local_address num_args) {
//     // the function to call should be on top
//     auto new_callee = peek();
//     if (v_tag(new_callee) != TAG_FUNC) {
//         runtime_error("Error on call: callee is not a function");
//         return ip + 2;
//     }
//     auto func = vfunction(new_callee);
//     auto stub = func->stub;
//     // foreign calls are just normal calls
//     if (stub->foreign != nullptr || callee == nullptr) {
//         return call(num_args);
//     }

//     // we're officially done with the previous function now
//     stack->pop_callee();
//     stack->push_callee(func);
//     pop();

//     // set up the call stack
//     stack->close_for_tc(num_args, bp);
//     arrange_call_stack(func, num_args);

//     return make_tcall(func);
// }


// code_address vm_thread::apply(local_address num_args, bool tail) {
//     // all we need to do is expand the varargs. The rest will be taken care of
//     // by call().
//     auto callee = peek(0);
//     auto args = peek(1);

//     if (!vis_function(callee)) {
//         runtime_error("OP_APPLY first argument not a function.");
//     } else if (!vis_cons(args) && args != V_EMPTY) {
//         runtime_error("OP_APPLY last argument not a list.");
//     }

//     auto func = vfunction(callee);
//     if (tail) {
//         stack->pop_callee();
//         stack->push_callee(func);
//         stack->close_for_tc(num_args + 1, bp);
//     } else {
//         stack->push_callee(func);
//     }
//     pop();

//     u32 list_len = 0;
//     for (auto it = args; it != V_EMPTY; it = vtail(it)) {
//         push(vtail(it));
//         set_from_top(1, vhead(it));
//         ++list_len;
//     }

//     pop();
//     arrange_call_stack(func, num_args + list_len);
//     return tail ? make_tcall(func) : make_call(func);
// }


// void vm_thread::step() {
//     u8 instr = chunk->read_byte(ip);

//     // variable for use inside the switch
//     value v1, v2, v3;

//     local_address num_args;

//     local_address l;
//     u16 id;

//     upvalue_cell* u;

//     // note: when an instruction uses an argument that occurs in the bytecode, it is responsible for
//     // updating the instruction pointer at the end of its exection (which should be after any
//     // exceptions that might be raised).
//     switch (instr) {
//     case OP_NOP:
//         break;
//     case OP_POP:
//         pop();
//         break;
//     case OP_COPY:
//         push(stack->peek(chunk->read_byte(ip+1)));
//         ++ip;
//         break;
//     case OP_LOCAL:
//         push(local(chunk->read_byte(ip+1)));
//         ++ip;
//         break;
//     case OP_SET_LOCAL:
//         set_local(chunk->read_byte(ip+1), peek(0));
//         pop();
//         ++ip;
//         break;

//     case OP_UPVALUE:
//         l = chunk->read_byte(ip+1);
//         // TODO: check upvalue exists
//         u = callee->upvals[l];
//         if (u->closed) {
//             push(u->closed_value);
//         } else{
//             if (callee == nullptr) {
//                 runtime_error("op-upvalue in toplevel frame.");
//             }
//             auto pos = u->pos;
//             push(stack->peek_bottom(pos));
//         }
//         ++ip;
//         break;
//     case OP_SET_UPVALUE:
//         l = chunk->read_byte(ip+1);
//         // TODO: check upvalue exists
//         u = callee->upvals[l];
//         if (u->closed) {
//             u->closed_value = stack->peek();
//         } else {
//             auto pos = callee->stub->upvals[l];
//             stack->set(pos, stack->peek());
//         }
//         stack->pop();
//         ++ip;
//         break;

//     case OP_CLOSURE:
//         id = chunk->read_short(ip+1);
//         v1 = stack->create_function(chunk->get_function(id), bp);

//         ip += 2;
//         break;

//     case OP_CLOSE:
//         num_args = chunk->read_byte(ip+1);
//         stack->close(stack->get_pointer() - num_args);
//         // TODO: check stack size >= num_args
//         ++ip;
//         break;

//     case OP_GLOBAL:
//         v1 = stack->peek();
//         if (v_tag(v1) != TAG_SYM) {
//             runtime_error("OP_GLOBAL name operand is not a symbol.");
//         }
//         v2 = get_global(v1);
//         pop();
//         push(v2);
//         break;
//     case OP_SET_GLOBAL:
//         v1 = stack->peek(); // value
//         v2 = stack->peek(1); // name
//         if (v_tag(v2) != TAG_SYM) {
//             runtime_error("op-set-global name operand is not a symbol.");
//         }
//         add_global(v2, v1);
//         pop_times(2);
//         break;
//     case OP_BY_GUID:
//         v1 = by_guid(stack->peek());
//         pop();
//         push(v1);
//         break;
//     case OP_MACRO:
//         v1 = stack->peek();
//         if (v_tag(v1) != TAG_SYM) {
//             runtime_error("OP_MACRO name operand is not a symbol.");
//         }
//         v2 = get_macro(v1);
//         stack->pop();
//         stack->push(v2);
//         break;
//     case OP_SET_MACRO:
//         v1 = stack->peek();
//         v2 = stack->peek(1);
//         if (v_tag(v2) != TAG_SYM) {
//             runtime_error("op-set-macro name operand is not a symbol.");
//         } else if (v_tag(v1) != TAG_FUNC) {
//             runtime_error("op-set-macro value is not a function.");
//         }
//         add_macro(v2, v1);
//         stack->pop_times(2);
//         break;

//     case OP_METHOD:
//         v1 = stack->peek();  // symbol
//         if (v_tag(v1) != TAG_SYM) {
//             runtime_error("Method lookup failed. Method name must be a symbol.");
//         }
//         v2 = stack->peek(1);
//         if (v_tag(v2) != TAG_TABLE) {
//             runtime_error("Method lookup failed. Target object is not a table.");
//         }
//         {
//             auto mt = vtable(v2)->metatable;
//             if (v_tag(mt) != TAG_TABLE) {
//                 runtime_error("Method lookup failed. Target object has no metatable.");
//             }
//             auto x = vtable(mt)->contents.get(v1);
//             if (!x.has_value()) {
//                 runtime_error("Method lookup failed. No such method found.");
//             }
//             pop();
//             set_from_top(0, *x);
//         }
//         break;

//     case OP_CONST:
//         id = chunk->read_short(ip+1);
//         if (id >= chunk->constant_arr.size) {
//             runtime_error("Attempt to access nonexistent constant.");
//         }
//         push(chunk->get_constant(id));
//         ip += 2;
//         break;

//     case OP_NIL:
//         push(V_NIL);
//         break;
//     case OP_FALSE:
//         push(V_FALSE);
//         break;
//     case OP_TRUE:
//         push(V_TRUE);
//         break;

//     case OP_OBJ_GET:
//         // key
//         v1 = peek(0);
//         // object
//         v2 = peek(1);
//         if (v_tag(v2) == TAG_TABLE) {
//             auto x = vtable(v2)->contents.get(v1);
//             if (x.has_value()) {
//                 pop();
//                 set_from_top(0, *x);
//             } else {
//                 pop();
//                 pop();
//                 push(V_NIL);
//             }
//             break;
//         } 
//         runtime_error("OP_OBJ_GET operand not a table.");
//         break;

//     case OP_OBJ_SET:
//         // new-value
//         v3 = stack->peek(0);
//         // key
//         v1 = stack->peek(1);
//         // object
//         v2 = stack->peek(2);
//         if (v_tag(v2) != TAG_TABLE) {
//             runtime_error("OP_OBJ_SET operand not a table.");
//         }
//         vtable(v2)->contents.insert(v1,v3);
//         stack->pop_times(3);
//         break;

//     case OP_IMPORT:
//         do_import();
//         break;

//     case OP_JUMP:
//         // one less than we actually want b/c the ip gets incremented at the
//         // end
//         ip += 2 + static_cast<i16>(chunk->read_short(ip+1));
//         break;

//     case OP_CJUMP:
//         // jump on false
//         if (!vtruth(stack->peek(0))) {
//             ip += 2 + static_cast<i16>(chunk->read_short(ip+1));
//         } else {
//             ip += 2;
//         }
//         stack->pop();
//         break;

//     case OP_CALL:
//         num_args = chunk->read_byte(ip+1);
//         ip = call(num_args) - 1;
//         break;

//     case OP_TCALL:
//         num_args = chunk->read_byte(ip+1);
//         ip = tcall(num_args) - 1;
//         break;

//     case OP_APPLY:
//         num_args = chunk->read_byte(ip+1);
//         ip = apply(num_args, false) - 1;
//         break;

//     case OP_TAPPLY:
//         num_args = chunk->read_byte(ip+1);
//         ip = apply(num_args, true) - 1;
//         break;

//     case OP_RETURN:
//         // check that we are in a call frame
//         if (callee == nullptr) {
//             runtime_error("Return instruction at top level.");
//         }

//         // do the return manipulation on the stack
//         stack->do_return(bp);
//         stack->pop_callee();
//         status = vs_return;

//         break;

//     case OP_TABLE:
//         stack->push_table();
//         break;

//     default:
//         runtime_error("Unrecognized opcode.");
//         break;
//     }
//     ++ip;

// }

// void vm_thread::execute(fault* err) {
//     this->err = err;
//     if (status == vs_waiting_for_import) {
//         auto x = globals->get_ns(pending_import_id);
//         if (!x.has_value()) {
//             set_fault(err, chunk->location_of(ip), "vm",
//                     "Import failed (no namespace created).");
//             return;
//         }
//         string prefix, stem;
//         ns_id_destruct((*symtab)[pending_import_id], &prefix, &stem);
//         // FIXME: maybe check for overwrites?
//         copy_defs(*symtab, **globals->get_ns(chunk->ns_id), **x, stem + ":");
//     }
//     status = vs_running;
//     try {
//         while (status == vs_running) {
//             if (ip >= chunk->code.size) {
//                 status = vs_stopped;
//                 break;
//             }
//             step();
//         }
//     } catch (const runtime_exception& ex) {
//         // fault object should already be set
//         status = vs_fault;
//     }

// }

}

