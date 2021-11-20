#ifndef __FN_NAMESPACE_HPP
#define __FN_NAMESPACE_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {

// namespaces are key-value stores used to hold global variables and imports
struct alignas(32) fn_namespace {
    symbol_id name;
    table<symbol_id,value> defs;
    table<symbol_id,value> macros;

    fn_namespace(symbol_id name);

    optional<value> get(symbol_id name) const;
    void set(symbol_id name, const value& v);
    forward_list<symbol_id> names() const;

    optional<value> get_macro(symbol_id name) const;
    void set_macro(symbol_id name, const value& v);
    forward_list<symbol_id> macro_names() const;
};


// global_env keeps track of currently-loaded chunks and namespaces
struct global_env {
    symbol_table* symtab;
    table<symbol_id, fn_namespace*> ns_table;

    // this creates the root namespace hierarchy including the fn.builtin
    // namespace.
    global_env(symbol_table* use_symtab);
    ~global_env();

    symbol_table* get_symtab();

    optional<fn_namespace*> get_ns(symbol_id name);
    fn_namespace* create_ns(symbol_id name);

};

// import bindings src into dest. The new bindings' names consist
// of the names in src with prefix prepended.
void do_import(symbol_table& symtab,
        fn_namespace& dest,
        fn_namespace& src,
        const string& prefix);

}

#endif
