#include "compile2.hpp"

namespace fn {

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
    output.name_id = scanner_intern(sst, "<toplevel>");
    output.stack_required = 0;
    output.num_opt = 0;
    output.has_vari = false;
    output.num_upvals = 0;

    output.ci_arr.push_back(code_info{0, source_loc{0,0}});
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
        index = *x;
        return true;
    }

    // look in the enclosing function
    if (parent) {
        u8 upval_index;
        bool direct = parent->find_local_var(upval_index, name);
        if (direct || parent->find_upvalue_var(upval_index, name)) {
            index = output->num_upvals;
            upvals.insert(name, index);
            output->upvals.push_back(upval_index);
            output->upvals_direct.push_back(direct);
            ++output->num_upvals;
            return true;
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

void bc_compiler::patch16(u16 u, u32 where) {
    *(u16*)&output->code[where] = u;
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

ast::node* bc_compiler::macroexpand(const ast::node* macro_form) {
    if (macro_form->kind != ast::ak_list) {
        return nullptr;
    } else if (macro_form->list_length == 0) {
        return nullptr;
    } else if (macro_form->datum.list[0]->kind != ast::ak_symbol) {
        return nullptr;
    }

    auto name = scanner_name(*sst, macro_form->datum.list[0]->datum.str_id);
    auto fqn = resolve_sym(S, S->ns_id, intern(S, name));
    if (push_macro(S, fqn)) {
        for (u32 i = 1; i < macro_form->list_length; ++i) {
            push_quoted(S, *sst, macro_form->datum.list[i]);
        }
        call(S, macro_form->list_length - 1);
        if (has_error(S)) {
            return nullptr;
        }
    } else {
        return nullptr;
    }
    ast::node* form;
    pop_syntax(form, S, *sst);
    while (true) {
        if (form->kind != ast::ak_list) {
            break;
        } else if (form->list_length == 0) {
            break;
        } else if (form->datum.list[0]->kind != ast::ak_symbol) {
            break;
        }
        name = scanner_name(*sst, form->datum.list[0]->datum.str_id);
        fqn = resolve_sym(S, S->ns_id, intern(S, name));
        if (push_macro(S, fqn)) {
            for (u32 i = 1; i < form->list_length; ++i) {
                push_quoted(S, *sst, form->datum.list[i]);
            }
            call(S, form->list_length - 1);
            ast::free_graph(form);
            if (has_error(S)) {
                return nullptr;
            }
            pop_syntax(form, S, *sst);
        } else {
            break;
        }
    }

    return form;
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
    if (!compile(val, false)) {
        return false;
    }
    emit8(OP_SET_GLOBAL);
    output->globals.push_back(bc_output_global{
                .raw_name = name->datum.str_id,
                .patch_addr = output->code.size
            });
    // placeholder for the global ID which will be patched by the interpreter.
    emit32(0);
    --sp;
    return true;
}

bool bc_compiler::compile_sub_fun(const ast::node* params,
        ast::node* const* body, u32 body_len, const string& name) {
    dyn_array<sst_id> pos_params;
    dyn_array<ast::node*> init_vals;
    bool has_vari;
    sst_id vari;
    if (!process_params(params, pos_params, init_vals,
                    has_vari, vari)) {
        return false;
    }
    // compile the child function
    bc_compiler_output child_out;
    bc_compiler child{this, S, *sst, child_out};
    // FIXME: incorporate function name into compiler output

    child_out.name_id = scanner_intern(*sst, name);
    // set up arguments as local variables
    for (auto p : pos_params) {
        child_out.params.push_back(p);
        child.push_var(p);
        ++child.sp;
    }
    if (has_vari) {
        child_out.has_vari = true;
        child_out.vari_param = vari;
        child.push_var(vari);
        ++child.sp;
    }
    if (!child.compile_function_body(body, body_len)) {
        return false;
    }

    // code to create the function object
    auto save_sp = sp;
    for (auto x : init_vals) {
        if (!compile(x, false)) {
            return false;
        }
    }
    auto child_id = output->sub_funs.size;
    output->sub_funs.push_back(child_out);
    emit8(OP_CLOSURE);
    emit16(child_id);
    sp = save_sp + 1;
    return true;
}

bool bc_compiler::compile_defmacro(const ast::node* ast) {
    if (ast->list_length < 3) {
        compile_error(ast->loc, "defmacro requires a name and parameter list.");
        return false;
    }
    if (ast->datum.list[1]->kind != ast::ak_symbol) {
        compile_error(ast->loc, "defmacro name must be a symbol.");
        return false;
    }
    auto name_id = ast->datum.list[1]->datum.str_id;
    // resolve the full symbol name
    auto sid = intern(S, scanner_name(*sst, name_id));
    auto fqn = resolve_sym(S, S->ns_id, sid);
    // TODO: check name legality
    if (!compile_sub_fun(ast->datum.list[2], &ast->datum.list[3],
                    ast->list_length - 3, "#macro:" + symname(S, fqn))) {
        return false;
    }

    bc_output_const k;
    k.kind = bck_symbol;
    k.d.str_id = scanner_intern(*sst, symname(S, fqn));
    auto cid = output->const_table.size;
    output->const_table.push_back(k);
    emit8(OP_SET_MACRO);
    emit16(cid);

    // defmacro returns nil
    emit8(OP_NIL);

    return true;
}

bool bc_compiler::compile_do(const ast::node* ast, bool tail) {
    return compile_body(&ast->datum.list[1], ast->list_length-1, tail);
}

bool bc_compiler::compile_if(const ast::node* ast, bool tail) {
    // validate the if form
    if (ast->list_length != 4) {
        compile_error(ast->loc, "if requires exactly 3 arguments.");
        return false;
    }
    if (!compile(ast->datum.list[1], false)) {
        return false;
    }
    emit8(OP_CJUMP);
    auto patch_addr1 = output->code.size;
    emit16(0);
    --sp;

    if (!compile(ast->datum.list[2], tail)) {
        return false;
    }
    emit8(OP_JUMP);
    auto patch_addr2 = output->code.size;
    emit16(0);

    patch16(output->code.size - patch_addr1 - 2, patch_addr1);
    --sp;
    if (!compile(ast->datum.list[3], tail)) {
        return false;
    }

    patch16(output->code.size - patch_addr2 - 2, patch_addr2);
    return true;
}

bool bc_compiler::compile_fn(const ast::node* ast) {
    // validate the arity of the fn form
    if (ast->list_length < 2) {
        compile_error(ast->loc, "fn requires a parameter list.");
        return false;
    }

    // TODO: generate a meaningful name
    compile_sub_fun(ast->datum.list[1], &ast->datum.list[2],
            ast->list_length - 2, "");

    dyn_array<sst_id> pos_params;
    dyn_array<ast::node*> init_vals;
    bool has_vari;
    sst_id vari;
    if (!process_params(ast->datum.list[1], pos_params, init_vals,
                    has_vari, vari)) {
        return false;
    }
    // compile the child function
    bc_compiler_output child_out;
    bc_compiler child{this, S, *sst, child_out};

    // set up arguments as local variables
    for (auto p : pos_params) {
        child_out.params.push_back(p);
        child.push_var(p);
        ++child.sp;
    }
    if (has_vari) {
        child_out.has_vari = true;
        child_out.vari_param = vari;
        child.push_var(vari);
        ++child.sp;
    }
    if (!child.compile_function_body(&ast->datum.list[2], ast->list_length - 2)) {
        return false;
    }

    // code to create the function object
    auto save_sp = sp;
    for (auto x : init_vals) {
        if (!compile(x, false)) {
            return false;
        }
    }
    auto child_id = output->sub_funs.size;
    output->sub_funs.push_back(child_out);
    emit8(OP_CLOSURE);
    emit16(child_id);
    sp = save_sp + 1;
    return true;
}

bool bc_compiler::compile_quote(const ast::node* root) {
    if (root->list_length != 2) {
        compile_error(root->loc, "quote requires exactly one argument.");
        return false;
    }

    auto cid = output->const_table.size;
    output->const_table.push_back(
            bc_output_const{
                bck_quoted,
                {.quoted = root->datum.list[1]}
            });
    emit8(OP_CONST);
    emit16(cid);
    ++sp;
    return true;
}

bool bc_compiler::compile_symbol_list(const ast::node* root, bool tail) {
    // if this is called, we're guaranteed that the list begins with a symbol

    auto sym_id = intern(S, scanner_name(*sst,
                    root->datum.list[0]->datum.str_id));
    if (sym_id == cached_sym(S, SC_DEF)) {
        return compile_def(root);
    } else if (sym_id == cached_sym(S, SC_DEFMACRO)) {
        return compile_defmacro(root);
    } else if (sym_id == cached_sym(S, SC_DO)) {
        return compile_do(root, tail);
    } else if (sym_id == cached_sym(S, SC_IF)) {
        return compile_if(root, tail);
    // } else if (sym_id == cached_sym(S, SC_IMPORT)) {
    //     return compile_import(root);
    } else if (sym_id == cached_sym(S, SC_FN)) {
        return compile_fn(root);
    // } else if (sym_id == cached_sym(S, SC_LET)) {
    //     return compile_let(root);
    } else if (sym_id == cached_sym(S, SC_QUOTE)) {
        return compile_quote(root);
    // } else if (sym_id == cached_sym(S, SC_SET)) {
    //     return compile_set(root);
    } else {
        return compile_call(root, tail);
    }
    return true;
}

bool bc_compiler::compile_call(const ast::node* root, bool tail) {
    auto save_sp = sp;
    for (u32 i = 0; i < root->list_length; ++i) {
        if (!compile(root->datum.list[i], false)) {
            return false;
        }
    }
    emit8(tail ? OP_TCALL : OP_CALL);
    emit8(root->list_length - 1);
    sp = save_sp + 1;
    return true;
}

bool bc_compiler::compile_body(ast::node* const* exprs, u32 len, bool tail) {
    if (len == 0) {
        emit8(OP_NIL);
        return true;
    }
    u32 save_sp = sp;
    u32 i;
    for (i = 0; i < len - 1; ++i) {
        if (!compile_within_body(exprs[i], false)) {
            return false;
        }
        emit8(OP_POP);
        --sp;
    }
    compile_within_body(exprs[i], tail);
    if (!tail) {
        emit8(OP_CLOSE);
        emit8(sp - save_sp);
    }
    sp = save_sp + 1;
    return true;
}

bool bc_compiler::is_let_form(const ast::node* expr) {
    return expr->kind == ast::ak_list && expr->list_length >= 2
        && expr->datum.list[0]->kind == ast::ak_symbol
        && intern(S, scanner_name(*sst, expr->datum.list[0]->datum.str_id))
        == cached_sym(S, SC_LET);
}

bool bc_compiler::is_do_inline_form(const ast::node* expr) {
    return expr->kind == ast::ak_list && expr->list_length >= 2
        && expr->datum.list[0]->kind == ast::ak_symbol
        && intern(S, scanner_name(*sst, expr->datum.list[0]->datum.str_id))
        == cached_sym(S, SC_DO_INLINE);
}

bool bc_compiler::validate_let_form(const ast::node* expr) {
    if ((expr->list_length & 1) != 1) {
        compile_error(expr->loc, "let requires an even number of arguments.");
        return false;
    }
    for (u32 i = 1; i < expr->list_length; i += 2) {
        if (expr->datum.list[i]->kind != ast::ak_symbol) {
            compile_error(expr->loc, "let names must be symbols.");
            return false;
        }
    }
    return true;
}

bool bc_compiler::compile_within_body(const ast::node* expr, bool tail) {
    bool did_expand = false;
    auto expanded = macroexpand(expr);
    if (expanded) {
        did_expand = true;
        expr = expanded;
    }
    if (is_let_form(expr)) {
        if (!validate_let_form(expr)) {
            if (did_expand) {
                ast::free_graph(expanded);
            }
            return false;
        }
        auto base_sp = sp;
        for (u32 i = 1; i < expr->list_length; i+=2) {
            auto str_id = expr->datum.list[i]->datum.str_id;
            push_var(str_id);
            emit8(OP_NIL);
            ++sp;
        }
        for (u32 i = 2; i < expr->list_length; i+=2) {
            compile(expr->datum.list[i], false);
            emit8(OP_SET_LOCAL);
            emit8(base_sp + (i / 2) - 1);
            --sp;
        }
        emit8(OP_NIL);
        ++sp;
    } else if (is_do_inline_form(expr)) {
        if (expr->list_length == 1) {
            emit8(OP_NIL);
        } else {
            u32 i;
            for (i = 1; i < expr->list_length-1; ++i) {
                compile_within_body(expr->datum.list[i], false);
                emit8(OP_POP);
            }
            compile_within_body(expr->datum.list[i], tail);
        }
    } else {
        if (!compile(expr, tail)) {
            if (did_expand) {
                ast::free_graph(expanded);
            }
            return false;
        }
    }
    if (did_expand) {
        ast::free_graph(expanded);
    }
    return true;
}

bool bc_compiler::compile_number(const ast::node* root) {
    // FIXME: technically we should check for constant ID overflow here
    auto cid = output->const_table.size;
    output->const_table.push_back(bc_output_const{bck_number,
                                                  {.num = root->datum.num}});
    emit8(OP_CONST);
    emit16(cid);
    ++sp;
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
    ++sp;
    return true;
}

bool bc_compiler::compile_symbol(const ast::node* root) {
    auto sid = intern(S, scanner_name(*sst, root->datum.str_id));
    if (sid == cached_sym(S, SC_YES)) {
        emit8(OP_YES);
        ++sp;
    } else if (sid == cached_sym(S, SC_NO)) {
        emit8(OP_NO);
        ++sp;
    } else if (sid == cached_sym(S, SC_NIL)) {
        emit8(OP_NIL);
        ++sp;
    } else {
        // variable lookup
        u8 index;
        if (find_local_var(index, root->datum.str_id)) {
            emit8(OP_LOCAL);
            emit8(index);
            ++sp;
        } else if (find_upvalue_var(index, root->datum.str_id)) {
            emit8(OP_UPVALUE);
            emit8(index);
            ++sp;
        } else {
            emit8(OP_GLOBAL);
            output->globals.push_back(bc_output_global{
                        .raw_name = root->datum.str_id,
                        .patch_addr = output->code.size
                    });
            // placeholder for global ID
            emit32(0);
            ++sp;
        }
    }
    return true;
}

bool bc_compiler::compile_list(const ast::node* root, bool tail) {
    if (root->list_length == 0) {
        compile_error(root->loc, "Empty list is not a legal expression.");
        return false;
    }
    if (root->datum.list[0]->kind == ast::ak_symbol) {
        return compile_symbol_list(root, tail);
    } else {
        return compile_call(root, tail);
    }
    return true;
}

// TODO: track stack space required by each function
bool bc_compiler::compile(const ast::node* root, bool tail) {
    auto expanded = macroexpand(root);
    if (expanded) {
        root = expanded;
    }
    switch (root->kind) {
    case ast::ak_number:
        return compile_number(root);
    case ast::ak_string:
        return compile_string(root);
    case ast::ak_symbol:
        return compile_symbol(root);
    case ast::ak_list:
        return compile_list(root, tail);
    default:
        compile_error(root->loc, "Unsupported form sorry\n");
        return false;
    }
    if (expanded) {
        ast::free_graph(expanded);
    }
    return true;
}

bool bc_compiler::compile_function_body(ast::node* const* exprs, u32 len) {
    if (len == 0) {
        emit8(OP_NIL);
        ++sp;
    }
    if (!compile_body(exprs, len, true)) {
        return false;
    }
    emit8(OP_RETURN);
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
