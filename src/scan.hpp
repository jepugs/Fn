#ifndef __FN_SCAN_HPP
#define __FN_SCAN_HPP

#include "array.hpp"
#include "base.hpp"
#include "table.hpp"

#include <iostream>
#include <fstream>
#include <memory>

namespace fn {

using sst_id = u32;

// holds all the strings needed by the scanner/parser. Keeping a separate table
// simplifies other data structures, while still keeping parsing independent of
// the garbage collector.
struct scanner_string_table {
    dyn_array<string> by_id;
    table<string,sst_id> by_name;
};

sst_id scanner_intern(scanner_string_table& pt, const string& str);
const string& scanner_name(const scanner_string_table& pt, sst_id id);

enum token_kind {
    // eof
    tk_eof,
    // paired delimiters
    tk_lbrace,
    tk_rbrace,
    tk_lbracket,
    tk_rbracket,
    tk_lparen,
    tk_rparen,
    // dollar syntax
    tk_dollar_brace,
    tk_dollar_bracket,
    tk_dollar_paren,
    tk_dollar_backtick,
    // quotation
    tk_quote,
    tk_backtick,
    tk_comma,
    tk_comma_at,
    // atoms
    tk_number,
    tk_string,
    tk_symbol
};

struct token {
    source_loc loc;
    token_kind kind;
    union {
        f64 num;
        u32 str_id;
        void* nothing;
    } d;
    token(const source_loc& loc, token_kind kind)
        : loc{loc}
        , kind{kind} {
        d.nothing = nullptr;
    }
};

class scanner {
public:
    scanner(scanner_string_table& sst, std::istream& in, int line=1, int col=0)
        : sst{&sst}
        , input{&in}
        , line{line}
        , col{col} {
    }
    ~scanner();

    token next_token();
    // tell if EOF has been reached
    bool eof();
    // check for eof after skipping whitespace
    // FIXME: this should skip comments too
    bool eof_skip_ws();
    // get stream position from running tellg on the input stream
    size_t tellg();

    scanner_string_table& get_sst();

    source_loc get_loc();

private:
    scanner_string_table* sst;
    std::istream* input;

    // these track location in input (used for generating error messages)
    int line;
    int col;

    // increment the scanner position, keeping track of lines and columns
    void advance(char ch);
    // these raise appropriate exceptions at EOF
    char get_char();
    char peek_char();

    // functions to make token objects with the proper location info
    token make_token(token_kind tk) const;
    token make_token(token_kind tk, const string& str) const;
    token make_token(token_kind tk, double num) const;

    // methods to scan variable-length tokens

    // scan a string literal
    token scan_string_literal();
    // scan a string escape sequence, writing the generated characters to buf
    void get_string_escape_char(dyn_array<char>& buf);
    // used for certain escape sequences
    void hex_digits_to_bytes(dyn_array<char>& buf, u32 num_bytes);
    void octal_to_byte(dyn_array<char>& buf, u8 first);

    // this method scans number and symbol tokens
    token scan_atom(char first); // needs first character of the token

    // Helper methods for scanning atoms. The algorithm is inspired by a state
    // machine, but written by hand. To avoid backtracking, there are pretty
    // strict restrictions on the conditions under which each method below may
    // be called. Refer to the source in src/scan.cpp for more information.
    optional<f64> try_scan_num(dyn_array<char>& buf, char first);
    optional<f64> try_scan_digits(dyn_array<char>& buf,
                                  char first,
                                  int sign,
                                  u32 base);
    optional<f64> try_scan_frac(dyn_array <char>& buf, i32* exp, u32 base);
    optional<i32> try_scan_exp(dyn_array<char>& buf);

    // throw an appropriate fn_exception
    inline void error(const char* msg) {
        throw fn_exception{"scanner", msg, source_loc{line, col-1}};
    }
};


}


#endif

