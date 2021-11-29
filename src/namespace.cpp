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
    : symtab{use_symtab}
    , builtin_id{symtab->intern("fn/builtin")}
    , import_builtin{false} {
    create_ns(builtin_id);
    import_builtin=true;
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
    if (import_builtin) {
        copy_defs(*symtab, *res, **ns_table.get(builtin_id), "");
    }
    return res;
}

void ns_name(const string& global_name, string* pkg, string* name) {
    if (global_name.size() == 0) {
        *name = "";
        *pkg = "";
        return;
    }
    i64 last_slash = -1;
    for (u32 i = 0; i < global_name.size(); ++i) {
        if (global_name[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash == -1) {
        *pkg = "";
    } else {
        *pkg = global_name.substr(0, last_slash);
    }
    *name = global_name.substr(last_slash+1);
}

bool is_subpkg(const string& sub, const string& pkg) {
    if (sub.size() < pkg.size()) {
        return false;
    }
    u32 i;
    for (i = 0; i < pkg.size(); ++i) {
        if (sub[i] != pkg[i]) {
            return false;
        }
    }
    if (i < sub.size()) {
        return sub[i] == '/';
    } else {
        return true;
    }
}

string subpkg_rel_path(const string& sub, const string& pkg) {
    if (sub.size() == pkg.size()) {
        return ".";
    } else {
        return sub.substr(pkg.size()+1);
    }
}

void copy_defs(symbol_table& symtab, fn_namespace& dest, fn_namespace& src,
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
