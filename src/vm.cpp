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
    , toplevel_chunk{use_chunk}
    , ip{0}
    , frame{new call_frame(nullptr, 0, 0, nullptr)}
    , lp{V_NULL} {
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
    return lp;
}

void vm_thread::add_global(value name, value v) {
    if (!name.is_symbol()) {
        runtime_error("Global name is not a symbol.");
    }
    auto ns_id = cur_chunk()->get_ns_id();
    auto ns = *globals->get_ns(ns_id);
    ns->set(v_usym_id(name), v);
}

value vm_thread::get_global(value name) {
    if (!name.is_symbol()) {
        runtime_error("Global name is not a symbol.");
    }

    auto ns_id = cur_chunk()->get_ns_id();
    auto ns = *globals->get_ns(ns_id);
    auto res = ns->get(v_usym_id(name));

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
        return toplevel_chunk;
    }
}
code_chunk* vm_thread::get_toplevel_chunk() {
    return toplevel_chunk;
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

value vm_thread::pop() {
    if (frame->bp >= stack->get_pointer()) {
        runtime_error("pop on empty call frame");
    }
    return stack->pop();
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

code_address vm_thread::apply(working_set& use_ws, local_address num_args) {
    // last argument is table, second to last is list
    auto arg_tab = pop();
    auto arg_list = pop();

    auto tag = v_tag(arg_list);
    if (tag != TAG_EMPTY && tag != TAG_CONS) {
        runtime_error("2nd-to-last argument to apply must be a list.");
    }
    tag = v_tag(arg_tab);
    if (tag != TAG_TABLE) {
        runtime_error("Last argument to apply must be a table.");
    }

    forward_list<value> stack_vals;
    for (u32 i = 0; i < num_args; ++i) {
        stack_vals.push_front(pop());
    }

    // now the function is at the top of the stack

    // push arguments
    push(arg_tab);
    for (auto v : stack_vals) {
        push(v);
    }
    // count variadic arguments
    u32 vlen = 0;
    auto tl = arg_list;
    while (tl.is_cons()) {
        push(v_uhead(tl));
        tl = v_utail(tl);
        ++vlen;
    }
 
    // TODO: use a maxargs constant
    if (vlen + num_args > 255) {
        runtime_error("Too many arguments for function call in apply.");
    }

    return call(use_ws, static_cast<local_address>(vlen + num_args));
}

// Calling convention: a call uses num_args + 2 elements on the stack. At the
// very bottom we expect the callee, followed by num_args positional arguments,
// and finally a table containing keyword arguments.
code_address vm_thread::call(working_set& use_ws, local_address num_args) {
    // the function to call should be at the bottom
    auto callee = peek(num_args + 1);
    auto kw_tab = use_ws.pin_value(pop()); // keyword arguments
    if (!kw_tab.is_table()) {
        runtime_error("Error on call instruction: malformed keyword table.");
    }
    if (v_tag(callee) != TAG_FUNC) {
        runtime_error("Error on call instruction: callee is not a function");
        return ip + 2;
    }

    auto func = callee.ufunction();
    auto stub = func->stub;
    if (stub->foreign_func) {
        // TODO: foreign function
        runtime_error("Error on call instruction: foreign functions not supported.");
        return ip + 2;
    }

    // native function call

    auto num_pos = stub->pos_params.size();

    // For the most part, positional arguments can get left where they are.
    // Extra positional arguments go to the variadic list parameter
    value vlist = V_EMPTY;
    if (num_pos < num_args) {
        if (!stub->vl_param.has_value()) {
            runtime_error("Too many positional arguments to function.");
        }
        i32 m = num_args - num_pos;
        for (auto i = 0; i < m; ++i) {
            // this builds the list in the correct order
            vlist = use_ws.add_cons(pop(), vlist);
        }
    }

    // validate keyword table
    value vtable = use_ws.add_table();
    auto& kw = kw_tab.utable()->contents;
    table<symbol_id,value> pos; // holds additional positional parameters
    for (auto k : kw.keys()) {
        auto id = v_usym_id(k);
        // first, check if this is a positional argument we still need
        bool found_pos = false;
        for (auto i = num_args; i < num_pos; ++i) {
            if(stub->pos_params[i] == id) {
                pos.insert(id, *kw.get(k));
                found_pos = true;
                break;
            }
        }
        if (found_pos) {
            break;
        }

        if (!stub->vt_param.has_value()) {
            // if there's no variadic table argument, we have an error here
            runtime_error("Unrecognized keyword argument in call.");
        } else {
            vtable.utable()->contents.insert(k, *kw.get(k));
        }
    }

    // finish placing positional parameters on the stack
    for (u32 i = num_args; i < num_pos; ++i) {
        auto v = pos.get(stub->pos_params[i]);
        if (v.has_value()) {
            push(*v);
        } else if (i >= stub->req_args) {
            push(callee.ufunction()->init_vals[i-stub->req_args]);
        } else {
            runtime_error("Missing non-optional argument.");
        }
    }

    // push variadic list and table parameters 
    if (stub->vl_param.has_value()) {
        push(vlist);
    }
    if (stub->vt_param.has_value()) {
        push(vtable);
    }

    // extend the call frame
    // stack pointer for the new frame
    u8 sp = static_cast<u8>(num_pos
            + stub->vl_param.has_value()
            + stub->vt_param.has_value());
    u32 bp = stack->get_pointer() - sp;

    frame = new call_frame{frame, ip+2, bp, func, (u8)stub->pos_params.size()};
    return stub->addr;
}


void vm_thread::init_function(function* f) {
    auto stub = f->stub;

    // Add init values
    // DANGER! Init vals are popped right off the stack, so they better be there
    // when this function gets initialized!
    for (auto i = stub->req_args; i < stub->pos_params.size(); ++i) {
        f->init_vals[i] = stack->pop();
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
        lp = pop();
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
        v1 = pop();
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
            auto pos = frame->caller->stub->upvals[l] + frame->bp;
            push(stack->peek_bottom(pos));
        }
        ++ip;
        break;
    case OP_SET_UPVALUE:
        l = cur_chunk()->read_byte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        if (u->closed) {
            u->closed_value = pop();
        } else {
            auto pos = frame->caller->stub->upvals[l] + frame->bp;
            stack->set(pos, pop());
        }
        ++ip;
        break;

    case OP_CLOSURE:
        id = cur_chunk()->read_short(ip+1);
        stub = cur_chunk()->get_function(cur_chunk()->read_short(ip+1));
        v1 = ws.add_function(stub);
        init_function(v1.ufunction());
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
        v1 = pop();
        if (v_tag(v1) != TAG_SYM) {
            runtime_error("OP_GLOBAL name operand is not a symbol.");
        }
        push(get_global(v1));
        break;
    case OP_SET_GLOBAL:
        v1 = pop(); // value
        // FIXME: potential optimization: make the second pop() here a peek()
        // instead and leave the symbol on the stack
        v2 = pop(); // name
        if (v_tag(v2) != TAG_SYM) {
            runtime_error("OP_SET_GLOBAL name operand is not a symbol.");
        }
        add_global(v2, v1);
        break;

    case OP_CONST:
        id = cur_chunk()->read_short(ip+1);
        if (id >= cur_chunk()->num_consts()) {
            runtime_error("attempt to access nonexistent constant.");
        }
        push(cur_chunk()->get_const(id));
        ip += 2;
        break;

    case OP_NULL:
        push(V_NULL);
        break;
    case OP_FALSE:
        push(V_FALSE);
        break;
    case OP_TRUE:
        push(V_TRUE);
        break;

    case OP_OBJ_GET:
        // key
        v1 = pop();
        // object
        v2 = pop();
        if (v_tag(v2) == TAG_TABLE) {
            push(v_utab_get(v2, v1));
            break;
        } 
        runtime_error("obj-get operand not a table");
        break;

    case OP_OBJ_SET:
        // new-value
        v3 = pop();
        // key
        v1 = pop();
        // object
        v2 = pop();
        if (v_tag(v2) != TAG_TABLE) {
            runtime_error("obj-set operand not a table");
        }
        v2.utable()->contents.insert(v1,v3);
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
        if (!v_truthy(pop())) {
            jump = true;
            addr = ip + 3 + static_cast<i16>(cur_chunk()->read_short(ip+1));
        } else {
            ip += 2;
        }
        break;

    case OP_CALL:
        num_args = cur_chunk()->read_byte(ip+1);
        jump = true;
        addr = call(ws, num_args);
        break;

    case OP_APPLY:
        num_args = cur_chunk()->read_byte(ip+1);
        jump = true;
        addr = apply(ws, num_args);
        break;

    case OP_RETURN:
        // check that we are in a call frame
        if (frame->caller == nullptr) {
            runtime_error("return instruction at top level.");
        }
        // get return value
        v1 = ws.pin_value(pop());

        // jump to return address
        jump = true;
        addr = frame->ret_addr;

        num_args = frame->num_args;
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
        if (ip >= cur_chunk()->size()) {
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
    case OP_NULL:
        out << "null";
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
    while (ip < code.size()) {
        u8 instr = code.read_byte(ip);
        // write line
        out << std::setw(6) << ip << "  ";
        disassemble_instr(code, ip, out);

        // additional information
        if (instr == OP_CONST) {
            // write constant value
            out << " ; "
                << v_to_string(code.get_const(code.read_short(ip+1)),
                        &symtab);
        } else if (instr == OP_CLOSURE) {
            out << " ; addr = " << code.get_function(code.read_short(ip+1))->addr;
        }

        out << "\n";
        ip += instr_width(instr);
    }
}

}

