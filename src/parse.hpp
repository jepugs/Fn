#ifndef __FN_PARSE_HPP
#define __FN_PARSE_HPP

#include "base.hpp"
#include "istate.hpp"
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
        string* str;
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
        const string& str,
        ast_form* dest=nullptr);
ast_form* mk_string_form(source_loc loc,
        string&& str,
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
        dyn_array<ast_form*>* lst,
        ast_form* dest=nullptr);

void clear_ast_form(ast_form* form, bool recursive=true);
void free_ast_form(ast_form* form, bool recursive=true);


// get the next form by reading tokens one at a time from the scanner. Return a
// null pointer on EOF. It is the responsibility of the caller to delete the
// returned object. Returns null and sets err on failure.

// If the parse failed irrecoverably, sets *resumable = false. If the parse
// could succeed by adding additional characters to the end of the input, sets
// *resumable = true and *bytes_used to the number of bytes used after the last
// successful parse. This is mainly for the REPL.
ast_form* parse_next_form(scanner* sc,
        symbol_table* symtab,
        bool* resumable,
        fault* err);

// This is the same as above, but we pass in the first token directly (as
// opposed to getting it from the scanner). Used for prefix operators.
ast_form* parse_next_form(scanner* sc,
        symbol_table* symtab,
        token t0,
        bool* resumable,
        fault* err);

// Attempt to parse as many ast_forms as possible from the given scanner.
dyn_array<ast_form*> parse_from_scanner(scanner* sc,
        symbol_table* symtab,
        fault* err);

// Attempt to parse as many ast_forms as possible from the given string. The
// parse_error* structure is set to the first error encountered.
dyn_array<ast_form*> parse_string(const string& src,
        symbol_table* symtab,
        fault* err);

// Parse AST forms from in until an error is encountered
dyn_array<ast_form*> parse_input(std::istream* in,
        symbol_table* symtab,
        fault* err);

// This sets *resumable the same way as in parse_next_form. In addition, on
// error, it sets *bytes_used to be the number of bytes consumed after the last
// successful parse. This is used for detecting the ends of expressions at the
// REPL to enable multi-line input and multiple expressions per line.
dyn_array<ast_form*> partial_parse_input(scanner* sc,
        symbol_table* symtab,
        u32* bytes_used,
        bool* resumable,
        fault* err);

// create an ast_form* from the top of the stack
ast_form* pop_syntax(istate* S, const source_loc& loc);

}
#endif
