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
    ak_number_atom,
    ak_string_atom,
    ak_symbol_atom,
    ak_list
};

struct ast_form {
    source_loc loc;
    ast_kind kind;
    u32 list_length; // only used for list nodes
    union {
        f64 num;
        fn_string* str;
        symbol_id sym;
        ast_form** list;
    } datum;

    // make a deep copy. Must be deleted later
    ast_form* copy() const;

    string as_string(const symbol_table* symtab) const;

    bool is_symbol() const;
    bool is_keyword(const symbol_table* symtab) const;
    symbol_id get_symbol() const;
};

ast_form* mk_number_form(source_loc loc, f64 num, ast_form* dest=nullptr);
ast_form* mk_string_form(source_loc loc,
        const fn_string& str,
        ast_form* dest=nullptr);
ast_form* mk_string_form(source_loc loc,
        fn_string&& str,
        ast_form* dest=nullptr);
ast_form* mk_symbol_form(source_loc loc,
        symbol_id sym,
        ast_form* dest=nullptr);
ast_form* mk_list_form(source_loc loc,
        u32 list_length,
        ast_form** list,
        ast_form* dest=nullptr);
// make a list form by copying the contents of a vector
ast_form* mk_list_form(source_loc loc,
        vector<ast_form*>& lst,
        ast_form* dest=nullptr);

void clear_ast_form(ast_form* form, bool recursive=true);
void free_ast_form(ast_form* form, bool recursive=true);

struct parse_error {
    source_loc origin;
    bool resumable = false;
    string message;
};

// get the next form by reading tokens one at a time from the scanner. Return a
// null pointer on EOF. It is the responsibility of the caller to delete the
// returned object. Returns null and sets err on failure.
ast_form* parse_input(scanner* sc,
        symbol_table* symtab,
        parse_error* err);
// This is the same as above, but we pass in the first token directly (as
// opposed to getting it from the scanner).
ast_form* parse_input(scanner* sc,
        symbol_table* symtab,
        token t0,
        parse_error* err);

// attempt to parse as many ast_forms as possible from the given string. The
// parse_error* structure is set to the first error encountered.
vector<ast_form*> parse_string(const string& src,
        symbol_table* symtab,
        u32* bytes_used,
        parse_error* err);

}
#endif
