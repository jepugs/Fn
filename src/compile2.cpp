#include "compile2.hpp"

namespace fn {

static void add_upvalue(bc_compiler_output& bco, u8 index, u8 direct) {
    bco.upvals.push_back(index);
    bco.upvals_direct.push_back(direct);
    ++bco.num_upvals;
}

static bool is_legal_local_name(const string& str) {
    // name cannot be empty, begin with a hash or contain a colon
    if (str.empty()) {
        return false;
    }
    if (str[0] == '#') {
        return false;
    }
    for (auto c : str) {
        if (c == ':') {
            return false;
        }
    }
    return true;
}

bc_compiler::bc_compiler(bc_compiler* parent, istate* S,
        scanner_string_table& sst, bc_compiler_output& output)
    : parent{parent}
    , S{S}
    , sst{&sst}
    , sp{0}
    , output{&output} {
    output.sst = &sst;
    output.stack_required = 0;
    output.num_opt = 0;
    output.has_vari = false;
    output.num_upvals = 0;
}

void bc_compiler::pop_vars(u8 sp) {
    while (vars.size > 0) {
        if (vars[0].index <= sp) {
            break;
        }
        --vars.size;
    }
}

void bc_compiler::push_var(sst_id name) {
    vars.push_back(lexical_var{name, sp});
}

bool bc_compiler::find_local_var(u8& index, sst_id name) {
    // search local vars first
    for (auto x : vars) {
        if (x.name == name) {
            index = x.index;
            return true;
        }
    }
    return false;
}

bool bc_compiler::find_upvalue_var(u8& index, sst_id name) {
    auto x = upvals.get(name);
    if (x.has_value()) {
        index = x->index;
        return true;
    }

    // look in the enclosing function
    if (parent) {
        u8 upval_index;
        if (parent->find_local_var(upval_index, name)) {
            // TODO:
        }
    }

    return false;
}

void bc_compiler::emit8(u8 u) {
    output->code.push_back(u);
}

void bc_compiler:: emit16(u16 u) {
    output->code.push_back(0);
    output->code.push_back(0);
    *(u16*)&output->code[output->code.size - 2] = u;
}

void bc_compiler:: emit32(u32 u) {
    output->code.push_back(0);
    output->code.push_back(0);
    output->code.push_back(0);
    output->code.push_back(0);
    *(u32*)&output->code[output->code.size - 4] = u;
}

bool bc_compiler::process_params(const ast::node* params,
        dyn_array<sst_id>& pos_params, dyn_array<ast::node*>& init_vals,
        bool& has_vari, sst_id& vari) {
    if (params->kind != ast::ak_list) {
        compile_error(params->loc, "Malformed parameter list.");
        return false;
    }

    // part one: before any optional or variadic args
    has_vari = false;
    u32 i = 0;
    for (i = 0; i < params->list_length; ++i) {
        auto x = params->datum.list[i];
        if (x->kind != ast::ak_symbol) {
            break;
        }
        if (scanner_name(*sst, x->datum.str_id) == "&") {
            has_vari = true;
            break;
        }
        pos_params.push_back(x->datum.str_id);
    }

    // part two: optional args
    if (!has_vari) {
        for (; i < params->list_length; ++i) {
            auto x = params->datum.list[i];
            if (x->kind != ast::ak_list) {
                if (x->kind == ast::ak_symbol
                        && scanner_name(*sst, x->datum.str_id) == "&") {
                    has_vari = true;
                    break;
                }
                compile_error(x->loc, "Malformed parameter list.");
                return false;
            }
            if (x->list_length != 2
                    || x->datum.list[0]->kind != ast::ak_symbol) {
                compile_error(x->loc, "Malformed optional parameter.");
                return false;
            }
            pos_params.push_back(x->datum.list[0]->datum.str_id);
            init_vals.push_back(x->datum.list[1]);
        }
    }

    // part three: variadic argument
    if (has_vari) {
        if (i + 2 != params->list_length) {
            compile_error(params->datum.list[i]->loc,
                    "Missing variadic parameter name.");
            return false;
        }
        auto x = params->datum.list[i + 1];
        if (x->kind != ast::ak_symbol) {
            compile_error(x->loc, "Malformed variadic parameter.");
            return false;
        }
        vari = x->datum.str_id;
    }
    return true;
}

bool bc_compiler::compile_def(const ast::node* ast) {
    // validate the def form
    if (ast->list_length != 3) {
        compile_error(ast->loc, "def requires exactly 2 arguments.");
        return false;
    }
    auto name = ast->datum.list[1];
    if (name->kind != ast::ak_symbol) {
        compile_error(name->loc, "def name must be a symbol.");
        return false;
    }
    auto val = ast->datum.list[2];
    emit8(OP_SET_GLOBAL);
    output->globals.push_back(bc_output_global{
                .raw_name = name->datum.str_id,
                .patch_addr = output->code.size
            });
    // placeholder for the global ID which will be patched by the interpreter.
    emit32(0);
    return true;
}

// bool bc_compiler::compile_fn(const ast::node* ast) {
//     // validate the arity of the fn form
//     if (ast->list_length < 2) {
//         compile_error(ast->loc, "fn requires a parameter list.");
//         return false;
//     }

//     dyn_array<sst_id> pos_params;
//     dyn_array<ast::node*> init_vals;
//     bool has_vari;
//     sst_id vari;
//     if (!process_params(ast->datum.list[1], pos_params, init_vals,
//                     has_vari, vari)) {
//         return false;
//     }
//     // compile the child function
//     bc_compiler_output child_out;
//     bc_compiler child{child_out, this, *sst};

//     // set up arguments as local variables
//     for (auto p : pos_params) {
//         child.push_var(p);
//         ++child.sp;
//     }
//     if (has_vari) {
//         child.push_var(vari);
//         ++child.sp;
//     }
//     // if (!child.compile_body(&ast->datum.list[2], ast->list_length - 2)) {
//     //     return false;
//     // }

//     // code to create the function object
//     auto save_sp = sp;
//     for (auto x : init_vals) {
//         if (!compile(x, false)) {
//             return false;
//         }
//     }
//     auto child_id = output->sub_funs.size;
//     output->sub_funs.push_back(*child.output);
//     emit8(OP_CLOSURE);
//     emit16(child_id);
//     sp = save_sp + 1;
//     return true;
// }

bool bc_compiler::compile_number(const ast::node* root) {
    // FIXME: technically we should check for constant ID overflow here
    auto cid = output->const_table.size;
    output->const_table.push_back(bc_output_const{bck_number, root->datum.num});
    emit8(OP_CONST);
    emit16(cid);
    return true;
}

bool bc_compiler::compile_string(const ast::node* root) {
    // FIXME: technically we should check for constant ID overflow here
    auto cid = output->const_table.size;
    bc_output_const k;
    k.kind = bck_string;
    k.d.str_id = root->datum.str_id;
    output->const_table.push_back(k);
    emit8(OP_CONST);
    emit16(cid);
    return true;
}

bool bc_compiler::compile(const ast::node* root, bool tail) {
    switch (root->kind) {
    case ast::ak_number:
        return compile_number(root);
    case ast::ak_string:
        return compile_string(root);
    default:
        compile_error(root->loc, "Unsupported form sorry\n");
        return false;
    }
    return true;
}

bool bc_compiler::compile_toplevel(const ast::node* root) {
    if (!compile(root, true)) {
        return false;
    }
    emit8(OP_RETURN);
    return true;
}

void bc_compiler::compile_error(const source_loc& loc, const string& message) {
    std::ostringstream os;
    os << "Line " << loc.line << ", col " << loc.col << ":\n  " << message;
    set_error(S->err, message);
}

bool compile_to_bytecode(bc_compiler_output& out, istate* S,
        scanner_string_table& sst, const ast::node* root) {
    bc_compiler c{nullptr, S, sst, out};
    return c.compile_toplevel(root);
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
    for (u32 i = 0; i < stub->code_length; i += instr_width(stub->code[i])) {
        disassemble_instr(&stub->code[i], os);
        if (stub->code[i] == OP_CONST) {
            auto id = read_short(&stub->code[i+1]);
            auto val = stub->const_arr[id];
            os << "    " << "; " << v_to_string(val, S->symtab, true);
        } else if (stub->code[i] == OP_GLOBAL) {
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
            for (u32 i = 0; i < stub->num_sub_funs; ++i) {
                disassemble_with_header(os, S, stub->sub_funs[i],
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
