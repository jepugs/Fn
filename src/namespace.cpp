#include "namespace.hpp"

namespace fn {

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


void ns_id_destruct(const string& ns_id, string* prefix, string* stem) {
    if (ns_id.size() == 0) {
        *stem = "";
        *prefix = "";
        return;
    }
    i64 last_slash = -1;
    for (u32 i = 0; i < ns_id.size(); ++i) {
        if (ns_id[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash == -1) {
        *prefix = "";
    } else {
        *prefix = ns_id.substr(0, last_slash);
    }
    *stem = ns_id.substr(last_slash+1);
}

bool is_subns(const string& sub, const string& ns) {
    if (sub.size() < ns.size()) {
        return false;
    } else if (ns.size() == 0) {
        return true;
    }
    u32 i;
    for (i = 0; i < ns.size(); ++i) {
        if (sub[i] != ns[i]) {
            return false;
        }
    }
    if (i < sub.size()) {
        return sub[i] == '/';
    } else {
        return true;
    }
}

string subns_rel_path(const string& sub, const string& ns) {
    if (sub.size() == ns.size()) {
        return ".";
    } else if (ns.size() == 0) {
        return sub;
    } else {
        return sub.substr(ns.size()+1);
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
        dest.set_macro(name, *src.get_macro(k));
    }
}


}
