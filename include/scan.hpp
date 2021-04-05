#ifndef __f_n_s_ca_n_h_pp
#define __f_n_s_ca_n_h_pp

#include "base.hpp"

#include <iostream>
#include <fstream>
#include <memory>

namespace fn_scan {

using namespace fn;

// utility function used to remove backslashes from symbols (this happens after escape code parsing)
string strip_escape_chars(const string& s);

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
    // note: symbols may include dot characters
    tk_symbol,
    // obj.key dot form
    tk_dot
};

union token_datum {
    // used for numbers. this better be 64 bits.
    f64 num;
    // used for string literals and symbol names
    string *str;
    // placeholder null pointer for other token types
    void *nothing;
};

struct token {
    token_kind tk;
    source_loc loc;
    token_datum datum;

    token() : tk(tk_eof), loc(""), datum({.nothing = nullptr}) { }
    token(token_kind tk, source_loc loc)
        : tk(tk)
        , loc(loc)
        , datum({.nothing = nullptr})
    { }
    token(token_kind tk, source_loc loc, double num)
        : tk(tk)
        , loc(loc)
        , datum({.num = num})
    { }
    token(token_kind tk, source_loc loc, const string& str)
        : tk(tk)
        , loc(loc)
        , datum({.str = new string(str)})
    { }

    // f_ix_me: it's probably inefficient to copy the whole darn string in the copy constructor/operator
    token(const token& tok)
        : tk(tok.tk)
        , loc(tok.loc)
    {
        if (tk == tk_string || tk == tk_symbol || tk == tk_dot) {
            datum.str = new string(*tok.datum.str);
        } else {
            datum = tok.datum;
        }
    }
    token& operator=(const token& tok) {
        if (this == &tok) return *this;

        // free old string if necessary
        if (tk == tk_string || tk == tk_symbol || tk == tk_dot) {
            delete datum.str;
        }

        this->tk = tok.tk;
        // copy new string if necessary
        if (tk == tk_string || tk == tk_symbol || tk == tk_dot) {
            datum.str = new string(*tok.datum.str);
        } else {
            this->datum = tok.datum;
        }
        return *this;
    }

    ~token() {
        // must free the string used by symbols and string literals
        if (tk == tk_string || tk == tk_symbol || tk == tk_dot)
            delete datum.str;
    }

    string to_string() const {
        switch (tk) {
        case tk_eof:
            return "e_of";
        case tk_lbrace:
            return "{";
        case tk_rbrace:
            return "}";
        case tk_lbracket:
            return "[";
        case tk_rbracket:
            return "]";
        case tk_lparen:
            return "(";
        case tk_rparen:
            return ")";
        case tk_dollar_backtick:
            return "$`";
        case tk_dollar_brace:
            return "${";
        case tk_dollar_bracket:
            return "$[";
        case tk_dollar_paren:
            return "$(";
        case tk_quote:
            return "'";
        case tk_backtick:
            return "`";
        case tk_comma:
            return ",";
        case tk_comma_at:
            return ",@";
        case tk_number:
            return std::to_string(this->datum.num);
        case tk_string:
            // f_ix_me: this should probably do proper escaping
            return "\"" + *(this->datum.str) + "\"";
        case tk_symbol:
        case tk_dot:
            return *(this->datum.str);
        }
        // this is unreachable code to silence a compiler warning
        return "";
    }
};


class scanner {
public:
    scanner(std::istream* in, const string& filename="", int line=1, int col=0)
        : input(in)
        , filename(new string(filename))
        , line(line)
        , col(col)
    { }
    scanner(const string& filename)
        : line(1)
        , col(0)
    {
        input = new std::ifstream(filename, std::ios_base::in);
        close_stream = true;
    }
    ~scanner();

    token next_token();


private:
    std::istream *input;
    // if true, the stream is closed when the scanner ends
    bool close_stream = false;

    // these track location in input (used for generating error messages)
    std::shared_ptr<string> filename;
    int line;
    int col;

    // increment the scanner position, keeping track of lines and columns
    void advance(char ch);

    // methods to scan variable-length tokens
    token scan_sym_or_num(char first); // needs first character of the token
    token scan_string_literal();
    // scan a string escape sequence and return the corresponding character
    char get_string_escape_char();

    // tell if e_of has been reached
    bool eof();
    // these raise appropriate exceptions at e_of
    char get_char();
    char peek_char();

    // functions to make token objects with the proper location info
    token make_token(token_kind tk);
    token make_token(token_kind tk, string str);
    token make_token(token_kind tk, double num);

    // throw an appropriate fn_error
    inline void error(const char* msg) {
        throw fn_error("scanner", msg, source_loc(filename, line, col-1));
    }
};

}


#endif

