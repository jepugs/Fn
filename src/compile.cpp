#include "allocator.hpp"
#include "compile.hpp"

#include "vm.hpp"

namespace fn {

// TODO: add source location here
void compiler::compile_error(const string& msg) {
    ierror(S, msg);
    std::cout << msg << '\n';
    throw compile_exception{};
}

void compiler::write_byte(u8 u) {
    push_back_code(S, ft->stub, u);
}

void compiler::write_short(u16 u) {
    u8* data = (u8*)&u;
    write_byte(data[0]);
    write_byte(data[1]);
}

void compiler::write_u32(u32 u) {
    u8* data = (u8*)&u;
    write_byte(data[0]);
    write_byte(data[1]);
    write_byte(data[2]);
    write_byte(data[3]);
}

u8 compiler::read_byte(u32 where) {
    return handle_stub(ft->stub)->code.data->data[where];
}

u32 compiler::read_u32(u32 where) {
    return *((u32*) &handle_stub(ft->stub)->code.data->data[where]);
}

void compiler::patch_short(u16 u, u32 where) {
    u8* data = (u8*)&u;
    handle_stub(ft->stub)->code.data->data[where] = data[0];
    handle_stub(ft->stub)->code.data->data[where+1] = data[1];
}

void compiler::patch_jump(u32 jmp_addr, u32 dest) {
    i64 offset = dest - jmp_addr - 3;
    // FIXME: check distance fits in 16 bits
    patch_short((i16)offset, jmp_addr+1);
}

u32 compiler::get_global_id(symbol_id fqn) {
    auto x = S->G->def_tab.get2(fqn);
    if (x) {
        return x->val;
    }
    S->G->def_arr.push_back(V_UNIN);
    S->G->def_ids.push_back(fqn);
    auto id = S->G->def_arr.size - 1;
    S->G->def_tab.insert(fqn, id);
    return id;
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
                .index = (u8)(uv_head ? 1 + uv_head->index : 0),
                .next = uv_head
            };
            uv_head = u;
            // add to the function stub
            push_back_upval(S, ft->stub, true, l->index);
        }
        auto v = parent->lookup_upval(sid);
        if (v) {
            u = new local_upvalue{
                .name = sid,
                .direct = false,
                .index = (u8)(uv_head ? 1 + uv_head->index : 0),
                .next = uv_head
            };
            uv_head = u;
            push_back_upval(S, ft->stub, false, v->index);
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
    // push indicator params
    u32 i = ft->params.size - handle_stub(ft->stub)->num_opt;
    for (; i < ft->params.size; ++i) {
        auto str = "?" + (*S->symtab)[ft->params[i]];
        auto l = new lexical_var {
            .name=intern(S, str),
            .index=(u8)sp++,
            .is_upvalue=false,
            .next=var_head
        };
        var_head = l;
    }
    compile_llir(ft->body, true);
    write_byte(OP_RETURN);
}

void compiler::update_hwm(u32 local_hwm) {
    if (local_hwm > sp_hwm) {
        sp_hwm = local_hwm;
    }
}

void compiler::compile_llir(llir_form* form, bool tail) {
    update_code_info(S, handle_stub(ft->stub), form->origin);
    switch (form->tag) {
    case lt_apply:
        compile_apply((llir_apply*)form, tail);
        break;
    case lt_call:
        compile_call((llir_call*)form, tail);
        break;
    case lt_def:
        compile_def((llir_def*)form);
        break;
    case lt_defmacro:
        compile_defmacro((llir_defmacro*)form);
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

        auto start = handle_stub(ft->stub)->code.size;
        write_byte(OP_CJUMP);
        write_short(0);
        --sp;
        compile_llir(x->then, tail);
        --sp;

        auto end_then = handle_stub(ft->stub)->code.size;
        write_byte(OP_JUMP);
        write_short(0);

        patch_jump(start, handle_stub(ft->stub)->code.size);
        compile_llir(x->elce, tail);
        patch_jump(end_then, handle_stub(ft->stub)->code.size);
    }
        break;
    case lt_import:
        compile_import((llir_import*)form);
        break;
    case lt_fn:
        compile_fn((llir_fn*)form);
        break;
    case lt_set:
        compile_set((llir_set*)form);
        break;
    case lt_var:
        compile_var((llir_var*)form);
        break;
    case lt_with:
        compile_with((llir_with*)form, tail);
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

void compiler::compile_get(llir_call* form) {
    if (form->num_args < 1) {
        compile_error("get requires at least one argument.");
    }
    compile_llir(form->args[0]);
    for (u32 i = 1; i < form->num_args; ++i) {
        compile_llir(form->args[i]);
        write_byte(OP_OBJ_GET);
        --sp;
    }
}

void compiler::compile_call(llir_call* form, bool tail) {
    // TODO: first should check for functions like get, which we can optimize
    auto start_sp = sp;
    if (form->callee->tag == lt_var) {
        auto x = (llir_var*)form->callee;
        auto name = symname(S, x->name);
        if (name == ".") {
            compile_get(form);
            return;
        } else if (name.at(0) == '.') {
            // the above line is ok because there's no way to make an empty
            // symbol name.

            // method call
            compile_sym(intern(S, name.substr(1)));
            for(u32 i = 0; i < form->num_args; ++i) {
                compile_llir(form->args[i]);
            }
            // put code info back after processing arguments
            update_code_info(S, handle_stub(ft->stub), form->header.origin);
            // DEBUG
            write_byte(tail ? OP_TCALLM : OP_CALLM);
            write_byte(form->num_args);
            sp = start_sp + 1;
            return;
        }
    }

    compile_llir(form->callee);
    for(u32 i = 0; i < form->num_args; ++i) {
        compile_llir(form->args[i]);
    }
    // put code info back after processing arguments
    update_code_info(S, handle_stub(ft->stub), form->header.origin);
    // DEBUG
    write_byte(tail ? OP_TCALL : OP_CALL);
    write_byte(form->num_args);
    sp = start_sp + 1;
}

void compiler::compile_apply(llir_apply* form, bool tail) {
    compile_llir(form->callee);
    for (u32 i = 0; i < form->num_args; ++i) {
        compile_llir(form->args[i]);
    }
    write_byte(tail ? OP_TAPPLY : OP_APPLY);
    write_byte(form->num_args - 1);
}

void compiler::compile_def(llir_def* form) {
    compile_llir(form->value);
    write_byte(OP_SET_GLOBAL);
    auto fqn = resolve_sym(S, S->ns_id, form->name);
    write_u32(get_global_id(fqn));
}

void compiler::compile_defmacro(llir_defmacro* form) {
    auto fqn = resolve_sym(S, S->ns_id, form->name);
    compile_llir(form->macro_fun);
    write_byte(OP_SET_MACRO);
    write_short(add_const(S, ft, vbox_symbol(fqn)));
}

void compiler::compile_import(llir_import* form) {
    compile_sym(form->target);
    if (form->has_alias) {
        compile_sym(form->alias);
    } else {
        string prefix;
        string stem;
        ns_id_destruct(symname(S, form->target), &prefix, &stem);
        compile_sym(intern(S, stem));
    }
    write_byte(OP_IMPORT);
    write_byte(OP_NIL);
    sp -= 1;
    
}

void compiler::compile_fn(llir_fn* form) {
    // compile init args
    auto start_sp = sp;
    for (u32 i = 0; i < form->num_opt; ++i) {
        compile_llir(form->inits[i]);
    }
    update_code_info(S, handle_stub(ft->stub), form->header.origin);
    write_byte(OP_CLOSURE);
    write_short(form->fun_id);
    sp = start_sp + 1;
    // compile the sub function stub
    auto sub = ft->sub_funs[form->fun_id];
    compiler c{S, sub, this};
    c.compile();
}

void compiler::compile_set(llir_set* form) {
    if (form->target->tag == lt_var) {
        auto sid = ((llir_var*)(form->target))->name;
        // look for local variable
        auto l = lookup_var(sid);
        if (l) {
            compile_llir(form->value);
            update_code_info(S, handle_stub(ft->stub), form->header.origin);
            write_byte(OP_COPY);
            update_hwm(sp+1);
            write_byte(0);
            write_byte(OP_SET_LOCAL);
            write_byte(l->index);
            return;
        }
        auto u = lookup_upval(sid);
        if (u) {
            compile_llir(form->value);
            update_code_info(S, handle_stub(ft->stub), form->header.origin);
            update_hwm(sp+1);
            write_byte(OP_COPY);
            write_byte(0);
            write_byte(OP_SET_UPVALUE);
            write_byte(u->index);
            return;
        }
        compile_error("set! target symbol does not name a local variable.");
    } else if (form->target->tag == lt_call) {
        // make sure it's a get call
        auto target = (llir_call*)form->target;
        if (target->callee->tag != lt_var
                || ((llir_var*)target->callee)->name != intern(S, ".")
                || target->num_args < 2) {
            compile_error("Malformed set! target.");
        }
        // compile the target object
        compile_llir(target->args[0]);
        // access keys, stopping before the last one
        u32 i;
        for (i = 1; i + 1 < target->num_args; ++i) {
            compile_llir(target->args[i]);
            write_byte(OP_OBJ_GET);
            --sp;
        }
        // use the final key and do the set operation
        compile_llir(target->args[i]);
        compile_llir(form->value);
        write_byte(OP_OBJ_SET);
        sp -= 2;
    } else {
        compile_error("Malformed set! target.");
    }
}

static bool is_fqn(const string& str) {
    return str[0] == '#' && str.find(':') != string::npos;
}

void compiler::compile_var(llir_var* form) {
    update_hwm(++sp);
    // first, identify special constants
    if (form->name == cached_sym(S, SC_NIL)) {
        write_byte(OP_NIL);
    } else if (form->name == cached_sym(S, SC_YES)) {
        write_byte(OP_YES);
    } else if (form->name == cached_sym(S, SC_NO)) {
        write_byte(OP_NO);
    } else {
        if (is_fqn(symname(S,form->name))) {
            auto fqn = intern(S, symname(S, form->name).substr(1));
            write_byte(OP_GLOBAL);
            write_u32(get_global_id(fqn));
            return;
        }
        auto l = lookup_var(form->name);
        if (l != nullptr) {
            write_byte(OP_LOCAL);
            write_byte(l->index);
            return;
        }
        auto u = lookup_upval(form->name);
        if (u != nullptr) {
            write_byte(OP_UPVALUE);
            write_byte(u->index);
            return;
        }
        auto fqn = resolve_sym(S, S->ns_id, form->name);
        if (S->err_happened) {
            return;
        }
        write_byte(OP_GLOBAL);
        write_u32(get_global_id(fqn));
    }
}

void compiler::compile_with(llir_with* form, bool tail) {
    auto old_head = var_head;
    auto old_sp = sp;

    // prepend new vars
    for (u32 i = 0; i < form->num_vars; ++i) {
        // place for var
        write_byte(OP_NIL);
        auto l = new lexical_var {
            .name = form->vars[i],
            .index = (u8)sp++,
            .next = var_head
        };
        var_head = l;
    }
    for (u32 i = 0; i < form->num_vars; ++i) {
        compile_llir(form->values[i]);
        update_code_info(S, handle_stub(ft->stub), form->header.origin);
        write_byte(OP_SET_LOCAL);
        write_byte(old_sp + i);
        --sp;
    }
    if (form->body_length == 0) {
        write_byte(OP_NIL);
        update_hwm(sp);
    } else {
        u32 i;
        for (i = 0; i < form->body_length-1; ++i) {
            compile_llir(form->body[i]);
            write_byte(OP_POP);
            --sp;
        }
        compile_llir(form->body[i], tail);
    }
    update_code_info(S, handle_stub(ft->stub), form->header.origin);

    // in the tail position, this is handled by the subsequent return
    if (!tail) {
        write_byte(OP_CLOSE);
        write_byte(sp - old_sp);
        sp = old_sp + 1;
    }
    // clean up the lexical environment
    while (var_head != old_head) {
        auto tmp = var_head;
        var_head = var_head->next;
        delete tmp;
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
        // TODO: print GLOBAL and SET_GLOBAL IDs
        out << "global ";
        break;
    case OP_SET_GLOBAL:
        out << "set-global ";
        break;
    case OP_CONST:
        out << "const " << read_short(&code_start[1]);
        break;
    case OP_NIL:
        out << "nil";
        break;
    case OP_NO:
        out << "no";
        break;
    case OP_YES:
        out << "yes";
        break;
    case OP_OBJ_GET:
        out << "obj-get";
        break;
    case OP_OBJ_SET:
        out << "obj-set";
        break;
    case OP_MACRO:
        out << "macro " << read_short(&code_start[1]);
        break;
    case OP_SET_MACRO:
        out << "set-macro " << read_short(&code_start[1]);
        break;
    case OP_CALLM:
        out << "callm " << (i32)code_start[1];
        break;
    case OP_TCALLM:
        out << "tcallm " << (i32)code_start[1];
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
    // FIXME: stop accessing gc arrays directly you dumbass
    for (u32 i = 0; i < stub->code.size; i += instr_width(stub->code.data->data[i])) {
        disassemble_instr(&stub->code.data->data[i], os);
        if (stub->code.data->data[i] == OP_CONST) {
            auto id = read_short(&stub->code.data->data[i+1]);
            auto val = gc_array_get(&stub->const_arr, id);
            os << "    " << "; " << v_to_string(val, S->symtab, true);
        } else if (stub->code.data->data[i] == OP_GLOBAL) {
            // TODO: print global value
            // auto id = read_u32(&stub->code[i+1]);
            // os << "    " << "; " << id;
        }
        os << '\n';
    }
}

static void disassemble_with_header(std::ostringstream& os, istate* S,
        function_stub* stub, const string& header, bool recur) {
    os << header << '\n';
    if (stub->foreign) {
        os << "; <foreign_fun>";
        return;
    } else {
        disassemble_stub(os, S, stub);
        if (recur) {
            for (u32 i = 0; i < stub->sub_funs.size; ++i) {
                disassemble_with_header(os, S, gc_array_get(&stub->sub_funs, i),
                        ";" + header + ":" + std::to_string(i), recur);
            }
        }
    }
}

void disassemble_top(istate* S, bool recur) {
    auto stub = vfunction(peek(S))->stub;
    std::ostringstream os;
    disassemble_with_header(os, S, stub, "; function", recur);
    push_string(S, os.str());
}

}
