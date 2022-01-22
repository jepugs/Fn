#include "compile.hpp"

#include "vm.hpp"

namespace fn {

void compiler::compile_error(const string& msg) {
    ierror(S, msg);
    throw compile_exception{};
}

void compiler::write_byte(u8 u) {
    ft->stub->code.push_back(u);
}

void compiler::write_short(u16 u) {
    u8* data = (u8*)&u;
    ft->stub->code.push_back(data[0]);
    ft->stub->code.push_back(data[1]);
}

void compiler::patch_short(u16 u, u32 where) {
    u8* data = (u8*)&u;
    ft->stub->code[where] = data[0];
    ft->stub->code[where+1] = data[1];
}

void compiler::patch_jump(u32 jmp_addr, u32 dest) {
    i64 offset = dest - jmp_addr - 3;
    // FIXME: check distance fits in 16 bits
    patch_short((i16)offset, jmp_addr+1);
}

// look up a lexical variable
lexical_var* compiler::lookup_var(symbol_id sid) {
    auto l = var_head;
    for (; l != nullptr; l = l->next) {
        if (l->name == sid) {
            break;
        }
    }
    return l;
}

// Look up an upvalue
local_upvalue* compiler::lookup_upval(symbol_id sid) {
    auto u = uv_head;
    for (; u != nullptr; u = u->next) {
        if (u->name == sid) {
            return u;
        }
    }
    // upvalue not found. Search enclosing function.
    if (parent) {
        auto l = parent->lookup_var(sid);
        if (l) {
            l->is_upvalue = true;
            u = new local_upvalue{
                .name = sid,
                .direct = true,
                .index = (uv_head ? 1 + uv_head->index : 0),
                .next = uv_head
            };
            uv_head = u;
            // add to the function stub
            ++ft->stub->num_upvals;
            ft->stub->upvals_direct.push_back(true);
            ft->stub->upvals.push_back(bp - parent->bp - l->index);
        }
        auto v = parent->lookup_upval(sid);
        if (v) {
            u = new local_upvalue{
                .name = sid,
                .direct = false,
                .index = (uv_head ? 1 + uv_head->index : 0),
                .next = uv_head
            };
            uv_head = u;
            ++ft->stub->num_upvals;
            ft->stub->upvals_direct.push_back(false);
            ft->stub->upvals.push_back(v->index);
        }
    }
    return u;
}

void compile_form(istate* S, ast_form* ast) {
    push_empty_fun(S);
    auto ft = init_function_tree(S, vfunction(peek(S))->stub);
    expand(S, ft, ast);
    if (S->err_happened) {
        free_function_tree(S, ft);
        pop(S);
        // don't attempt to compile
        return;
    }
    compiler c{S, ft};
    c.compile();
    // no longer need the tree
    free_function_tree(S, ft);
}

compiler::compiler(istate* S, function_tree* ft, compiler* parent, u32 bp)
    : S{S}
    , ft{ft}
    , parent{parent}
    , bp{bp}
    , sp{0}
    , sp_hwm{0}
    , var_head{nullptr}
    , uv_head{nullptr} {
}

compiler::~compiler() {
    auto v = var_head;
    while (v != nullptr) {
        auto tmp = v;
        v = v->next;
        delete tmp;
    }
    auto u = uv_head;
    while (u != nullptr) {
        auto tmp = u;
        u = u->next;
        delete tmp;
    }
}

void compiler::compile() {
    // push parameters as lexical vars
    for (auto sid : ft->params) {
        auto l = new lexical_var {
            .name=sid,
            .index=(u8)sp++,
            .is_upvalue=false,
            .next=var_head
        };
        var_head = l;
    }
    compile_llir(ft->body);
    write_byte(OP_RETURN);
}

void compiler::update_hwm(u32 local_hwm) {
    if (local_hwm > sp_hwm) {
        sp_hwm = local_hwm;
    }
}

void compiler::compile_llir(llir_form* form, bool tail) {
    switch (form->tag) {
    case lt_def:
        compile_def((llir_def*)form);
        break;
    case lt_call:
        compile_call((llir_call*)form, tail);
        break;
    case lt_const:
        update_hwm(sp + 1);
        write_byte(OP_CONST);
        write_short(((llir_const*)form)->id);
        ++sp;
        break;
    case lt_if: {
        auto x = (llir_if*)form;
        compile_llir(x->test);

        auto start = ft->stub->code.size;
        write_byte(OP_CJUMP);
        write_short(0);
        --sp;
        compile_llir(x->then, tail);
        --sp;

        auto end_then = ft->stub->code.size;
        write_byte(OP_JUMP);
        write_short(0);

        patch_jump(start, ft->stub->code.size);
        compile_llir(x->elce, tail);
        patch_jump(end_then, ft->stub->code.size);
    }
        break;
    case lt_fn:
        compile_fn((llir_fn*)form);
        break;
    case lt_var:
        compile_var((llir_var*)form);
        break;
    default:
        break;
    }
}

void compiler::compile_sym(symbol_id sid) {
    write_byte(OP_CONST);
    write_short(add_const(S, ft, vbox_symbol(sid)));
    ++sp;
}

void compiler::compile_def(llir_def* form) {
    compile_sym(form->name);
    compile_llir(form->value);
    write_byte(OP_SET_GLOBAL);
    --sp;
}

void compiler::compile_call(llir_call* form, bool tail) {
    // TODO: first should check for functions like get, which we can optimize
    auto start_sp = sp;
    if (form->callee->tag == lt_dot) {
        // method call
        auto dot = (llir_dot*)form->callee;
        compile_sym(dot->key);
        compile_llir(dot->obj);
        for(u32 i = 0; i < form->num_args; ++i) {
            compile_llir(form->args[i]);
        }
        write_byte(tail ? OP_TCALLM : OP_CALLM);
        write_byte(form->num_args);
    } else {
        compile_llir(form->callee);
        for(u32 i = 0; i < form->num_args; ++i) {
            compile_llir(form->args[i]);
        }
        write_byte(tail ? OP_TCALL : OP_CALL);
        write_byte(form->num_args);
    }
    sp = start_sp + 1;
}

void compiler::compile_var(llir_var* form) {
    // first, identify special constants
    if (form->name == intern(S, "nil")) {
        write_byte(OP_NIL);
        ++sp;
    } else if (form->name == intern(S, "true")) {
        write_byte(OP_TRUE);
        ++sp;
    } else if (form->name == intern(S, "false")) {
        write_byte(OP_FALSE);
        ++sp;
    } else {
        auto l = lookup_var(form->name);
        if (l != nullptr) {
            write_byte(OP_LOCAL);
            write_byte(l->index);
            ++sp;
            return;
        }
        auto u = lookup_upval(form->name);
        if (u != nullptr) {
            write_byte(OP_UPVALUE);
            write_byte(u->index);
            ++sp;
            return;
        }
        compile_sym(form->name);
        write_byte(OP_GLOBAL);
    }
}

void compiler::compile_fn(llir_fn* form) {
    // compile init args
    auto start_sp = sp;
    for (u32 i = 0; i < form->num_opt; ++i) {
        compile_llir(form->inits[i]);
    }
    write_byte(OP_CLOSURE);
    write_short(form->fun_id);
    sp = start_sp+1;
    // compile the sub function stub
    auto sub = ft->sub_funs[form->fun_id];
    if (sub->stub->code.size == 0) {
        compiler c{S, sub, this, start_sp};
        c.compile();
    }
}

static u16 read_short(u8* p) {
    return *((u16*)p);
}

static void disassemble_instr(u8* code_start, std::ostream& out) {
    u8 instr = code_start[0];
    switch (instr) {
    case OP_NOP:
        out << "nop";
        break;
    case OP_POP:
        out << "pop";
        break;
    case OP_LOCAL:
        out << "local " << (i32)code_start[1];
        break;
    case OP_SET_LOCAL:
        out << "set-local " << (i32)code_start[1];
        break;
    case OP_COPY:
        out << "copy " << (i32)code_start[1];
        break;
    case OP_UPVALUE:
        out << "upvalue " << (i32)code_start[1];
        break;
    case OP_SET_UPVALUE:
        out << "set-upvalue " << (i32)code_start[1];
        break;
    case OP_CLOSURE:
        out << "closure " << read_short(&code_start[1]);
        break;
    case OP_CLOSE:
        out << "close " << (i32)code_start[1];
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
        out << "const " << read_short(&code_start[1]);
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
    case OP_CALLM:
        out << "callm";
        break;
    case OP_TCALLM:
        out << "tcallm";
        break;
    case OP_IMPORT:
        out << "import";
        break;
    case OP_JUMP:
        out << "jump " << (i32)(static_cast<i16>(read_short(&code_start[1])));
        break;
    case OP_CJUMP:
        out << "cjump " << (i32)(static_cast<i16>(read_short(&code_start[1])));
        break;
    case OP_CALL:
        out << "call " << (i32)code_start[1];
        break;
    case OP_TCALL:
        out << "tcall " << (i32)code_start[1];
        break;
    case OP_APPLY:
        out << "apply " << (i32)code_start[1];
        break;
    case OP_TAPPLY:
        out << "tapply " << (i32)code_start[1];
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

static void disassemble_stub(std::ostringstream& os, istate* S, function_stub* stub) {
    for (u32 i = 0; i < stub->code.size; i += instr_width(stub->code[i])) {
        disassemble_instr(&stub->code[i], os);
        if (stub->code[i] == OP_CONST) {
            auto val = stub->const_arr[read_short(&stub->code[i+1])];
            os << "    " << "; " << v_to_string(val, S->symtab, true);
        }
        os << '\n';
    }
}

void disassemble_top(istate* S, bool recur) {
    auto stub = vfunction(peek(S))->stub;
    if (stub->foreign) {
        push_string(S, "<foreign_fun>");
        return;
    }
    std::ostringstream os;
    disassemble_stub(os, S, stub);
    if (recur) {
        for (u32 i = 0; i < stub->sub_funs.size; ++i) {
            os << "; subfun " << i << "\n";
            disassemble_stub(os, S, stub->sub_funs[i]);
        }
    }
    push_string(S, os.str());
}

}
