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
    fs::path path_obj{path};
    auto ns_name = symtab.intern(pkg + path_obj.stem().u8string());

    auto chunk = alloc.add_chunk(ns_name);
    // compiler c{&symtab, &alloc, chunk};
    // vm_thread vm{&alloc, &globals, chunk};

    // fn_parse::ast_node* expr;
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
    // compiler c{&symtab, &alloc, chunk};
    // vm_thread vm{&alloc, &globals, chunk};

    // fn_parse::ast_node* expr;
    // while ((expr = fn_parse::parse_node(sc, symtab))) {
    //     // TODO: this is where macroexpansion goes
    //     c.compile_expr(expr);
    //     delete expr;
    //     disassemble(symtab, *chunk, std::cout);
    //     interpret_to_end(vm);
    // }
    // // TODO: check vm status

    // return vm.last_pop();
    return V_NIL;
}

ast_node* macroexpand(fn_namespace* ns, const ast_node* form) {
    auto res = form->copy();
}

void interpreter::runtime_error(const string& msg,
        const source_loc& loc) {
    throw fn_error("runtime", msg, loc);
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

void interpreter::add_builtin_function(const string& name,
        value (*foreign_func)(interpreter_handle*, local_address, value*)) {
    auto ws = alloc.add_working_set();
    auto f = vfunction(ws.add_function(nullptr));
    f->foreign_func = foreign_func;

    auto builtin = *globals.get_ns(symtab.intern("fn/builtin"));
    builtin->set(symtab.intern(name), as_value(f));
}


}
