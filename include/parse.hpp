#ifndef __FN_PARSE_HPP
#define __FN_PARSE_HPP

#include "base.hpp"
#include "scan.hpp"
#include "values.hpp"

namespace fn_parse {

using namespace fn;
using namespace fn_scan;

// note: ak_error currently unused. In the future it will be associated with a
// string
enum ast_kind {
    ak_atom,
    ak_error,
    ak_list
};

enum atom_type {
    at_number,
    at_string,
    at_symbol
};

struct ast_atom  {
    atom_type type;
    union {
        f64 num;
        fn_string* str;
        symbol_id sym;
    } datum;

    ast_atom(f64 num);
    ast_atom(const fn_string& str);
    ast_atom(fn_string&& str);
    ast_atom(const symbol& sym);

    // this will copy the string if necessary
    ast_atom(const ast_atom& at);
    // copy operator
    ast_atom& operator=(const ast_atom& at);

    // rvalue move operations
    ast_atom(ast_atom&& at);
    ast_atom& operator=(ast_atom&& at);

    // string is automatically freed
    ~ast_atom();
};

struct ast_node {
    source_loc loc;
    ast_kind kind;
    union {
        ast_atom* atom;
        vector<ast_node*>* list;
    } datum;

    ast_node(const source_loc& loc); // makes an error node
    ast_node(const ast_atom& at, const source_loc& loc);
    // the vector here is copied
    ast_node(const vector<ast_node*>& list, const source_loc& loc);

    // shallow copy by default
    ast_node(const ast_node&) = default;

    ~ast_node();

    // make a deep copy. Must be deleted later
    ast_node* copy() const;

    string as_string(const symbol_table& symtab) const;

    bool is_symbol() const;
    bool is_keyword(const symbol_table& symtab) const;
    const symbol& get_symbol(const symbol_table& symtab) const;
    symbol_id get_symbol_id(const symbol_table& symtab) const;
};

// get the next form by reading tokens one at a time from the scanner. Return a
// null pointer on EOF. It is the responsibility of the caller to delete the
// returned object.
ast_node* parse_node(scanner& sc,
                     symbol_table& symtab,
                     optional<token> t0 = std::nullopt);

struct parameter {
    symbol_id sym;
    ast_node* init_form;

    // a deep copy is made of init if provided
    parameter(symbol_id sym, const ast_node* init=nullptr)
        : sym{sym}
        , init_form{nullptr} {
        if (init) {
            init_form = init->copy();
        }
    }

    parameter(const parameter& src)
        : sym{src.sym}
        , init_form{src.init_form==nullptr ? nullptr : src.init_form->copy()} {
    }
};

// TODO: replace positional with a vector of symbol ids, a req_args field, and a
// separate vector of initforms. This will remove the need for the parameter
// structure.
struct param_list {
    vector<parameter> positional;
    optional<symbol_id> var_list;
    optional<symbol_id> var_table;

    param_list() = default;
    param_list(const vector<parameter>& positional,
               const optional<symbol_id>& var_list=std::nullopt,
               const optional<symbol_id>& var_table=std::nullopt)
        : positional{positional}
        , var_list{var_list}
        , var_table{var_table} {
    }

    ~param_list() = default;
};

// Parse an ast_node into a param_list. In the process, we check for syntactic
// validity. This function does not verify whether the parameter names are
// legal, nor does it check for duplicates. (That is done in the compiler).
param_list parse_params(symbol_table& symtab, const ast_node& form);

}
#endif
