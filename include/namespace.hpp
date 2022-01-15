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


// namespace id destructuring
void ns_id_destruct(const string& global_name, string* prefix, string* stem);
// tell if sub is a subns of pkg
bool is_subns(const string& sub, const string& ns);
// Returns the suffix of sub which distinguishes it from superpackage pkg. If
// this would be the empty string, returns ".". This will not work properly if
// sub is not a subpackage of pkg.
string subns_rel_path(const string& sub, const string& ns);


// import bindings src into dest. The new bindings' names consist
// of the names in src with prefix prepended.
void copy_defs(symbol_table& symtab,
        fn_namespace& dest,
        fn_namespace& src,
        const string& prefix);

}

#endif
