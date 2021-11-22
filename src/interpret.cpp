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

void interpreter::interpret_to_end(vm_thread& vm) {
    vm.execute();
    while (vm.check_status() == vs_waiting_for_import) {
        // TODO: import code here :/
        break;
    }
    // all other statuses cause us to exit
}

value interpreter::interpret_file(const string& path, bool* error) {
    std::ifstream in{path};
    return interpret_istream(&in, path, error);
}

value interpreter::interpret_string(const string& src) {
    auto ws = alloc.add_working_set();
    u32 bytes_used;
    return partial_interpret_string(src, &ws, &bytes_used);
}

value interpreter::partial_interpret_string(const string& src,
        working_set* ws,
        u32* bytes_used) {
    parse_error p_err;
    auto forms = parse_string(src, &symtab, bytes_used, &p_err);
    if (forms.size() == 0) {
        if (!p_err.resumable) {
            // TODO: we can do better than this for error handling
            std::cout << "Parse error: " << p_err.message << '\n';
            return V_NIL;
        } else {
            return V_NIL;
        }
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
        interpret_to_end(vm);

        free_llir_form(llir);
        res = vm.last_pop();
    }

    return res;
}

value interpreter::interpret_istream(std::istream* in,
        const string& src_name,
        bool* error) {
    fn_scan::scanner sc{in, src_name};
    parse_error p_err;

    auto forms = parse_input(&sc, &symtab, &p_err);
    if (forms.size() == 0) {
        if (error != nullptr) {
            *error = true;
        }
        std::cout << "Parser error: " << p_err.message << '\n';
        return V_NIL;
    }

    auto ns_name = symtab.intern("fn/user");
    auto chunk = alloc.add_chunk(ns_name);
    value res = V_NIL;

    *error = false;
    for (auto ast : forms) {
        expand_error e_err;
        expander ex{this, chunk};
        auto llir = ex.expand(ast, &e_err);
        free_ast_form(ast);

        if (llir == nullptr) {
            if (error != nullptr) {
                *error = true;
            }
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
            if (error != nullptr) {
                *error = true;
            }
            std::cout << "Compile error: " << c_err.message << '\n';
            break;
        }

        if (log_dis) {
            std::cout << "Disassembled bytecode: \n";
            disassemble(symtab, *chunk, std::cout);
        }

        vm_thread vm{&alloc, &globals, chunk};
        interpret_to_end(vm);

        free_llir_form(llir);
        res = vm.last_pop();
    }

    return res;
}


ast_form* interpreter::macroexpand(symbol_id ns_id, const ast_form* form) {
    auto res = form->copy();
    return res;
}

value interpreter::ast_to_value(working_set* ws, const ast_form* form) {
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

void interpreter::runtime_error(const string& msg,
        const source_loc& loc) {
    throw fn_error("runtime", msg, loc);
}

void interpreter::add_builtin_function(const string& name,
        const string& params,
        value (*foreign_func)(interpreter_handle*, value*)) {
    // Have to extract the params manually, as the expander method could execute
    // code if it encounters an initform that has a macro in it.
    parse_error err;
    u32 bytes_used;
    auto forms = parse_string(params, &symtab, &bytes_used, &err);
    if (forms.size() != 1
            || bytes_used != params.size()
            || forms[0]->kind != ak_list) {
        throw std::runtime_error{
            "Malformed parameter string for add_builtin_function."
        };
    }

    auto lst = forms[0]->datum.list;
    auto len = forms[0]->list_length;
    vector<symbol_id> pos_params;
    optional<symbol_id> vl = std::nullopt;
    optional<symbol_id> vt = std::nullopt;

    symbol_id amp = symtab.intern("&");
    symbol_id colamp = symtab.intern(":&");

    u32 i;
    for (i = 0; i < len; ++i) {
        if (!lst[i]->is_symbol()) {
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
                throw std::runtime_error{
                    "Malformed parameter string for add_builtin_function."
                };
            }
        }
    }

    // FIXME: check that the param list isn't too long
    auto stub = new function_stub{
        .pos_params = pos_params,
        .req_args = (u8)pos_params.size(),
        .vl_param = vl,
        .vt_param = vt,
        .foreign = foreign_func
        // no other values are used by foreign functions
    };
    ffi_stubs.push_back(stub);

    auto ws = alloc.add_working_set();
    auto f = vfunction(ws.add_function(stub));
    auto builtin = *globals.get_ns(symtab.intern("fn/builtin"));
    builtin->set(symtab.intern(name), as_value(f));
}


}
