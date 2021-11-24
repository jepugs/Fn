#include "interpret.hpp"

#include <filesystem>
#include <sstream>

namespace fn {

namespace fs = std::filesystem;

interpreter::interpreter()
    : globals{&symtab}
    , alloc{&globals} {

    search_path.push_back("/usr/lib/fn/ns");
    search_path.push_back(fs::current_path().u8string());

    globals.create_ns(symtab.intern("fn/builtin"));
    globals.create_ns(symtab.intern("fn/user"));
}

interpreter::~interpreter() {
    for (auto s : ffi_stubs) {
        delete s;
    }
}

void interpreter::configure_logging(bool log_llir_forms,
        bool log_disassembly) {
    log_llir = log_llir_forms;
    log_dis = log_disassembly;
}

allocator* interpreter::get_alloc() {
    return &alloc;
}

symbol_table* interpreter::get_symtab() {
    return &symtab;
}

global_env* interpreter::get_global_env() {
    return &globals;
}

void interpreter::interpret_to_end(vm_thread& vm, fault* err) {
    vm.execute();
    while (vm.check_status() == vs_waiting_for_import) {
        // TODO: import code here :/
        break;
    }
    // all other statuses cause us to exit
}

value interpreter::interpret_form(ast_form* ast,
        symbol_id ns,
        fault* err) {
    auto chunk = alloc.add_chunk(ns);
    value res = V_NIL;

    expand_error e_err;
    expander ex{this, chunk};
    auto llir = ex.expand(ast, &e_err);
    free_ast_form(ast);
    if (llir == nullptr) {
        set_fault(err, ast->loc, "expand",
                "Expansion error: " + e_err.message);
        return V_NIL;
    }

    if (log_llir) {
        std::cout << "LLIR: \n";
        print_llir(llir, symtab, chunk);
    }
    compiler c{&symtab, &alloc, chunk};
    compile_error c_err;
    c_err.has_error = false;
    c.compile(llir, &c_err);
    if (c_err.has_error) {
        set_fault(err, ast->loc, "compile",
                "Compilation error: " + c_err.message);
        return V_NIL;
    }

    if (log_dis) {
        std::cout << "Disassembled bytecode: \n";
        disassemble(symtab, *chunk, std::cout);
    }

    vm_thread vm{&alloc, &globals, chunk};
    interpret_to_end(vm, err);

    free_llir_form(llir);
    res = vm.last_pop();

    return res;
}

value interpreter::interpret_file(const string& path, fault* err) {
    std::ifstream in{path};
    return interpret_istream(&in, path, err);
}

value interpreter::interpret_string(const string& src, fault* err) {
    auto ws = alloc.add_working_set();
    u32 bytes_used;
    bool resumable;
    return partial_interpret_string(src, &ws, &bytes_used, &resumable, err);
}

value interpreter::partial_interpret_string(const string& src,
        working_set* ws,
        u32* bytes_used,
        bool* resumable,
        fault* err) {
    std::istringstream in{src};
    auto forms=partial_parse_input(&in,"",&symtab,bytes_used,resumable,err);
    if (forms.size == 0) {
        return V_NIL;
    }

    auto ns_name = symtab.intern("fn/user");
    auto chunk = alloc.add_chunk(ns_name);

    auto res = V_NIL;
    for (auto ast : forms) {
        expand_error e_err;
        expander ex{this, chunk};
        auto llir = ex.expand(ast, &e_err);
        free_ast_form(ast);

        if (llir == nullptr) {
            // TODO: throw an error
            std::cout << "Expansion error: " << e_err.message << '\n';
            break;
        }

        if (log_llir) {
            std::cout << "LLIR: \n";
            print_llir(llir, symtab, chunk);
        }


        compiler c{&symtab, &alloc, chunk};
        compile_error c_err;
        c_err.has_error = false;
        c.compile(llir, &c_err);
        if (c_err.has_error) {
            std::cout << "Compile error: " << c_err.message << '\n';
            break;
        }

        if (log_dis) {
            std::cout << "Disassembled bytecode: \n";
            disassemble(symtab, *chunk, std::cout);
        }

        vm_thread vm{&alloc, &globals, chunk};
        interpret_to_end(vm, err);

        free_llir_form(llir);
        res = vm.last_pop();
    }

    return res;
}

value interpreter::interpret_istream(std::istream* in,
        const string& src_name,
        fault* err) {
    auto forms = parse_input(in, src_name, &symtab, err);
    if (forms.size == 0) {
        return V_NIL;
    }

    auto ns_name = symtab.intern("fn/user");
    auto chunk = alloc.add_chunk(ns_name);
    value res = V_NIL;

    for (auto ast : forms) {
        expand_error e_err;
        expander ex{this, chunk};
        auto llir = ex.expand(ast, &e_err);
        if (llir == nullptr) {
            set_fault(err, ast->loc, "expand",
                    "Expansion error: " + e_err.message);
            free_ast_form(ast);
            break;
        }
        free_ast_form(ast);

        if (log_llir) {
            std::cout << "LLIR: \n";
            print_llir(llir, symtab, chunk);
        }

        compiler c{&symtab, &alloc, chunk};
        compile_error c_err;
        c_err.has_error = false;
        c.compile(llir, &c_err);
        if (c_err.has_error) {
            set_fault(err, llir->origin, "compile",
                    "Compile error: " + c_err.message);
            free_llir_form(llir);
            break;
        }

        if (log_dis) {
            std::cout << "Disassembled bytecode: \n";
            disassemble(symtab, *chunk, std::cout);
        }

        vm_thread vm{&alloc, &globals, chunk};
        interpret_to_end(vm, err);

        free_llir_form(llir);
        res = vm.last_pop();
    }

    return res;
}


ast_form* interpreter::expand_macro(symbol_id macro,
        symbol_id ns_id,
        local_address num_args,
        ast_form** args,
        source_loc& loc) {
    auto chunk = alloc.add_chunk(ns_id);
    auto ws = alloc.add_working_set();

    for (u32 i = 0; i < num_args; ++i) {
        auto v = ast_to_value(&ws, args[i]);
        chunk->write_byte(OP_CONST);
        chunk->write_short(chunk->add_constant(v));
    }

    // TODO: catch runtime errors here to report them to the expander
    chunk->write_byte(OP_TABLE);

    chunk->write_byte(OP_CONST);
    chunk->write_short(chunk->add_constant(as_sym_value(macro)));
    chunk->write_byte(OP_MACRO);

    chunk->write_byte(OP_CALL);
    chunk->write_byte(num_args);
    chunk->write_byte(OP_POP);

    // FIXME: should return this fault to the caller
    fault err;
    vm_thread vm{&alloc, &globals, chunk};
    interpret_to_end(vm, &err);
    if (err.happened) {
        return nullptr;
    }
    auto val = vm.last_pop();
    return value_to_ast(val, loc);
}

value interpreter::ast_to_value(working_set* ws, ast_form* form) {
    switch (form->kind) {
    case ak_number_atom:
        return as_value(form->datum.num);
    case ak_string_atom:
        return ws->add_string(*form->datum.str);
    case ak_symbol_atom:
        return as_sym_value(form->datum.sym);
    case ak_list:
        if (form->list_length == 0) {
            return V_EMPTY;
        } else {
            auto lst = form->datum.list;
            auto res = ws->add_cons(ast_to_value(ws, lst[0]), V_EMPTY);
            auto hd = res;
            for (u32 i = 1; i < form->list_length; ++i) {
                vcons(hd)->tail = ws->add_cons(ast_to_value(ws, lst[i]), V_EMPTY);
                hd = vcons(hd)->tail;
            }
            return res;
        }
    }
    return V_NIL; // unreachable, but g++ was whining
}

ast_form* interpreter::value_to_ast(value v, const source_loc& loc) {
    switch (v_tag(v)) {
    case TAG_NUM:
        return mk_number_form(loc, vnumber(v));
    case TAG_STRING:
        return mk_string_form(loc, *vstring(v));
    case TAG_SYM:
        return mk_symbol_form(loc, vsymbol(v));
    }

    // list code
    if (v == V_EMPTY) {
        dyn_array<ast_form*> lst;
        lst.push_back(mk_symbol_form(loc, symtab.intern("List")));
        return mk_list_form(loc, &lst);
    }

    if (v_tag(v) != TAG_CONS) {
        return nullptr;
    }

    dyn_array<ast_form*> lst;
    for (auto it = v; it != V_EMPTY; it = v_tail(it)) {
        auto x = value_to_ast(v_head(it), loc);
        if (!x) {
            for (auto y : lst) {
                free_ast_form(y);
            }
            return nullptr;
        }
        lst.push_back(x);
    }
    return mk_list_form(loc, &lst);
}

void interpreter::runtime_error(const string& msg,
        const source_loc& loc) {
    throw fn_error("runtime", msg, loc);
}

void interpreter::add_builtin_function(const string& name,
        const string& params,
        value (*foreign_func)(interpreter_handle*, value*)) {
    // Have to extract the params manually, as the expander method could execute
    // code if it encounters an initform that has a macro in it.
    fault err;
    auto forms = parse_string(params, &symtab, &err);
    if (err.happened
            || forms.size != 1
            || forms[0]->kind != ak_list) {
        for (auto f : forms) {
            free_ast_form(f);
        }
        throw std::runtime_error{
            "Malformed parameter string for add_builtin_function."
        };
    }

    auto lst = forms[0]->datum.list;
    auto len = forms[0]->list_length;
    dyn_array<symbol_id> pos_params;
    optional<symbol_id> vl = std::nullopt;
    optional<symbol_id> vt = std::nullopt;

    symbol_id amp = symtab.intern("&");
    symbol_id colamp = symtab.intern(":&");

    u32 i;
    for (i = 0; i < len; ++i) {
        if (!lst[i]->is_symbol()) {
            for (auto f : forms) {
                free_ast_form(f);
            }
            throw std::runtime_error{
                "Malformed parameter string for add_builtin_function."
            };
        }
        auto sym = lst[i]->datum.sym;
        if (sym == amp || sym == colamp) {
            break;
        } else {
            pos_params.push_back(sym);
        }
    }

    // varargs. Unfortunately I don't think there's a better way to do this.
    if (i < len) {
        // must have lst[i] == amp or colamp
        if (lst[i]->datum.sym == amp) {
            if (len - i == 2) {
                vl = lst[i+1]->datum.sym;
            } else if (len - i == 4 && lst[i+2]->datum.sym == colamp) {
                vt = lst[i+3]->datum.sym;
            } else {
                for (auto f : forms) {
                    free_ast_form(f);
                }
                throw std::runtime_error{
                    "Malformed parameter string for add_builtin_function."
                };
            }
        } else if (lst[i]->datum.sym == colamp) {
            if (len - i == 2) {
                vt = lst[i+1]->datum.sym;
            } else if (len - i == 4 && lst[i+2]->datum.sym == amp) {
                vl = lst[i+3]->datum.sym;
            } else {
                for (auto f : forms) {
                    free_ast_form(f);
                }
                throw std::runtime_error{
                    "Malformed parameter string for add_builtin_function."
                };
            }
        }
    }

    // don't need these any more
    for (auto f : forms) {
        free_ast_form(f);
    }

    // FIXME: check that the param list isn't too long
    auto stub = new function_stub{
        .pos_params = pos_params,
        .req_args = (u8)pos_params.size,
        .vl_param = vl,
        .vt_param = vt,
        .foreign = foreign_func,
        .name = name
        // no other values are used by foreign functions
    };
    ffi_stubs.push_back(stub);

    auto ws = alloc.add_working_set();
    auto f = vfunction(ws.add_function(stub));
    auto builtin = *globals.get_ns(symtab.intern("fn/builtin"));
    builtin->set(symtab.intern(name), as_value(f));
}


}
