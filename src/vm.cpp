#include "vm.hpp"

#include "config.h"

#include "bytes.hpp"
#include "namespace.hpp"
#include "ffi/fn_handle.hpp"
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
    , status{vs_stopped}
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
    stack->kill();
}

vm_status vm_thread::check_status() const {
    return status;
}

symbol_id vm_thread::get_pending_import_id() const {
    return pending_import_id;
}

value vm_thread::get_symbol(const string& name) {
    return vbox_symbol(get_symtab()->intern(name));
}

u32 vm_thread::get_ip() const {
    return ip;
}

void vm_thread::set_ip(code_address new_ip)  {
    ip = new_ip;
}

value vm_thread::last_pop(working_set* ws) const {
    return ws->pin_value(stack->get_last_pop());
}

void vm_thread::add_global(value name, value v) {
    if (!vis_symbol(name)) {
        runtime_error("Variable names must be symbols.");
    }
    if (vhas_header(v)) {
        alloc->designate_global(vheader(v));
    }
    auto ns_id = chunk->ns_id;
    auto ns = *globals->get_ns(ns_id);
    ns->set(vsymbol(name), v);
}

value vm_thread::get_global(value name) {
    auto ns_id = chunk->ns_id;
    auto ns = *globals->get_ns(ns_id);
    if (!vis_symbol(name)) {
        runtime_error("Variable names must be symbols.");
    }
    auto res = ns->get(vsymbol(name));

    if (!res.has_value()) {
        runtime_error("Attempt to access unbound global variable "
                + v_to_string(name, get_symtab()));
    }
    return *res;
}

value vm_thread::by_guid(value name) {
    if (!vis_symbol(name)) {
        runtime_error("Variable GUIDs must be symbols.");
    }
    auto str = symtab->symbol_name(vsymbol(name)).substr(2);
    u32 i;
    for (i = 0; i < str.size(); ++i) {
        if (str[i] == ':') {
            break;
        }
    }
    if (i == str.size()) {
        runtime_error("Missing colon in GUID.");
    }

    auto ns_str = str.substr(0, i);
    auto var_str = str.substr(i+1);
    if (ns_str == "") {
        runtime_error("Empty namespace name in GUID.");
    }
    if (var_str == "") {
        runtime_error("Empty variable name in GUID.");
    }
    auto ns = globals->get_ns(symtab->intern(ns_str));
    if (!ns.has_value()) {
        runtime_error("GUID corresponds to nonexistent namespace.");
    }
    auto x = (**ns).get(symtab->intern(var_str));
    if (!x.has_value()) {
        runtime_error("GUID corresponds to nonexistent definition.");
    }
    return *x;
}

void vm_thread::add_macro(value name, value v) {
    if (!vis_function(v)) {
        runtime_error("op-macro value not a function.");
    }
    alloc->designate_global(vheader(v));
    auto ns_id = chunk->ns_id;
    auto ns = *globals->get_ns(ns_id);
    ns->set_macro(vsymbol(name), v);
}

value vm_thread::get_macro(value name) {
    auto ns_id = chunk->ns_id;
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
    auto ns_id = peek(0);
    if (!vis_symbol(ns_id)) {
        runtime_error("OP_IMPORT name must be a symbol.");
    }
    // since it's a symbol we don't need to worry about leaving it where the gc
    // can see it.
    pop();
    auto x = globals->get_ns(vsymbol(ns_id));
    if (!x.has_value()) {
        // this will be saved as the last pop before deferring control to the
        // interpreter
        pending_import_id = vsymbol(ns_id);
        status = vs_waiting_for_import;
    } else {
        string prefix, stem;
        ns_id_destruct((*symtab)[vsymbol(ns_id)], &prefix, &stem);
        // FIXME: maybe check for overwrites?
        copy_defs(*symtab, **globals->get_ns(chunk->ns_id), **x, stem + ":");
    }
}


code_chunk* vm_thread::get_chunk() const {
    return chunk;
}
allocator* vm_thread::get_alloc() const {
    return alloc;
}
symbol_table* vm_thread::get_symtab() const {
    return symtab;
}
const root_stack* vm_thread::get_stack() const {
    return stack;
}


void vm_thread::runtime_error(const string& msg) const {
    // FIXME: this sort of information should probably not go to the user. We
    // could log it instead if we had a logger.
    string s = "{ip:" + std::to_string(ip) + "} ";
    if (frame->caller && frame->caller->stub->name.size() > 0) {
        s = s + "(In function: " + frame->caller->stub->name + ") ";
    }
    s = s + msg;
    set_fault(err, chunk->location_of(ip), "vm", s);
    throw runtime_exception{};
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
    auto res = ws->pin_value(stack->peek());
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

void vm_thread::arrange_call_stack(function* func,
        local_address num_args) {
    auto req_args = func->stub->req_args;    
    auto num_pos_args = func->stub->pos_params.size;
    auto has_vl = func->stub->vl_param.has_value();

    // For the most part, positional arguments can get left where they are.
    // Extra positional arguments go to the variadic parameter

    if (num_args < req_args) {
        runtime_error("Missing required argument in function call or apply.");
    } else if (num_args < num_pos_args) {
        // push init vals
        for (u32 i = num_args; i < num_pos_args; ++i) {
            push(func->init_vals[i - req_args]);
        }
    } else if (num_args > num_pos_args) {
        if (!has_vl) {
            runtime_error("Too many positional arguments to function.");
        }
        i32 m = num_args - num_pos_args;
        stack->top_to_list(m);
    } else if (has_vl) {
        // in the case where num_pos_args == num_args and there's a variadic
        // list parameter, we need to do this
        push(V_EMPTY);
    }

    // push indicator args
    u32 i;
    // m clamps num_args to the number of positional parameters so we don't push
    // more indicator args than necessary.
    auto m = num_args < num_pos_args ? num_args : num_pos_args;
    for (i = req_args; i < m; ++i) {
        push(V_TRUE);
    }
    for (; i < num_pos_args; ++i) {
        push(V_FALSE);
    }
}

code_address vm_thread::make_call(function* func) {
    auto stub = func->stub;
    auto num_opt = stub->pos_params.size - stub->req_args;
    // stack pointer (equal to number of parameter variables)
    u8 sp = static_cast<u8>(stub->pos_params.size
            + num_opt // for indicators
            + stub->vl_param.has_value());
    if (stub->foreign != nullptr) { // foreign function call
        fn_handle handle{
            .vm=this,
            .stack=stack,
            .func_name=stub->name,
            .origin=chunk->location_of(ip),
            .err=err
        };

        auto ret_place = stack->get_pointer() - sp;
        auto start_args = &(*stack)[stack->get_pointer() - sp];
        stub->foreign(&handle, start_args);
        // can take args off the stack
        stack->do_return(ret_place);

        if(err->happened) {
            status = vs_fault;
        }
        stack->pop_callee();
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

code_address vm_thread::make_tcall(function* func) {
    auto stub = func->stub;
    // update the frame
    auto num_opt = stub->pos_params.size - stub->req_args;
    // stack pointer (equal to number of parameter variables)
    u8 sp = static_cast<u8>(stub->pos_params.size
            + num_opt // for indicators
            + stub->vl_param.has_value());
    frame->num_args = sp;
    frame->caller = func;
    chunk = stub->chunk;

    return stub->addr;
}

// Calling convention: a call uses num_args + 1 elements on the stack. At the
// very top is the callee, followed by num_args positional arguments, ordered so
// the first argument is on the bottom.
code_address vm_thread::call(local_address num_args) {
    // the function to call should be on top
    auto callee = peek();
    if (v_tag(callee) != TAG_FUNC) {
        runtime_error("Error on call: callee is not a function");
        return ip + 2;
    }
    auto func = vfunction(callee);
    stack->push_callee(func);
    pop();

    // put function arguments in place
    arrange_call_stack(func, num_args);

    // make_call() will create the new stack frame
    return make_call(func);
}


code_address vm_thread::tcall(local_address num_args) {
    // the function to call should be on top
    auto callee = peek();
    if (v_tag(callee) != TAG_FUNC) {
        runtime_error("Error on call: callee is not a function");
        return ip + 2;
    }
    auto func = vfunction(callee);
    auto stub = func->stub;
    // foreign calls are just normal calls
    if (stub->foreign != nullptr || frame->caller == nullptr) {
        return call(num_args);
    }

    // we're officially done with the previous function now
    stack->pop_callee();
    stack->push_callee(func);
    pop();

    // set up the call stack
    stack->close_for_tc(num_args, frame->bp);
    arrange_call_stack(func, num_args);

    return make_tcall(func);
}


code_address vm_thread::apply(local_address num_args, bool tail) {
    // all we need to do is expand the varargs. The rest will be taken care of
    // by call().
    auto callee = peek(0);
    auto args = peek(1);

    if (!vis_function(callee)) {
        runtime_error("OP_APPLY first argument not a function.");
    } else if (!vis_cons(args) && args != V_EMPTY) {
        runtime_error("OP_APPLY last argument not a list.");
    }

    auto func = vfunction(callee);
    stack->push_callee(func);
    pop();

    u32 list_len = 0;
    for (auto it = args; it != V_EMPTY; it = vtail(it)) {
        push(vtail(it));
        set_from_top(1, vhead(it));
        ++list_len;
    }

    pop();
    arrange_call_stack(func, num_args + list_len);
    return tail ? make_tcall(func) : make_call(func);
    //return tail ? tcall(num_args+list_len) : call(num_args+list_len);
}

void vm_thread::step() {

    u8 instr = chunk->read_byte(ip);

    // variable for use inside the switch
    value v1, v2, v3;

    bool jump = false;
    code_address addr = 0;

    local_address num_args;

    local_address l;
    u16 id;

    call_frame *tmp;

    upvalue_cell* u;

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
        push(stack->peek(chunk->read_byte(ip+1)));
        ++ip;
        break;
    case OP_LOCAL:
        push(local(chunk->read_byte(ip+1)));
        ++ip;
        break;
    case OP_SET_LOCAL:
        set_local(chunk->read_byte(ip+1), peek(0));
        pop();
        ++ip;
        break;

    case OP_UPVALUE:
        l = chunk->read_byte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        if (u->closed) {
            push(u->closed_value);
        } else{
            if (frame->prev == nullptr) {
                runtime_error("op-upvalue in toplevel frame.");
            }
            auto pos = u->pos;
            push(stack->peek_bottom(pos));
        }
        ++ip;
        break;
    case OP_SET_UPVALUE:
        l = chunk->read_byte(ip+1);
        // TODO: check upvalue exists
        u = frame->upvals[l];
        if (u->closed) {
            u->closed_value = stack->peek();
        } else {
            auto pos = frame->caller->stub->upvals[l];
            stack->set(pos, stack->peek());
        }
        stack->pop();
        ++ip;
        break;

    case OP_CLOSURE:
        id = chunk->read_short(ip+1);
        v1 = stack->create_function(chunk->get_function(id), frame->bp);

        ip += 2;
        break;

    case OP_CLOSE:
        num_args = chunk->read_byte(ip+1);
        stack->close(stack->get_pointer() - num_args);
        // TODO: check stack size >= num_args
        ++ip;
        break;

    case OP_GLOBAL:
        v1 = stack->peek();
        if (v_tag(v1) != TAG_SYM) {
            runtime_error("OP_GLOBAL name operand is not a symbol.");
        }
        v2 = get_global(v1);
        pop();
        push(v2);
        break;
    case OP_SET_GLOBAL:
        v1 = stack->peek(); // value
        v2 = stack->peek(1); // name
        if (v_tag(v2) != TAG_SYM) {
            runtime_error("op-set-global name operand is not a symbol.");
        }
        add_global(v2, v1);
        pop_times(2);
        break;
    case OP_BY_GUID:
        v1 = by_guid(stack->peek());
        pop();
        push(v1);
        break;
    case OP_MACRO:
        v1 = stack->peek();
        if (v_tag(v1) != TAG_SYM) {
            runtime_error("OP_MACRO name operand is not a symbol.");
        }
        v2 = get_macro(v1);
        stack->pop();
        stack->push(v2);
        break;
    case OP_SET_MACRO:
        v1 = stack->peek();
        v2 = stack->peek(1);
        if (v_tag(v2) != TAG_SYM) {
            runtime_error("op-set-macro name operand is not a symbol.");
        } else if (v_tag(v1) != TAG_FUNC) {
            runtime_error("op-set-macro value is not a function.");
        }
        add_macro(v2, v1);
        stack->pop_times(2);
        break;

    case OP_CONST:
        id = chunk->read_short(ip+1);
        if (id >= chunk->constant_arr.size) {
            runtime_error("Attempt to access nonexistent constant.");
        }
        push(chunk->get_constant(id));
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
        v1 = peek(0);
        // object
        v2 = peek(1);
        if (v_tag(v2) == TAG_TABLE) {
            if (vtable(v2)->contents.has_key(v1)) {
                pop();
                pop();
                push(*vtable(v2)->contents.get(v1));
            } else {
                pop();
                pop();
                push(V_NIL);
            }
            break;
        } 
        runtime_error("OP_OBJ_GET operand not a table.");
        break;

    case OP_OBJ_SET:
        // new-value
        v3 = stack->peek(0);
        // key
        v1 = stack->peek(1);
        // object
        v2 = stack->peek(2);
        if (v_tag(v2) != TAG_TABLE) {
            runtime_error("OP_OBJ_SET operand not a table.");
        }
        vtable(v2)->contents.insert(v1,v3);
        stack->pop_times(3);
        break;

    case OP_IMPORT:
        do_import();
        break;

    case OP_JUMP:
        jump = true;
        addr = ip + 3 + static_cast<i16>(chunk->read_short(ip+1));
        break;

    case OP_CJUMP:
        // jump on false
        if (!vtruth(stack->peek(0))) {
            jump = true;
            addr = ip + 3 + static_cast<i16>(chunk->read_short(ip+1));
        } else {
            ip += 2;
        }
        stack->pop();
        break;

    case OP_CALL:
        num_args = chunk->read_byte(ip+1);
        jump = true;
        addr = call(num_args);
        break;

    case OP_TCALL:
        num_args = chunk->read_byte(ip+1);
        jump = true;
        addr = tcall(num_args);
        break;

    case OP_APPLY:
        num_args = chunk->read_byte(ip+1);
        jump = true;
        addr = apply(num_args, false);
        break;

    case OP_TAPPLY:
        num_args = chunk->read_byte(ip+1);
        jump = true;
        addr = apply(num_args, true);
        break;

    case OP_RETURN:
        // check that we are in a call frame
        if (frame->caller == nullptr) {
            runtime_error("Return instruction at top level.");
        }

        // jump to return address
        jump = true;
        addr = frame->ret_addr;
        chunk = frame->ret_chunk;

        num_args = frame->num_args;
        // do the return manipulation on the stack
        stack->do_return(frame->bp);
        stack->pop_callee();
        tmp = frame;
        // TODO: restore stack pointer
        frame = tmp->prev;
        delete tmp;

        break;

    case OP_TABLE:
        stack->push_table();
        break;

    default:
        runtime_error("Unrecognized opcode.");
        break;
    }
    ++ip;

    if (jump) {
        ip = addr;
    }
}

void vm_thread::execute(fault* err) {
    this->err = err;
    if (status == vs_waiting_for_import) {
        auto x = globals->get_ns(pending_import_id);
        if (!x.has_value()) {
            set_fault(err, chunk->location_of(ip), "vm",
                    "Import failed (no namespace created).");
            return;
        }
        string prefix, stem;
        ns_id_destruct((*symtab)[pending_import_id], &prefix, &stem);
        // FIXME: maybe check for overwrites?
        copy_defs(*symtab, **globals->get_ns(chunk->ns_id), **x, stem + ":");
    }
    status = vs_running;
    try {
        while (status == vs_running) {
            if (ip >= chunk->code.size) {
                status = vs_stopped;
                break;
            }
            step();
        }
    } catch (const runtime_exception& ex) {
        // fault is already set
        status = vs_fault;
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
    case OP_BY_GUID:
        out << "by-guid";
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
    case OP_TAPPLY:
        out << "tapply " << (i32)((code.read_byte(ip+1)));;
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

