#ifndef __FN_NAMESPACE_HPP
#define __FN_NAMESPACE_HPP

#include "base.hpp"
#include "istate.hpp"
#include "values.hpp"

namespace fn {

struct fn_namespace {
    symbol_id id;
    // resolution tables map namespace-local names to FQNs
    table<symbol_id,symbol_id> resolve;
};

struct global_env {
    // definition IDs indexed by fully qualified name (FQN)
    table<symbol_id,u32> def_tab;
    // actual global definitions
    dyn_array<value> def_arr;
    // reverse lookup of def_tab
    dyn_array<symbol_id> def_ids;
    // macros indexed by FQN
    table<symbol_id,fn_function*> macro_tab;
    // table of namespaces by name
    table<symbol_id,fn_namespace*> ns_tab;

    // metatables for builtin types
    value list_meta = V_NIL;
    value string_meta = V_NIL;

    ~global_env();
};

// resolve a symbol to an FQN in the given namespace. The given namespace must
// exist when the call is made, or an error is generated. If the given symbol is
// not already present in the namespace, a new entry in the resolution table is
// made.
symbol_id resolve_sym(istate* S, symbol_id ns_id, symbol_id name);
// get the unique 32-bit identifier for a global variable. The variable will be
// created and set to V_UNIN if necessary.
u32 get_global_id(istate* S, symbol_id fqn);
// get the FQN of a symbol based on its numerical id
symbol_id global_name_by_id(istate* S, u32 id);
// get a global variable by its FQN. Returns false on failed lookup
bool push_global(istate* S, symbol_id fqn);
// set by FQN, creating a new definition if necessary
void set_global(istate* S, symbol_id fqn, value new_val);
// get a macro by its FQN. Returns false on failed lookup
bool push_macro(istate* S, symbol_id fqn);
// set a macro, creating a new definition if necessary
void set_macro(istate* S, symbol_id fqn, fn_function* fun);
// add a new namespace. If the namespace already exists, this returns the
// existing namespace.
fn_namespace* add_ns(istate* S, symbol_id ns_id);
// get a namespace, returning nullptr if it doesn't exist
fn_namespace* get_ns(istate* S, symbol_id ns_id);
// copy definitions from one namespace to another (this is logically half of an
// import, the other half being the file search/compilation step). Prefix is
// prepended to the imported variables.
bool copy_defs(istate* S, fn_namespace* dest, fn_namespace* src,
        const string& prefix, bool overwrite=true);
// switch namespaces, creating a new one if necessary.
void switch_ns(istate* S, symbol_id new_ns);


// namespace id destructuring
void ns_id_destruct(const string& global_name, string* prefix, string* stem);
// tell if sub is a subns of pkg
bool is_subns(const string& sub, const string& ns);
// Returns the suffix of sub which distinguishes it from superpackage pkg. If
// this would be the empty string, returns ".". This will not work properly if
// sub is not a subpackage of pkg.
string subns_rel_path(const string& sub, const string& ns);

}

#endif
