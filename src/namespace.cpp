#include "namespace.hpp"

namespace fn {

fn_namespace::fn_namespace(symbol_id name)
    : name{name} {
}

optional<value> fn_namespace::get(symbol_id sym) const {
    return defs.get(sym);
}

void fn_namespace::set(symbol_id sym, const value& v) {
    defs.insert(sym, v);
}

forward_list<symbol_id> fn_namespace::names() const {
    return defs.keys();
}

optional<value> fn_namespace::get_macro(symbol_id sym) const {
    return macros.get(sym);
}

void fn_namespace::set_macro(symbol_id sym, const value& v) {
    macros.insert(sym, v);
}

forward_list<symbol_id> fn_namespace::macro_names() const {
    return macros.keys();
}


global_env::global_env(symbol_table* use_symtab)
    : symtab{use_symtab} {
}

global_env::~global_env() {
    for (auto k : ns_table.keys()) {
        delete *ns_table.get(k);
    }
}

symbol_table* global_env::get_symtab() {
    return symtab;
}

optional<fn_namespace*> global_env::get_ns(symbol_id name) {
    return ns_table.get(name);
}

fn_namespace* global_env::create_ns(symbol_id name) {
    auto x = ns_table.get(name);
    if (x.has_value()) {
        delete *x;
    }
    auto res = new fn_namespace(name);
    ns_table.insert(name, res);
    return res;
}

void do_import(symbol_table& symtab, fn_namespace& dest, fn_namespace& src,
        const string& prefix) {
    for (auto k : src.names()) {
        auto name = symtab.intern(prefix + symtab[k]);
        dest.set(name, *src.get(k));
    }
    for (auto k : src.macro_names()) {
        auto name = symtab.intern(prefix + symtab[k]);
        dest.set_macro(name, *src.get(k));
    }
}


}
