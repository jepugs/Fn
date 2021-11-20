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
    globals.create_ns(symtab.intern("fn/repl"));
}

interpreter::~interpreter() {
    // TODO: there's probably something to do here...
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

value interpreter::interpret_file(const string& path) {
    // FIXME: handle errors
    std::ifstream in{path};
    fn_scan::scanner sc{&in};

    // TODO: check for package declaration and use it to generate correct
    // namespace name.
    string pkg = "fn/user/";

    // namespace name
    //fs::path path_obj{path};
    //auto ns_name = symtab.intern(pkg + path_obj.stem().u8string());

    // auto chunk = alloc.add_chunk(ns_name);
    // compiler c{&symtab, &alloc, chunk};
    // vm_thread vm{&alloc, &globals, chunk};

    // fn_parse::ast_form* expr;
    // while ((expr = fn_parse::parse_node(sc, symtab))) {
    //     // TODO: this is where macroexpansion goes
    //     c.compile_expr(expr);
    //     delete expr;
    //     interpret_to_end(vm);
    // }
    // // TODO: check vm status

    // return vm.last_pop();
    return V_NIL;
}

value interpreter::interpret_string(const string& src) {
    // FIXME: handle errors! >:(
    std::istringstream in{src};
    fn_scan::scanner sc{&in};

    auto ns_name = symtab.intern("fn/repl");
    auto chunk = alloc.add_chunk(ns_name);
    do_import(symtab,
            **globals.get_ns(ns_name),
            **globals.get_ns(symtab.intern("fn/builtin")),
            "");

    expander ex{this, chunk};
    while (!sc.eof()) {
        parse_error p_err;
        auto ast = parse_form(sc, symtab, &p_err);
        if (ast == nullptr) {
            // TODO: throw an error
            std::cout << "err: " << p_err.message << '\n';
            break;
        }

        expand_error e_err;
        auto llir = ex.expand(ast, &e_err);
        free_ast_form(ast);
        if (llir == nullptr) {
            // TODO: throw an error
            std::cout << "err: " << e_err.message << '\n';
            break;
        }

        print_llir(llir, symtab, chunk);

        compiler c{&symtab, &alloc, chunk};
        compile_error c_err;
        c_err.has_error = false;
        c.compile(llir, &c_err);
        if (c_err.has_error) {
            std::cout << "compile err: " << c_err.message << '\n';
            break;
        }
        disassemble(symtab, *chunk, std::cout);

        vm_thread vm{&alloc, &globals, chunk};
        interpret_to_end(vm);

        free_llir_form(llir);
        return vm.last_pop();
    }

    return V_NIL;
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
        value (*foreign_func)(interpreter_handle*, local_address, value*)) {
    auto ws = alloc.add_working_set();
    auto f = vfunction(ws.add_function(nullptr));
    f->foreign_func = foreign_func;

    auto builtin = *globals.get_ns(symtab.intern("fn/builtin"));
    builtin->set(symtab.intern(name), as_value(f));
}


}
