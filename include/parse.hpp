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

    // copy constructor
    ast_atom(const ast_atom& at);
    // copy operator
    ast_atom& operator=(const ast_atom& at);

    // rvalue move operations
    ast_atom(ast_atom&& at);
    ast_atom& operator=(ast_atom&& at);

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

    ~ast_node();

    string as_string(const symbol_table* symtab);

    bool is_symbol();
};

// get the next form by reading tokens one at a time from the scanner. Return a
// null pointer on EOF. It is the responsibility of the caller to delete the
// returned object.
ast_node* parse_node(scanner* sc,
                     symbol_table* symtab,
                     optional<token> t0 = std::nullopt);

}

#endif
