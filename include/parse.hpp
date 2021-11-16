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
    ak_list,
    ak_error
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

    string as_string(const symbol_table& symtab) const;

    bool is_symbol() const;
    bool is_keyword(const symbol_table& symtab) const;
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
        const vector<ast_form*>& lst,
        ast_form* dest=nullptr);

void free_ast_form(ast_form* form, bool recursive=true);

ast_form* copy_form();

// get the next form by reading tokens one at a time from the scanner. Return a
// null pointer on EOF. It is the responsibility of the caller to delete the
// returned object.
ast_form* parse_form(scanner& sc,
        symbol_table& symtab,
        optional<token> t0 = std::nullopt);

}
#endif
