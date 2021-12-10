#include "interpret.hpp"
#include "config.h"

#include <filesystem>
#include <sstream>

namespace fn {

namespace fs = std::filesystem;

interpreter::interpreter(logger* log)
    : globals{&symtab}
    , alloc{&globals}
    , log{log}
    , base_dir{"."}
    , base_pkg{symtab.intern("fn/user")} {

    auto ws = alloc.add_working_set();
    ffi_chunk = ws.add_chunk(symtab.intern("fn/builtin"));
    ++ffi_chunk->h.pin_count;
    alloc.add_gc_root(new pinned_object{(gc_header*)ffi_chunk});
}

interpreter::~interpreter() {
}

void interpreter::set_log_dis(bool b) {
    log_dis = b;
}

void interpreter::set_log_llir(bool b) {
    log_llir = b;
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
    auto ws = alloc.add_working_set();
    vm.execute(err);
    while (vm.check_status() == vs_waiting_for_import) {
        import_ns(vm.get_pending_import_id(), &ws, err);
        if (err->happened) {
            break;
        }
        vm.execute(err);
    }
    // all other statuses cause us to exit
}

value interpreter::interpret_form(ast_form* ast,
        symbol_id ns,
        working_set* ws,
        fault* err) {
    auto chunk = ws->add_chunk(ns);
    value res = V_NIL;

    expander ex{this, chunk};
    auto llir = ex.expand(ast, err);
    free_ast_form(ast);
    if (llir == nullptr) { // indicates the fault has been set
        return V_NIL;
    }

    if (log_llir) {
        std::cout << "LLIR: \n";
        print_llir(llir, symtab, chunk);
    }
    compiler c{&symtab, &alloc, chunk};
    c.compile(llir, err);
    if (err->happened) {
        log_error(err);
        return V_NIL;
    }

    if (log_dis) {
        std::cout << "Disassembled bytecode: \n";
        disassemble(symtab, *chunk, std::cout);
    }

    vm_thread vm{&alloc, &globals, chunk};
    interpret_to_end(vm, err);

    free_llir_form(llir);
    res = vm.last_pop(ws);

    return res;
}

value interpreter::interpret_file(const string& path,
        symbol_id ns_id,
        working_set* ws,
        fault* err) {
    std::ifstream in{path};
    scanner sc{&in, fs::path{path}.filename()};

    // FIXME: check whether the declared package matches the given one

    // Skip package declaration
    auto x = read_pkg_decl(&sc, ws, err);
    if (err->happened) {
        log_error(err);
        return V_NIL;
    }
    if (!x.has_value()) {
        //reset the scanner
        in = std::ifstream{path};
        sc = scanner{&in, fs::path{path}.filename()};
    }

    // create namespace if necessary
    auto ns = globals.get_ns(ns_id);
    if (!ns.has_value()) {
        globals.create_ns(ns_id);
    }
    bool resumable;
    auto res = interpret_from_scanner(&sc, ns_id, ws, &resumable, err);
    if (err->happened) {
        //log_error(err);
    }
    return res;

}

value interpreter::interpret_main_file(const string& path,
        working_set* ws,
        fault* err) {
    fs::path p{path};
    std::ifstream in{path};
    scanner sc{&in, p.filename()};
    fs::path dir{p};
    base_dir = dir.remove_filename().u8string();
    if (base_dir.size() == 0) {
        base_dir = ".";
    }

    source_loc start;
    auto x = read_pkg_decl(&sc, ws, err);
    // TODO: validate package name
    string pkg;
    if (!x.has_value()) {
        if (err->happened) {
            log_error(err);
            return V_NIL;
        }
        // no package declaration, so reinitialize the scanner
        in = std::ifstream{path};
        sc = scanner{&in, p.filename()};
        pkg = "fn/user";
    } else {
        pkg = symtab[*x];
    }
    base_pkg = intern(pkg);
    string ns_str = pkg + "/" + p.stem().u8string();
    bool resumable;
    auto res = interpret_from_scanner(&sc, symtab.intern(ns_str), ws,
            &resumable, err);
    if (err->happened) {
        log_error(err);
    }
    return res;
}


value interpreter::interpret_string(const string& src,
        symbol_id ns_id,
        working_set* ws,
        fault* err) {
    u32 bytes_used;
    bool resumable;
    auto vals = partial_interpret_string(src, ns_id, ws, &bytes_used,
            &resumable, err);
    // if !resumable, then partial_interpret_string already logged the error.
    if (err->happened && resumable) {
        log_error(err);
    }
    if (vals.size == 0) {
        return V_NIL;
    } else {
        return vals[vals.size-1];
    }
}

dyn_array<value> interpreter::partial_interpret_string(const string& src,
        symbol_id ns_id,
        working_set* ws,
        u32* bytes_used,
        bool* resumable,
        fault* err) {
    auto ws2 = alloc.add_working_set();
    std::istringstream in{src};
    auto strloc = string{"<string in ns:"} + symtab[ns_id] + ">";
    scanner sc{&in, strloc};
    auto forms = partial_parse_input(&sc, &symtab, bytes_used,
            resumable, err);
    if (forms.size == 0) {
        return {};
    }

    auto ns_name = symtab.intern("fn/user/repl");
    auto chunk = ws2.add_chunk(ns_name);

    dyn_array<value> res;
    for (auto ast : forms) {
        expander ex{this, chunk};
        auto llir = ex.expand(ast, err);
        free_ast_form(ast);
        if (llir == nullptr) { // fault has occurred
            *resumable = false;
            break;
        }
        if (log_llir) {
            log->log_info("interpreter",
                    "LLIR: \n" + print_llir(llir, symtab, chunk));
        }

        compiler c{&symtab, &alloc, chunk};
        c.compile(llir, err);
        free_llir_form(llir);
        if (err->happened) {
            log_error(err);
            *resumable = false;
            break;
        }
        if (log_dis) {
            std::ostringstream out;
            disassemble(symtab, *chunk, out);
            log->log_info("interpreter", "Disassembled bytecode: \n" + out.str());
        }

        vm_thread vm{&alloc, &globals, chunk};
        interpret_to_end(vm, err);
        if (err->happened) {
            log_error(err);
            *resumable = false;
            break;
        }

        res.push_back(vm.last_pop(ws));
    }

    return res;
}

value interpreter::interpret_from_scanner(scanner* sc,
        symbol_id ns_id,
        working_set* ws,
        bool* resumable,
        fault* err) {
    auto forms = parse_from_scanner(sc, &symtab, err);
    if (forms.size == 0) {
        return V_NIL;
    }

    auto chunk = ws->add_chunk(ns_id);
    value res = V_NIL;

    u32 i;
    for (i = 0; i < forms.size; ++i) {
        auto ast = forms[i];
        expander ex{this, chunk};
        auto llir = ex.expand(ast, err);
        free_ast_form(ast);
        if (llir == nullptr) {
            break;
        }

        if (log_llir) {
            std::cout << "LLIR: \n";
            print_llir(llir, symtab, chunk);
        }

        compiler c{&symtab, &alloc, chunk};
        c.compile(llir, err);
        free_llir_form(llir);
        if (err->happened) { // fault
            break;
        }

        vm_thread vm{&alloc, &globals, chunk};
        interpret_to_end(vm, err);
        if (err->happened) {
            break;
        }

        res = vm.last_pop(ws);
    }
    // clean up extra forms in case of early termination
    if (i < forms.size) {
        for (i = i+1; i < forms.size; ++i) {
            free_ast_form(forms[i]);
        }
    }

    if (log_dis) {
        std::cout << "Disassembled bytecode: \n";
        disassemble(symtab, *chunk, std::cout);
    }

    return res;
}

bool interpreter::import_ns(symbol_id ns_id, working_set* ws, fault* err) {
    // TODO: emit warning if import disagrees with file package declaration
    auto x = find_import_file(ns_id);
    if (!x.has_value()) {
        return false;
    }
    // create ns
    if (!globals.get_ns(ns_id).has_value()) {
        globals.create_ns(ns_id);
    }
    interpret_file(*x, ns_id, ws, err);
    return !err->happened;
}

optional<symbol_id> interpreter::read_pkg_decl(scanner* sc,
        working_set* ws,
        fault* err) {
    bool resumable;
    auto ast = parse_next_form(sc, &symtab, &resumable, err);
    if (err->happened) {
        return {};
    }
    if (ast->kind != ak_list || ast->list_length == 0) {
        free_ast_form(ast);
        return {};
    }
    auto op = ast->datum.list[0];
    if (op->kind != ak_symbol_atom || op->datum.sym != intern("package")) {
        free_ast_form(ast);
        return {};
    }
    if (ast->list_length != 2) {
        set_fault(err, ast->loc, "interpreter",
                "Incorrect arity in package declaration.");
        free_ast_form(ast);
        return {};
    }
    auto name_form = ast->datum.list[1];
    if (name_form->kind != ak_symbol_atom) {
        set_fault(err, name_form->loc, "interpreter",
                "Package name must be a symbol.");
        free_ast_form(ast);
        return {};
    }
    auto res = name_form->datum.sym;
    free_ast_form(ast);
    return res;
}

optional<string> interpreter::find_import_file(symbol_id ns_id) {
    string pkg, ns;
    ns_name(symtab[ns_id], &pkg, &ns);
    string base = symtab[base_pkg];
    if (is_subpkg(pkg, base)) {
        auto str = subpkg_rel_path(pkg, base);
        auto res = str + "/" + ns + ".fn";
        return res;
    }
    // system path
    auto str = string{PREFIX} + "/lib/fn/packages/" +  symtab[ns_id] + ".fn";
    fs::path p{str};
    if (fs::exists(p) && !fs::is_directory(p)) {
        return p.u8string();
    }
    return {};
}

ast_form* interpreter::expand_macro(symbol_id macro,
        symbol_id ns_id,
        local_address num_args,
        ast_form** args,
        source_loc& loc,
        fault* err) {
    auto ws = alloc.add_working_set();
    auto chunk = ws.add_chunk(ns_id);

    for (u32 i = 0; i < num_args; ++i) {
        auto v = ast_to_value(&ws, args[i]);
        chunk->write_byte(OP_CONST);
        chunk->write_short(chunk->add_constant(v));
    }

    chunk->write_byte(OP_CONST);
    chunk->write_short(chunk->add_constant(vbox_symbol(macro)));
    chunk->write_byte(OP_MACRO);

    chunk->write_byte(OP_CALL);
    chunk->write_byte(num_args);
    chunk->write_byte(OP_POP);

    vm_thread vm(&alloc, &globals, chunk);
    interpret_to_end(vm, err);
    if (err->happened) {
        return nullptr;
    }
    auto val = vm.last_pop(&ws);
    return value_to_ast(val, loc);
}

value interpreter::ast_to_value(working_set* ws, ast_form* form) {
    switch (form->kind) {
    case ak_number_atom:
        return vbox_number(form->datum.num);
    case ak_string_atom:
        return ws->add_string(*form->datum.str);
    case ak_symbol_atom:
        return vbox_symbol(form->datum.sym);
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
        return mk_list_form(loc, &lst);
    }

    if (v_tag(v) != TAG_CONS) {
        return nullptr;
    }

    dyn_array<ast_form*> lst;
    for (auto it = v; it != V_EMPTY; it = vtail(it)) {
        auto x = value_to_ast(vhead(it), loc);
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
    throw fn_exception("runtime", msg, loc);
}

void interpreter::add_builtin_function(const string& name,
        const string& params,
        value (*foreign_func)(fn_handle*, value*)) {
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
    auto stub_id = ffi_chunk->add_foreign_function((u8)pos_params.size,
            pos_params.data, (u8)pos_params.size, vl, vt, foreign_func, name);
        // no other values are used by foreign functions

    auto ws = alloc.add_working_set();
    auto f = vfunction(ws.add_function(ffi_chunk->get_function(stub_id)));
    auto builtin = *globals.get_ns(symtab.intern("fn/builtin"));
    builtin->set(symtab.intern(name), vbox_function(f));
}

symbol_id interpreter::intern(const string& str) {
    return symtab.intern(str);
}

symbol_id interpreter::gensym() {
    return symtab.gensym();
}

void interpreter::log_error(fault* err) {
    log->log_fault(*err);
}

}
