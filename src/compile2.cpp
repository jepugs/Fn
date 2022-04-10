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

bc_compiler::bc_compiler(bc_compiler* parent, scanner_string_table& sst)
    : parent{parent}
    , sst{&sst}
    , sp{0} {
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

bool bc_compiler::compile_fn(const ast::node* ast) {
    // validate the arity of the fn form
    if (ast->list_length < 2) {
        compile_error(ast->loc, "fn requires a parameter list.");
        return false;
    }

    dyn_array<sst_id> pos_params;
    dyn_array<ast::node*> init_vals;
    bool has_vari;
    sst_id vari;
    if (!process_params(ast->datum.list[1], pos_params, init_vals,
                    has_vari, vari)) {
        return false;
    }
    // compile the child function
    bc_compiler child{this, *sst};

    // set up arguments as local variables
    for (auto p : pos_params) {
        child.push_var(p);
        ++child.sp;
    }
    if (has_vari) {
        child.push_var(vari);
        ++child.sp;
    }
    if (!child.compile_body(&ast->datum.list[2], ast->list_length - 2)) {
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
    output->sub_funs.push_back(*child.output);
    emit8(OP_CLOSURE);
    emit16(child_id);
    sp = save_sp + 1;
    return true;
}

bool bc_compiler::compile_number(const ast::node* root) {
    // FIXME: technically we should check for constant ID overflow here
    auto cid = output->const_table.size;
    output->const_table.push_back(bc_output_const{bck_number, root->datum.num});
    emit8(OP_CONST);
    emit16(cid);
    return true;
}

bool bc_compiler::compile(const ast::node* root, bool tail) {
    switch (root->kind) {
    case ast::ak_number:
        return compile_number(root);
    default:
        compile_error(root->loc, "Unsupported form sorry\n");
        return false;
    }
}

void compile_to_bytecode(istate* S, error_info& err,
        const scanner_string_table& sst, const ast::node* root) {
}

}
