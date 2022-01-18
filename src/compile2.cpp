#include "compile2.hpp"

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
    ft->stub->code[where] = data[1];
}

void compiler::patch_jump(u32 jmp_addr, u32 dest) {
    i64 offset = dest - jmp_addr;
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
                .index = l->index,
                .next = uv_head
            };
            uv_head = u;
        }
        u = parent->lookup_upval(sid);
    }
    return u;
}

void compile_form(istate* S, ast_form* ast) {
    push_empty_fun(S);
    auto ft = init_function_tree(S, vfunction(peek(S))->stub);
    expand(S, ft, ast);
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
    compile_llir(ft->body);
    write_byte(OP_RETURN);
    for (auto sub : ft->sub_funs) {
        compiler c{S, sub, this};
        c.compile();
    }
}

void compiler::update_hwm(u32 local_hwm) {
    if (local_hwm > sp_hwm) {
        sp_hwm = local_hwm;
    }
}

void compiler::compile_llir(llir_form* form) {
    switch (form->tag) {
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
        compile_llir(x->then);
        --sp;

        auto end_then = ft->stub->code.size;
        write_byte(OP_JUMP);
        write_short(0);

        patch_jump(start, ft->stub->code.size);
        compile_llir(x->elce);
        patch_jump(end_then, ft->stub->code.size);
    }
        break;
    default:
        break;
    }
}

void compiler::compile_var(istate* S, llir_var* form) {
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
    case OP_METHOD:
        out << "method";
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

void disassemble_top(istate* S, bool recur) {
    std::ostringstream os;
    auto stub = vfunction(peek(S))->stub;
    for (u32 i = 0; i < stub->code.size; i += instr_width(stub->code[i])) {
        disassemble_instr(&stub->code[i], os);
        if (stub->code[i] == OP_CONST) {
            auto val = stub->const_arr[read_short(&stub->code[i+1])];
            os << "    " << "; " << v_to_string(val, S->symtab, true);
        }
        os << '\n';
    }
    push_string(S, os.str());
}

}
