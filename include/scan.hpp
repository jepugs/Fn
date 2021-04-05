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
    t_ke_of,
    // paired delimiters
    t_kl_brace,
    t_kr_brace,
    t_kl_bracket,
    t_kr_bracket,
    t_kl_paren,
    t_kr_paren,
    // dollar syntax
    t_kdollar_brace,
    t_kdollar_bracket,
    t_kdollar_paren,
    t_kdollar_backtick,
    // quotation
    t_kquote,
    t_kbacktick,
    t_kcomma,
    t_kcomma_splice,
    // atoms
    t_knumber,
    t_kstring,
    // note: symbols may include dot characters
    t_ksymbol,
    // obj.key dot form
    t_kdot
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

    token() : tk(t_ke_of), loc(""), datum({.nothing = nullptr}) { }
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
        if (tk == t_kstring || tk == t_ksymbol || tk == t_kdot) {
            datum.str = new string(*tok.datum.str);
        } else {
            datum = tok.datum;
        }
    }
    token& operator=(const token& tok) {
        if (this == &tok) return *this;

        // free old string if necessary
        if (tk == t_kstring || tk == t_ksymbol || tk == t_kdot) {
            delete datum.str;
        }

        this->tk = tok.tk;
        // copy new string if necessary
        if (tk == t_kstring || tk == t_ksymbol || tk == t_kdot) {
            datum.str = new string(*tok.datum.str);
        } else {
            this->datum = tok.datum;
        }
        return *this;
    }

    ~token() {
        // must free the string used by symbols and string literals
        if (tk == t_kstring || tk == t_ksymbol || tk == t_kdot)
            delete datum.str;
    }

    string to_string() const {
        switch (tk) {
        case t_ke_of:
            return "e_of";
        case t_kl_brace:
            return "{";
        case t_kr_brace:
            return "}";
        case t_kl_bracket:
            return "[";
        case t_kr_bracket:
            return "]";
        case t_kl_paren:
            return "(";
        case t_kr_paren:
            return ")";
        case t_kdollar_backtick:
            return "$`";
        case t_kdollar_brace:
            return "${";
        case t_kdollar_bracket:
            return "$[";
        case t_kdollar_paren:
            return "$(";
        case t_kquote:
            return "'";
        case t_kbacktick:
            return "`";
        case t_kcomma:
            return ",";
        case t_kcomma_splice:
            return ",@";
        case t_knumber:
            return std::to_string(this->datum.num);
        case t_kstring:
            // f_ix_me: this should probably do proper escaping
            return "\"" + *(this->datum.str) + "\"";
        case t_ksymbol:
        case t_kdot:
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

