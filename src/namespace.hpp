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
    // definitions/macros indexed by their fully qualified name (FQN)
    table<symbol_id,value> def_tab;
    table<symbol_id,fn_function*> macro_tab;
    // table of namespaces by name
    table<symbol_id,fn_namespace*> ns_tab;

    ~global_env();
};

// resolve a symbol to an FQN in the given namespace. The given namespace must
// exist when the call is made, or an error is generated. If the given symbol is
// not already present in the namespace, a new entry in the resolution table is
// made.
symbol_id resolve_sym(istate* S, symbol_id ns_id, symbol_id name);
// get a global variable by its FQN. Returns false on failed lookup
bool push_global(istate* S, symbol_id fqn);
// set by FQN, creating a new definition if necessary
void set_global(istate* S, symbol_id fqn, value new_val);
// add a new namespace. If the namespace already exists, this returns the
// existing namespace.
fn_namespace* add_ns(istate* S, symbol_id ns_id);
// get a namespace, returning nullptr if it doesn't exist
fn_namespace* get_ns(istate* S, symbol_id ns_id);
// copy definitions from one namespace to another (this is logically half of an
// import, the other half being the file search/compilation step). Prefix is
// prepended to the imported variables.
bool copy_defs(istate* S, symbol_id dest_id, symbol_id src_id,
        const string& prefix, bool overwrite=true);


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
