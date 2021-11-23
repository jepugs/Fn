#include "vm.hpp"

#include "config.h"

#include "bytes.hpp"
#include "namespace.hpp"
#include "values.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>


namespace fn {

vm_thread::vm_thread(allocator* use_alloc, global_env* use_globals,
        code_chunk* use_chunk)
    : symtab{use_globals->get_symtab()}
    , globals{use_globals}
    , alloc{use_alloc}
    , chunk{use_chunk}
    , ip{0}
    , frame{new call_frame{nullptr, 0, use_chunk, 0, nullptr}} {
    stack = alloc->add_root_stack();
}

vm_thread::~vm_thread() {
    // delete all call frames
    while (frame != nullptr) {
        auto tmp = frame->prev;
        // TODO: ensure reference count for upvalue_slot is decremented
        delete frame;
        frame = tmp;
    }
}

vm_status vm_thread::check_status() const {
    return status;
}

value vm_thread::get_symbol(const string& name) {
    return as_sym_value(get_symtab()->intern(name));
}

u32 vm_thread::get_ip() const {
    return ip;
}

value vm_thread::last_pop() const {
    return stack->get_last_pop();
}

void vm_thread::add_global(value name, value v) {
    auto ns_id = cur_chunk()->ns_id;
    auto ns = *globals->get_ns(ns_id);
    ns->set(vsymbol(name), v);
}

value vm_thread::get_global(value name) {
    auto ns_id = cur_chunk()->ns_id;
    auto ns = *globals->get_ns(ns_id);
    auto res = ns->get(vsymbol(name));

    if (!res.has_value()) {
        runtime_error("Attempt to access unbound global variable "
                + v_to_string(name, get_symtab()));
    }
    return *res;
}

void vm_thread::add_macro(value name, value v) {
    auto ns_id = cur_chunk()->ns_id;
    auto ns = *globals->get_ns(ns_id);
    ns->set_macro(vsymbol(name), v);
}

value vm_thread::get_macro(value name) {
    auto ns_id = cur_chunk()->ns_id;
    auto ns = *globals->get_ns(ns_id);
    auto res = ns->get_macro(vsymbol(name));

    if (!res.has_value()) {
        runtime_error("Attempt to access unbound global variable "
                + v_to_string(name, get_symtab()));
    }
    return *res;
}



optional<value> vm_thread::try_import(symbol_id ns_id) {
    // TODO: write this
    return std::nullopt;
}

void vm_thread::do_import() {
    // auto ns_id = pop().usym_id();
    // TODO: write this :(
}


code_chunk* vm_thread::cur_chunk() const {
    if (frame->caller) {
        return frame->caller->stub->chunk;
    } else {
        return chunk;
    }
}
code_chunk* vm_thread::get_chunk() {
    return chunk;
}
allocator* vm_thread::get_alloc() {
    return alloc;
}
symbol_table* vm_thread::get_symtab() {
    return symtab;
}

void vm_thread::runtime_error(const string& msg) const {
    auto p = cur_chunk()->location_of(ip);
    throw fn_error("runtime", "(ip = " + std::to_string(ip) + ") " + msg, p);
}

void vm_thread::push(value v) {
    if (stack->get_pointer() >= STACK_SIZE - 1) {
        runtime_error("stack exhausted.");
    }
    stack->push(v);
}

void vm_thread::pop() {
    if (frame->bp >= stack->get_pointer()) {
        runtime_error("pop on empty call frame");
    }
    stack->pop();
}

value vm_thread::pop_to_ws(working_set* ws) {
    if (frame->bp >= stack->get_pointer()) {
        runtime_error("pop on empty call frame");
    }
    auto res = ws->pin_value(peek());
    stack->pop();
    return res;
}

void vm_thread::pop_times(stack_address n) {
    if (frame->bp >= stack->get_pointer() - n + 1) {
        runtime_error("pop on empty call frame");
    }
    stack->pop_times(n);
}

value vm_thread::peek(stack_address i) const {
    if (i >= stack->get_pointer()) {
        runtime_error("peek out of stack bounds");
    }
    return stack->peek(i);
}

value vm_thread::local(local_address i) const {
    stack_address pos = i + frame->bp;
    if (pos >= stack->get_pointer()) {
        runtime_error("out of stack bounds on local");
    }
    return stack->peek_bottom(pos);
}

void vm_thread::set_local(local_address i, value v) {
    stack_address pos = i + frame->bp;
    if (pos >= stack->get_pointer()) {
        runtime_error("out of stack bounds on set-local.");
    }
    stack->set(pos, v);
}

void vm_thread::set_from_top(local_address i, value v) {
    stack_address pos = stack->get_pointer() - i;
    if (pos < frame->bp) {
        runtime_error("out of stack bounds on set-local.");
    }
    stack->set(pos, v);
}

table<local_address,value> vm_thread::process_kw_table(function_stub* stub,
        local_address num_args,
        value kw_tab,
        value var_table) {
    auto& kw = vtable(kw_tab)->contents;
    table<local_address,value> res;
    for (auto k : kw.keys()) {
        auto id = vsymbol(k);
        // first, check if this is a positional argument we still need
        bool found_pos = false;
        for (auto i = num_args; i < stub->pos_params.size(); ++i) {
            if(stub->pos_params[i] == id) {
                res.insert(i, *kw.get(k));
                found_pos = true;
                break;
            }
        }
        if (found_pos) {
            continue;
        }
        if (!stub->vt_param.has_value()) {
            // if there's no variadic table argument, we have an error here
            runtime_error("Unrecognized or redundant keyword in call.");
        } else {
            vtable(var_table)->contents.insert(k, *kw.get(k));
        }
    }
    return res;
}

void vm_thread::arrange_call_stack(working_set* ws,
        function* func,
        local_address num_args) {
    auto num_pos_args = func->stub->pos_params.size();
    auto has_vl = func->stub->vl_param.has_value();
    auto has_vt = func->stub->vt_param.has_value();
    auto req_args = func->stub->req_args;

    auto kw_tab = pop_to_ws(ws); // keyword argument table
    if (v_tag(kw_tab) != TAG_TABLE) {
        runtime_error("Error on call instruction: malformed keyword table.");
    }

    // For the most part, positional arguments can get left where they are.
    // Extra positional arguments go to the variadic list parameter
    value var_list = V_EMPTY;
    if (num_pos_args < num_args) {
        if (!has_vl) {
            runtime_error("Too many positional arguments to function.");
        }
        i32 m = num_args - num_pos_args;
        for (auto i = 0; i < m; ++i) {
            // this builds the list in the correct order
            var_list = ws->add_cons(pop_to_ws(ws), var_list);
        }
    }

    // handle the keyword table
    value var_tab = ws->add_table();
    table<local_address,value> extra_pos =
        process_kw_table(func->stub, num_args, kw_tab, var_tab);
    // put positional args in place
    for (u32 i = num_args; i < req_args; ++i) {
        auto x = extra_pos.get(i);
        if (!x.has_value()) {
            std::cout << "name is " << (*symtab)[func->stub->pos_params[i]] << '\n';
            runtime_error("Missing required argument in call.");
        }
        push(*x);
    }
    for (u32 i = req_args; i < num_pos_args; ++i) {
        auto x = extra_pos.get(i);
        if (!x.has_value()) {
            push(func->init_vals[i - req_args]);
        } else {
            push(*x);
        }
    }
    // put variadic args
    if (has_vl) {
        push(var_list);
    }
    if (has_vt) {
        push(var_tab);
    }
}

// Calling convention: a call uses num_args + 2 elements on the stack. At the
// very bottom we expect the callee, followed by num_args positional arguments,
// and finally a table containing keyword arguments.
code_address vm_thread::call(working_set* ws, local_address num_args) {
    // the function to call should be on top
    auto callee = pop_to_ws(ws);
    if (v_tag(callee) != TAG_FUNC) {
        runtime_error("Error on call: callee is not a function");
        return ip + 2;
    }
    auto func = vfunction(callee);

    // set the arguments
    arrange_call_stack(ws, func, num_args);

    auto stub = func->stub;

    // stack pointer (equal to number of parameter variables)
    u8 sp = static_cast<u8>(stub->pos_params.size()
            + stub->vl_param.has_value()
            + stub->vt_param.has_value());
    if (stub->foreign != nullptr) { // foreign function call
        interpreter_handle handle{.inter=this, .ws=ws,
            .func_name="<ffi call>"};
        auto args = new value[sp];
        for (i32 i = sp; i > 0; --i) {
            args[i-1] = pop_to_ws(ws);
        }
        push(stub->foreign(&handle, args));
        delete[] args;
        return ip + 2;
    } else { // native function call
        // base pointer
        u32 bp = stack->get_pointer() - sp;
        // extend the call frame
        frame = new call_frame{frame, ip+2, chunk, bp, func, sp};
        chunk = stub->chunk;
        return stub->addr;
    }
}

code_address vm_thread::tcall(working_set* ws, local_address num_args) {
    // the function to call should be on top
    auto callee = pop_to_ws(ws);
    if (v_tag(callee) != TAG_FUNC) {
        runtime_error("Error on call: callee is not a function");
        return ip + 2;
    }

    // save the values for the call operation
    dyn_array<value> saved_stack;
    for (i32 i = 0; i < num_args + 2; ++i) {
        saved_stack.push_back(pop_to_ws(ws));
    }

    // time to obliterate the old call frame...
    pop_times(stack->get_pointer() - frame->bp);
    // set up the stack
    for (i32 i = num_args + 2; i > 0; --i) {
        push(saved_stack[i-1]);
    }
    auto func = vfunction(callee);
    arrange_call_stack(ws, func, num_args);

    // update the frame
    auto stub = func->stub;
    u8 sp = static_cast<u8>(stub->pos_params.size()
            + stub->vl_param.has_value()
            + stub->vt_param.has_value());
    frame->num_args = sp;
    frame->caller = func;
    chunk = stub->chunk;
    return stub->addr;
}


code_address vm_thread::apply(working_set* ws, local_address num_args) {
    // all we need to do is expand the varargs. The rest will be taken care of
    // by call.
    auto callee = pop_to_ws(ws);
    auto kw_tab = pop_to_ws(ws);

    auto args = pop_to_ws(ws);
    if (args != V_EMPTY && !args.is_cons()) {
        runtime_error("apply argument list not actually a list");
    }
    u32 list_len = 0;
    for (auto it = args; it != V_EMPTY; it = v_tail(it)) {
        push(v_head(it));
        ++list_len;
    }

    // put these back where we found them
    push(kw_tab);
    push(callee);
    return call(ws, num_args + list_len);
}



void vm_thread::init_function(working_set* ws, function* f) {
    auto stub = f->stub;
    if (stub->foreign != nullptr) {
        // no init to do here
        return;
    }

    // Add init values
    // DANGER! Init vals are popped right off the stack, so they better be there
    // when this function gets initialized!
    auto len = stub->pos_params.size() - stub->req_args;
    for (u32 i = 0; i < len; ++i) {
        f->init_vals[i] = pop_to_ws(ws);
    }

    // set upvalues
    for (auto i = 0; i < stub->num_upvals; ++i) {
        auto pos = stub->upvals[i];
        if (stub->upvals_direct[i]) {
            auto u = stack->get_upvalue(frame->bp + pos);
            u->reference();
            f->upvals[i] = u;
        } else {
            auto u = frame->caller->upvals[pos];
            u->reference();
            f->upvals[i] = u;
        }
    }
}

#define push_bool(b) push(b ? V_TRUE : V_FALSE);
void vm_thread::step() {

    u8 instr = cur_chunk()->read_byte(ip);

    // variable for use inside the switch
    value v1, v2, v3;
    optional<value*> vp;

    bool jump = false;
    code_address addr = 0;

    local_address num_args;

    function_stub* stub;

    local_address l;
    u16 id;

    call_frame *tmp;

    upvalue_cell* u;

    working_set ws{alloc};

    // note: when an instruction uses an argument that occurs in the bytecode, it is responsible for
    // updating the instruction pointer at the end of its exection (which should be after any
    // exceptions that might be raised).
    switch (instr) {
    case OP_NOP:
        break;
    case OP_POP:
        pop();
        break;
    case OP_COPY:
        v1 = peek(cur_chunk()->read_byte(ip+1));
        push(v1);
        ++ip;
        break;
    case OP_LOCAL:
        v1 = local(cur_chunk()->read_byte(ip+1));
        push(v1);
        ++ip;
        break;
    case OP_SET_LOCAL:
        v1 = pop_to_ws(&ws);
        set_local(cur_chunk()->read_byte(ip+1), v1);
        ++ip;
        break;

    case OP_UPVALUE:
        l = cur_chunk()->read_byte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        if (u->closed) {
            push(u->closed_value);
        } else {
            if (frame->prev == nullptr) {
                runtime_error("Upvalue get in toplevel frame.");
            }
            auto pos = u->pos;
            push(stack->peek_bottom(pos));
        }
        ++ip;
        break;
    case OP_SET_UPVALUE:
        l = cur_chunk()->read_byte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        if (u->closed) {
            u->closed_value = pop_to_ws(&ws);
        } else {
            auto pos = frame->caller->stub->upvals[l] + frame->bp;
            stack->set(pos, pop_to_ws(&ws));
        }
        ++ip;
        break;

    case OP_CLOSURE:
        id = cur_chunk()->read_short(ip+1);
        stub = cur_chunk()->get_function(cur_chunk()->read_short(ip+1));
        v1 = ws.add_function(stub);
        init_function(&ws, vfunction(v1));
        push(v1);
        ip += 2;
        break;

    case OP_CLOSE:
        num_args = cur_chunk()->read_byte(ip+1);
        stack->close(stack->get_pointer() - num_args);
        // TODO: check stack size >= num_args
        ++ip;
        break;

    case OP_GLOBAL:
        v1 = pop_to_ws(&ws);
        if (v_tag(v1) != TAG_SYM) {
            runtime_error("OP_GLOBAL name operand is not a symbol.");
        }
        push(get_global(v1));
        break;
    case OP_SET_GLOBAL:
        v1 = pop_to_ws(&ws); // value
        v2 = pop_to_ws(&ws); // name
        if (v_tag(v2) != TAG_SYM) {
            runtime_error("OP_SET_GLOBAL name operand is not a symbol.");
        }
        add_global(v2, v1);
        break;
    case OP_MACRO:
        v1 = pop_to_ws(&ws);
        if (v_tag(v1) != TAG_SYM) {
            runtime_error("OP_MACRO name operand is not a symbol.");
        }
        push(get_macro(v1));
        break;
    case OP_SET_MACRO:
        v1 = pop_to_ws(&ws);
        v2 = pop_to_ws(&ws);
        if (v_tag(v2) != TAG_SYM) {
            runtime_error("OP_SET_MACRO name operand is not a symbol.");
        }
        add_macro(v2, v1);
        break;

    case OP_CONST:
        id = cur_chunk()->read_short(ip+1);
        if (id >= cur_chunk()->constant_arr.size) {
            runtime_error("attempt to access nonexistent constant.");
        }
        push(cur_chunk()->get_constant(id));
        ip += 2;
        break;

    case OP_NIL:
        push(V_NIL);
        break;
    case OP_FALSE:
        push(V_FALSE);
        break;
    case OP_TRUE:
        push(V_TRUE);
        break;

    case OP_OBJ_GET:
        // key
        v1 = pop_to_ws(&ws);
        // object
        v2 = pop_to_ws(&ws);
        if (v_tag(v2) == TAG_TABLE) {
            if (vtable(v2)->contents.has_key(v1)) {
                push(*vtable(v2)->contents.get(v1));
            } else {
                push(V_NIL);
            }
            break;
        } 
        runtime_error("obj-get operand not a table");
        break;

    case OP_OBJ_SET:
        // new-value
        v3 = pop_to_ws(&ws);
        // key
        v1 = pop_to_ws(&ws);
        // object
        v2 = pop_to_ws(&ws);
        if (v_tag(v2) != TAG_TABLE) {
            runtime_error("obj-set operand not a table");
        }
        vtable(v2)->contents.insert(v1,v3);
        break;

    case OP_IMPORT:
        do_import();
        // ns = import_ns(v1);
        // push(as_value(ns));
        break;

    case OP_JUMP:
        jump = true;
        addr = ip + 3 + static_cast<i16>(cur_chunk()->read_short(ip+1));
        break;

    case OP_CJUMP:
        // jump on false
        if (!v_truthy(pop_to_ws(&ws))) {
            jump = true;
            addr = ip + 3 + static_cast<i16>(cur_chunk()->read_short(ip+1));
        } else {
            ip += 2;
        }
        break;

    case OP_CALL:
        num_args = cur_chunk()->read_byte(ip+1);
        jump = true;
        addr = call(&ws, num_args);
        break;

    case OP_TCALL:
        num_args = cur_chunk()->read_byte(ip+1);
        jump = true;
        addr = tcall(&ws, num_args);
        break;

    case OP_APPLY:
        num_args = cur_chunk()->read_byte(ip+1);
        jump = true;
        addr = apply(&ws, num_args);
        break;

    case OP_RETURN:
        // check that we are in a call frame
        if (frame->caller == nullptr) {
            runtime_error("return instruction at top level.");
        }
        // get return value
        v1 = pop_to_ws(&ws);

        // jump to return address
        jump = true;
        addr = frame->ret_addr;
        chunk = frame->ret_chunk;

        num_args = frame->num_args;
        // close to base pointer
        stack->close(frame->bp);
        tmp = frame;
        // TODO: restore stack pointer
        frame = tmp->prev;
        delete tmp;

        push(v1);
        break;

    case OP_TABLE:
        push(ws.add_table());
        break;

    default:
        runtime_error("unrecognized opcode");
        break;
    }
    ++ip;

    if (jump) {
        ip = addr;
    }
}

void vm_thread::execute() {
    status = vs_running;
    while (status == vs_running) {
        if (ip >= cur_chunk()->code.size) {
            status = vs_stopped;
            break;
        }
        step();
    }
}

// disassemble a single instruction, writing output to out
void disassemble_instr(const code_chunk& code, code_address ip, std::ostream& out) {
    u8 instr = code.read_byte(ip);
    switch (instr) {
    case OP_NOP:
        out << "nop";
        break;
    case OP_POP:
        out << "pop";
        break;
    case OP_LOCAL:
        out << "local " << (i32)code.read_byte(ip+1);
        break;
    case OP_SET_LOCAL:
        out << "set-local " << (i32)code.read_byte(ip+1);
        break;
    case OP_COPY:
        out << "copy " << (i32)code.read_byte(ip+1);
        break;
    case OP_UPVALUE:
        out << "upvalue " << (i32)code.read_byte(ip+1);
        break;
    case OP_SET_UPVALUE:
        out << "set-upvalue " << (i32)code.read_byte(ip+1);
        break;
    case OP_CLOSURE:
        out << "closure " << code.read_short(ip+1);
        break;
    case OP_CLOSE:
        out << "close " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_GLOBAL:
        out << "global";
        break;
    case OP_SET_GLOBAL:
        out << "set-global";
        break;
    case OP_CONST:
        out << "const " << code.read_short(ip+1);
        break;
    case OP_NIL:
        out << "nil";
        break;
    case OP_FALSE:
        out << "false";
        break;
    case OP_TRUE:
        out << "true";
        break;
    case OP_OBJ_GET:
        out << "obj-get";
        break;
    case OP_OBJ_SET:
        out << "obj-set";
        break;
    case OP_MACRO:
        out << "macro";
        break;
    case OP_SET_MACRO:
        out << "set-macro";
        break;
    case OP_IMPORT:
        out << "import";
        break;
    case OP_JUMP:
        out << "jump " << (i32)(static_cast<i16>(code.read_short(ip+1)));
        break;
    case OP_CJUMP:
        out << "cjump " << (i32)(static_cast<i16>(code.read_short(ip+1)));
        break;
    case OP_CALL:
        out << "call " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_TCALL:
        out << "tcall " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_APPLY:
        out << "apply " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_RETURN:
        out << "return";
        break;
    case OP_TABLE:
        out << "table";
        break;

    default:
        out << "<unrecognized opcode: " << (i32)instr << ">";
        break;
    }
}

void disassemble(const symbol_table& symtab, const code_chunk& code, std::ostream& out) {
    u32 ip = 0;
    // TODO: annotate with line number
    while (ip < code.code.size) {
        u8 instr = code.read_byte(ip);
        // write line
        out << std::setw(6) << ip << "  ";
        disassemble_instr(code, ip, out);

        // additional information
        if (instr == OP_CONST) {
            // write constant value
            out << " ; "
                << v_to_string(code.get_constant(code.read_short(ip+1)),
                        &symtab);
        } else if (instr == OP_CLOSURE) {
            out << " ; addr = " << code.get_function(code.read_short(ip+1))->addr;
        }

        out << "\n";
        ip += instr_width(instr);
    }
}

}

