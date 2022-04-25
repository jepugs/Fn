#include "namespace.hpp"

namespace fn {

global_env::~global_env() {
    for (auto e : ns_tab) {
        delete e->val;
    }
}

bool resolve_symbol(symbol_id& out, istate* S, symbol_id name) {
    return resolve_in_ns(out, S, name, S->ns_id);
}

bool resolve_in_ns(symbol_id& out, istate* S, symbol_id name, symbol_id ns_id) {
    auto x = S->G->ns_tab.get2(ns_id);
    if (!x) {
        return false;
    }
    // try to resolve the name in the local namespace first
    auto ns = x->val;
    auto y = ns->resolve.get2(name);
    if (y) {
        out = y->val;
        return true;
    }

    // expand based on the name string
    auto name_str = symname(S, name);
    if (name_str.starts_with("#:")) {
        // already a fully qualified name
        out = name;
        return true;
    }
    auto p = name_str.find(":");
    if (p != string::npos) {
        // name contains a colon, need to resolve it in an external namespace
        auto ns_alias_str = name_str.substr(0, p);
        auto var_str = name_str.substr(p + 1);
        if (var_str.empty()
                || ns_alias_str.empty()
                || var_str.find(":") != string::npos) {
            // illegal colon syntax
            return false;
        }

        // check if we've imported anything with this alias
        auto z = ns->imports.get2(ns_alias_str);
        if (!z) {
            // unknown alias
            return false;
        }
        // get the imported namespace
        auto w = S->G->ns_tab.get2(z->val);
        if (!w) {
            // Aliased namespace doesn't exist. This should never happen, but
            // we'll handle it as a failed lookup
            return false;
        }
        auto other_ns = w->val;
        // attempt to resolve in other_ns
        auto a = other_ns->resolve.get2(intern_id(S, var_str));
        if (!a) {
            return false;
        } else {
            out = a->val;
            return true;
        }
    } else {
        // no colon, just expand the name in the current namespace
        out = intern_id(S, "#:" + symname(S, ns->id) + ":" + name_str);
        return true;
    }
}

symbol_id resolve_in_ns(istate* S, symbol_id name, symbol_id ns_id) {
    auto ns = S->G->ns_tab.get(ns_id);
    if (!ns.has_value()) {
        ierror(S, "Failed to resolve symbol name. No such namespace: "
                + symname(S, ns_id));
        return 0;
    }
    auto name_str = symname(S, name);
    // check for FQN
    if (name_str.size() >= 2 && name_str[0] == '#' && name_str[1] == ':') {
        return name;
    } else {
        auto x = (*ns)->resolve.get(name);
        if (x.has_value()) {
            return *x;
        } else {
            // unrecognized symbol => treat it as a global variable in namespace
            auto fqn = intern_id(S, "#:" + symname(S, (*ns)->id) + ":"
                    + name_str);
            // add to resolution table
            (*ns)->resolve.insert(name, fqn);
            return fqn;
        }
    }
}

u32 get_global_id(istate* S, symbol_id fqn) {
    auto e = S->G->def_tab.get2(fqn);
    if (e) {
        return e->val;
    } else {
        auto id = S->G->def_ids.size;
        S->G->def_ids.push_back(fqn);
        S->G->def_tab.insert(fqn, id);
        S->G->def_arr.push_back(V_UNIN);
        S->G->macro_arr.push_back(nullptr);
        return id;
    }
}

bool get_global(value& out, istate* S, symbol_id fqn) {
    auto res = S->G->def_tab.get2(fqn);
    if (!res) {
        return false;
    }
    out = S->G->def_arr[res->val];
    return true;
}

void set_global(istate* S, symbol_id fqn, value new_val) {
    auto x = S->G->def_tab.get2(fqn);
    if (x) {
        S->G->def_arr[x->val] = new_val;
    } else {
        auto id = S->G->def_arr.size;
        S->G->def_arr.push_back(new_val);
        S->G->def_ids.push_back(fqn);
        S->G->def_tab.insert(fqn, id);
    }
}

bool get_macro(value& out, istate* S, symbol_id fqn) {
    auto res = S->G->macro_tab.get(fqn);
    if (!res.has_value()) {
        return false;
    }
    out = vbox_function(*res);
    return true;
}

void set_macro(istate* S, symbol_id fqn, fn_function* fun) {
    S->G->macro_tab.insert(fqn, fun);
}

fn_namespace* add_ns(istate* S, symbol_id ns_id) {
    auto x = S->G->ns_tab.get(ns_id);
    if (x.has_value()) {
        return *x;
    }
    auto res = new fn_namespace{.id = ns_id};
    // FIXME: we just assume this exists here. Maybe we shouldn't?
    auto builtin_ns = get_ns(S, cached_sym(S, SC_FN_BUILTIN));
    if (builtin_ns) {
        for (auto s : builtin_ns->exports) {
            enact_import_from(res, S, builtin_ns, s);
        }
    }
    S->G->ns_tab.insert(ns_id, res);
    return res;
}

fn_namespace* get_ns(istate* S, symbol_id ns_id) {
    auto x = S->G->ns_tab.get2(ns_id);
    if (x) {
        return x->val;
    }
    return nullptr;
}

void switch_ns(istate* S, symbol_id new_ns) {
    S->ns_id = add_ns(S, new_ns)->id;
}

void add_export(fn_namespace* dest, istate* S, symbol_id sym) {
    for (auto s : dest->exports) {
        if (s == sym) {
            // don't export the symbol twice
            return;
        }
    }
    dest->exports.push_back(sym);
    auto fqn_str = "#:" + symname(S, dest->id) + ":" + symname(S, sym);
    dest->resolve.insert(sym, intern_id(S, fqn_str));
}

bool enact_import(fn_namespace* dest, istate* S, fn_namespace* src,
        string prefix) {
    if (dest->imports.get2(prefix)) {
        ierror(S, "Failed importing namespace as " + prefix
                + " due to name collision.");
        return false;
    }
    dest->imports.insert(prefix, src->id);
    return true;
}

bool enact_import_from(fn_namespace* dest, istate* S, fn_namespace* src,
        symbol_id sym) {
    auto x = src->resolve.get2(sym);
    if (!x) {
        ierror(S, "Failed importing symbol: not found.");
        return false;
    }
    auto y = dest->resolve.get2(sym);
    if (y) {
        ierror(S, "Failed importing symbol: name " + symname(S, sym)
                + " is already taken.");
        return false;
    }
    dest->resolve.insert(sym, x->val);
    dest->exports.push_back(sym);
    return true;
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


}
