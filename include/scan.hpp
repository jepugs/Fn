#ifndef __FN_SCAN_HPP
#define __FN_SCAN_HPP

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
    string* str;
    // used for dots
    vector<string>* ids;
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
        , datum({.nothing = nullptr}) {
    }
    token(token_kind tk, source_loc loc, double num)
        : tk(tk)
        , loc(loc)
        , datum({.num = num}) {
    }
    token(token_kind tk, source_loc loc, const string& str)
        : tk(tk)
        , loc(loc)
        , datum({.str = new string(str)}) {
    }
    token(token_kind tk, source_loc loc, const vector<string>& ids)
        : tk(tk)
        , loc(loc)
        , datum({.ids = new vector<string>(ids)}) {
    }

    token(const token& tok)
        : tk(tok.tk)
        , loc(tok.loc) {
        if (tk == tk_string || tk == tk_symbol) {
            datum.str = new string(*tok.datum.str);
        } else if (tk == tk_dot) {
            datum.ids = new vector<string>(*tok.datum.ids);
        } else {
            datum = tok.datum;
        }
    }
    token& operator=(const token& tok) {
        if (this == &tok) return *this;

        // free old string if necessary
        if (tk == tk_string || tk == tk_symbol) {
            delete datum.str;
        } else if (tk == tk_dot) {
            delete datum.ids;
        }

        this->tk = tok.tk;
        // copy new string if necessary
        if (tk == tk_string || tk == tk_symbol) {
            datum.str = new string(*tok.datum.str);
        } else if (tk == tk_dot) {
            datum.ids = new vector<string>(*tok.datum.ids);
        } else {
            this->datum = tok.datum;
        }
        return *this;
    }

    ~token() {
        // must free the string used by symbols and string literals
        if (tk == tk_string || tk == tk_symbol) {
            delete datum.str;
        } else if (tk == tk_dot) {
            delete datum.ids;
        }
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
        , col(col) {
    }
    scanner(const string& filename)
        : line(1)
        , col(0) {
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
    token scan_atom(char first); // needs first character of the token
    token scan_sym_or_num(char first); // needs first character of the token
    token scan_dot(string first); // needs first string of the token
    token scan_string_literal();
    // scan a string escape sequence and return the corresponding character
    char get_string_escape_char();

    // Scanner state machine: these functions act as entry points for various
    // points in a hand-programmed state machine that processes symbols,
    // numbers, and dots. There are strict requirements on the situations in
    // which these may be called. See src/scan.cpp for details.
    token scan_num_state(vector<char>& buf, char first, int sign);
    token scan_digit_state(vector<char>& buf,
                           optional<char> first,
                           int sign,
                           u32 base);
    token scan_frac_state(f64 integral, int sign, u32 base);
    token scan_sym_state(vector<char>& buf);

    // tell if e_of has been reached
    bool eof();
    // these raise appropriate exceptions at e_of
    char get_char();
    char peek_char();

    // functions to make token objects with the proper location info
    token make_token(token_kind tk) const;
    token make_token(token_kind tk, const string& str) const;
    token make_token(token_kind tk, double num) const;
    token make_token(token_kind tk, const vector<string>& ids) const;

    // throw an appropriate fn_error
    inline void error(const char* msg) {
        throw fn_error("scanner", msg, source_loc(filename, line, col-1));
    }
};

}


#endif

