#ifndef __FN_PARSE_HPP
#define __FN_PARSE_HPP

#include "base.hpp"
#include "scan.hpp"
#include "values.hpp"

namespace fn {

namespace ast {

using namespace fn;

enum ast_kind {
    ak_number,
    ak_string,
    ak_symbol,
    ak_list
};

// a node in the abstract syntax tree
struct node {
    source_loc loc;
    ast_kind kind;
    u32 list_length; // only used for list nodes
    union {
        f64 num;
        // this is w/r/t to an associated scanner_string_table
        symbol_id str_id;
        // node structure does not take
        node** list;
    } datum;

    explicit node(const source_loc& loc, ast_kind k, f64 num);
    explicit node(const source_loc& loc, ast_kind k, u32 str_id);
    // this takes ownership of list. It will be freed using delete[]
    explicit node(const source_loc& loc, ast_kind k, u32 list_length,
            node** list);

    node& operator=(const node& other) = delete;
    node(const node& other) = delete;
};

// functions to create ast nodes. These do allocation with new and must be
// freed later
node* mk_number(const source_loc& loc, f64 num);
node* mk_string(const source_loc& loc, u32 str_id);
node* mk_symbol(const source_loc& loc, u32 str_id);
// This will take ownership of the argument lst (i.e. it will be freed when
// free_node() is called)
node* mk_list(const source_loc& loc, u32 list_length, node** lst);
// This creates a new array by copying the contents of lst
node* mk_list(const source_loc& loc, const dyn_array<node*>& lst);

// make a deep copy of an AST
node* copy_graph(const node* root);
// free up a node allocated by one of the mk_*() functions
void free_graph(node* root);

} // end namespace fn::ast

struct istate;

// The parser class encapsulates all the logic used to parse a single ast node
class parser {
private:
    friend ast::node* parse_next_node(istate* S, scanner& sc, bool* resumable);

    // the istate is used for error generation only
    istate* S;
    scanner_string_table* sst;
    scanner* sc;
    // set when an error occurs
    bool err_resumable;

    parser(istate* S, scanner& sc);
    ast::node* parse();

    // parse until a token of the specified kind is encountered, adding forms to
    // the buffer along the way.
    bool parse_to_delimiter(dyn_array<ast::node*>& buf, token_kind delim);
    // parse with one lookahead token
    ast::node* parse_la(const token& t0);
    // create a list of length two whose first expression is the symbol
    // described by op and whose second expression is the next node. Takes a
    // lookahead token
    ast::node* parse_prefix(const source_loc& loc, const string& op,
            const token& t0);
};

// Create an ast::node by parsing input from a scanner. On failure, returns
// nullptr, sets an istate error, and also sets the value of *resumable. A true
// value indicates that the error was caused by EOF. (This allows the REPL to
// detect unfinished expressions). A false value is for an unrecoverable error.
ast::node* parse_next_node(istate* S, scanner& sc, bool* resumable);

// parse all available expressions from a string
dyn_array<ast::node*> parse_string(istate* S, scanner_string_table& sst,
        const string& str);

// parse all available expressions from a stream
dyn_array<ast::node*> parse_stream(istate* S, scanner_string_table& sst,
        std::istream& in); 

// pop a value from the top of the stack and convert it into an ast::node*. Sets
// istate error on failure. The created value must be freed manually.
ast::node* pop_syntax(istate* S, scanner_string_table& sst,
        const source_loc& loc);

}
#endif
