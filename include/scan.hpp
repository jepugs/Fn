#ifndef __FN_SCAN_HPP
#define __FN_SCAN_HPP

#include "base.hpp"

#include <iostream>
#include <fstream>
#include <memory>

namespace fn_scan {

using namespace std;
using namespace fn;

enum TokenKind {
    // eof
    TKEOF,
    // paired delimiters
    TKLBrace,
    TKRBrace,
    TKLBracket,
    TKRBracket,
    TKLParen,
    TKRParen,
    // dollar syntax
    TKDollarBrace,
    TKDollarBracket,
    TKDollarParen,
    TKDollarBacktick,
    // quotation
    TKQuote,
    TKBacktick,
    TKComma,
    TKCommaSplice,
    // atoms
    TKNumber,
    TKString,
    // note: symbols may include dot characters
    TKSymbol
};

union TokenDatum {
    // used for numbers. This better be 64 bits.
    f64 num;
    // used for string literals and symbol names
    string *str;
    // placeholder null pointer for other token types
    void *nothing;
};

struct Token {
    TokenKind tk;
    CodeLoc loc;
    TokenDatum datum;

    Token() : tk(TKNumber), loc(new string(""),1,1), datum({ .num=0 }) { }
    Token(TokenKind tk, CodeLoc loc)
        : tk(tk), loc(loc), datum({.nothing = NULL}) { }
    Token(TokenKind tk, CodeLoc loc, double num)
        : tk(tk), loc(loc), datum({.num = num}) { }
    Token(TokenKind tk, CodeLoc loc, const string& str)
        : tk(tk), loc(loc), datum({.str = new string(str)}) { }

    // FIXME: it's probably inefficient to copy the whole darn string in the copy constructor/operator
    Token(const Token& tok) : tk(tok.tk), loc(tok.loc) {
        if (tk == TKString || tk == TKSymbol) {
            datum.str = new string(*tok.datum.str);
        } else {
            datum = tok.datum;
        }
    }
    Token& operator=(const Token& tok) {
        if (this == &tok) return *this;

        // free old string if necessary
        if (tk == TKString || tk == TKSymbol) {
            delete datum.str;
        }

        this->tk = tok.tk;
        // copy new string if necessary
        if (tk == TKString || tk == TKSymbol) {
            datum.str = new string(*tok.datum.str);
        } else {
            this->datum = tok.datum;
        }
        return *this;
    }

    ~Token() {
        // must free the string used by symbols and string literals
        if (tk == TKString || tk == TKSymbol)
            delete datum.str;
    }

    string to_string() {
        switch (tk) {
        case TKEOF:
            return "EOF";
        case TKLBrace:
            return "{";
        case TKRBrace:
            return "}";
        case TKLBracket:
            return "[";
        case TKRBracket:
            return "]";
        case TKLParen:
            return "(";
        case TKRParen:
            return ")";
        case TKDollarBacktick:
            return "$`";
        case TKDollarBrace:
            return "${";
        case TKDollarBracket:
            return "$[";
        case TKDollarParen:
            return "$(";
        case TKQuote:
            return "'";
        case TKBacktick:
            return "`";
        case TKComma:
            return ",";
        case TKCommaSplice:
            return ",@";
        case TKNumber:
            return std::to_string(this->datum.num);
        case TKString:
            // FIXME: this should probably do proper escaping
            return "\"" + *(this->datum.str) + "\"";
        case TKSymbol:
            return *(this->datum.str);
        }
        // this is unreachable code to silence a compiler warning
        return "";
    }
};


class Scanner {
public:
    Scanner(istream* in, const string& filename="", int line=1, int col=1)
        : input(in), filename(new string(filename)), line(line), col(col) { }
    Scanner(const string& filename)
        : line(1), col(1) {
        input = new ifstream(filename, ios_base::in);        
        this->closeStream = true;
    }
    ~Scanner();

    Token nextToken();


private:
    istream *input;
    // if true, the stream is closed when the scanner ends
    bool closeStream = false;

    // these track location in input (used for generating error messages)
    shared_ptr<string> filename;
    int line;
    int col;

    // increment the scanner position, keeping track of lines and columns
    void advance(char ch);

    // methods to scan variable-length tokens
    Token scanSymOrNum(char first); // needs first character of the token
    Token scanStringLiteral();
    // scan a string escape sequence and return the corresponding character
    char getStringEscapeChar();

    // tell if EOF has been reached
    bool eof();
    // these raise appropriate exceptions at EOF
    char getChar();
    char peekChar();

    // functions to make token objects with the proper location info
    Token makeToken(TokenKind tk);
    Token makeToken(TokenKind tk, string str);
    Token makeToken(TokenKind tk, double num);
};


}

#endif

