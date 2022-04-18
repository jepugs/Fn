#include "namespace.hpp"

namespace fn {

global_env::~global_env() {
    for (auto e : ns_tab) {
        delete e->val;
    }
}

symbol_id resolve_symbol(istate* S, symbol_id name) {
    return resolve_in_ns(S, name, S->ns_id);
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
    copy_defs(S, res, get_ns(S, cached_sym(S, SC_FN_BUILTIN)), "");
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

bool copy_defs(istate* S, fn_namespace* dest, fn_namespace* src,
        const string& prefix, bool overwrite) {
    if (dest == nullptr || src == nullptr) {
        return true;
    }
    if (src == dest) {
        ierror(S, "Attempt to copy a namespace into itself.\n");
        return false;
    }
    for (auto e : src->resolve) {
        auto name = intern_id(S, prefix + symname(S, e->key));
        if (!overwrite && dest->resolve.has_key(name)) {
            ierror(S, "Name collision while copying definitions.");
            return false;
        }
        dest->resolve.insert(name, e->val);
    }
    return true;
}

void switch_ns(istate* S, symbol_id new_ns) {
    S->ns_id = add_ns(S, new_ns)->id;
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
