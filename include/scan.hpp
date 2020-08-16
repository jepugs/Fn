#ifndef __FN_SCAN_HPP
#define __FN_SCAN_HPP

#include "base.hpp"

#include <iostream>
#include <memory>
#include <vector>

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
    const TokenKind tk;
    const CodeLoc loc;
    TokenDatum datum;

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
        if (this != &tok && (tk == TKString || tk == TKSymbol)) {
            delete datum.str;
            datum.str = new string(*tok.datum.str);
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
        case TKSymbol:
            return *(this->datum.str);
        }
    }
};

// Object to maintain intermediate state as we scan input.
class ScanState {
    // All ScanState functionality is encapsulated by these functions
    friend vector<Token> scan(istream& input);
    friend vector<Token> scan(const string& filename);

    private:
    // stream to scan from
    istream* input;
    // vector to push tokens to
    // IMPLNOTE: this should probably be a queue, since we never use random access
    vector<Token>* output;

    // track location in input
    shared_ptr<string> filename;
    int line;
    int col;

    ScanState(istream* input, vector<Token>* output, const string& filename="", int line=1,
              int col=1)
        : input(input), output(output), filename(new string(filename)), line(line), col(col) { }

    // update the line and/or column numbers (dependent on if ch = '\n')
    void advance(char ch);
    // get a CodeLoc object for the current location in the stream
    // TODO: refactor implementation code to use this
    CodeLoc curLoc();
    // append a token to the output vector
    void pushToken(TokenKind tk);
    void pushToken(TokenKind tk, string str);
    void pushToken(TokenKind tk, double num);
    // process at least one character. May or may not generate a token. Exception on EOF
    void partialScan();
    // call partialScan until EOF
    void scan();

    // methods to scan variable-length tokens
    void scanSymOrNum(char first); // needs first character of the token
    void scanstringLiteral();

    // scan a string escape sequence and return the corresponding character
    char getstringEscapeChar();

    // tell if EOF has been reached
    bool eof();
    // these raise appropriate exceptions at EOF
    char getChar();
    char peekChar();
};


// scan an istream
vector<Token> scan(istream& input);
// scan the contents of a file
vector<Token> scan(const string& filename);
// scan tokens from a string
vector<Token> scanstring(const string& str);


// unfinished debugging facility
inline void printTokens(vector<Token> toks) {
    int indent = 0;
    for (auto t : toks) {
        switch (t.tk) {
        case TKEOF:
            std::cout << "\nEOF\n";
            break;
        case TKLBrace:
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            cout << "{ ";
            indent += 2;
            break;
        case TKRBrace:
            cout << " }";
            indent -= 2;
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            break;
        case TKLBracket:
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            cout << "[ ";
            indent += 2;
            break;
        case TKRBracket:
            cout << " ]";
            indent -= 2;
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            break;
        case TKLParen:
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            cout << "( ";
            indent += 2;
            break;
        case TKRParen:
            cout << " )";
            indent -= 2;
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            break;
        case TKDollarBrace:
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            cout << "${ ";
            indent += 2;
            break;
        case TKDollarBracket:
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            cout << "$[ ";
            indent += 2;
            break;
        case TKDollarParen:
            if (indent > 0) {
                cout << endl;
                for (int i = 0; i < indent; ++i) cout << ' ';
            }
            cout << "$( ";
            indent += 2;
            break;
        case TKDollarBacktick:
            cout << "$`";
            break;
        default:
            break;
        }
    }
}

}

#endif

