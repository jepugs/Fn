#include "scan.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace fn_scan {

using namespace std;


// is whitespace
static inline bool isWS(char c) {
    switch (c) {
    case ' ':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
    case '\v':
        return true;
    }
    return false;
}

// is symbol constituent character
static inline bool isSymChar(char c) {
    if (isWS(c)) return false;

    switch (c) {
    case '(':
    case ')':
    case '{':
    case '}':
    case '[':
    case ']':
    case '"':
    case '\'':
    case '`':
    case ',':
    case ';':
        return false;
    }

    return true;
}

Scanner::~Scanner() {
    if (closeStream) {
        ifstream* ptr = dynamic_cast<ifstream*>(input);
        ptr->close();
    }
}

// increment the scanner position, keeping track of lines and columns
void Scanner::advance(char ch) {
    if (ch == '\n') {
        ++line;
        col = 1;
    } else {
        ++col;
    }
}

Token Scanner::makeToken(TokenKind tk) {
    return Token(tk, SourceLoc(filename, line, col));
}
Token Scanner::makeToken(TokenKind tk, string str) {
    return Token(tk, SourceLoc(filename, line, col), str);
}
Token Scanner::makeToken(TokenKind tk, double num) {
    return Token(tk, SourceLoc(filename, line, col), num);
}

// this is the main scanning function
Token Scanner::nextToken() {
    while (!eof()) {
        auto c = getChar();
        if (isWS(c)) {
            continue;
        }
        switch (c) {
        case ';': // comment
            while (getChar() != '\n');
            break;

        // paired delimiters
        case '{':
            return makeToken(TKLBrace);
        case '}':
            return makeToken(TKRBrace);
        case '[':
            return makeToken(TKLBracket);
        case ']':
            return makeToken(TKRBracket);
        case '(':
            return makeToken(TKLParen);
        case ')':
            return makeToken(TKRParen);

        // quotation
        case '\'':
            return makeToken(TKQuote);
        case '`':
            return makeToken(TKBacktick);
        case ',':
            // check if next character is @
            // IMPLNOTE: an EOF at this point would be a syntax error, but we let it slide up to the
            // parser for the sake of better error generation
            if (!eof() && peekChar() == '@') {
                getChar();
                return makeToken(TKCommaSplice);
            } else {
                return makeToken(TKComma);
            }

        // dollar sign
        case '$':
            // IMPLNOTE: unlike the case for unquote, eof here could still result in a syntactically
            // valid (albeit probably dumb) program
            if (eof()) {
                scanSymOrNum(c);
                break;
            }
            c = peekChar();
            switch (c) {
            case '`':
                return makeToken(TKDollarBacktick);
                break;
            case '{':
                return makeToken(TKDollarBrace);
                break;
            case '[':
                return makeToken(TKDollarBracket);
                break;
            case '(':
                return makeToken(TKDollarParen);
                break;
            }
            break;

            // string literals
        case '"':
            return scanStringLiteral();
            break;

            // symbol or number
        default:
            return scanSymOrNum(c);
        }
    }
    // if we get here, we encountered EOF
    return makeToken(TKEOF);
}


Token Scanner::scanSymOrNum(char first) {
    bool escaped = first == '\\';
    vector<char> buf;
    buf.push_back(first);

    while(true) {
        if (escaped) {
            // IMPLNOTE: this throws an exception at EOF, which is the desired behavior
            buf.push_back(getChar());
            escaped = false;
            continue;
        }
        if (eof()) {
            // it's fine for eof to terminate a symbol unless the last character is an escape
            break;
        }

        char c = peekChar();
        if (!isSymChar(c)) {
            break;
        }

        getChar();
        if (c == '\\') {
            escaped = true;
        } else {
            buf.push_back(c);
        }
    }


    string s(buf.data(),buf.size());
    // TODO: rather than rely on stod, we shoud probably use our own number scanner
    double d;
    try {
        d = stod(s);
        return makeToken(TKNumber, d);
    } catch(...) { // TODO: handle out_of_range
        return makeToken(TKSymbol, s);
    }
}

Token Scanner::scanStringLiteral() {
    char c = getChar();
    vector<char> buf;

    while (c != '"') {
        if (c == '\\') {
            c = getStringEscapeChar();
        }
        buf.push_back(c);
        c = getChar();
    }

    return makeToken(TKString, string(buf.data(),buf.size()));
}

char Scanner::getStringEscapeChar() {
    // TODO: implement multi-character escape sequences
    char c = getChar();
    switch (c) {
    case '\'':
        return '\'';
    case '\"':
        return '\"';
    case '\?':
        return '\?';
    case '\\':
        return '\\';
    case '\a':
        return '\a';
    case '\b':
        return '\b';
    case '\f':
        return '\f';
    case '\n':
        return '\n';
    case '\r':
        return '\r';
    case '\t':
        return '\t';
    case '\v':
        return '\v';
    }

    throw FNError("scanner", "Unrecognized string escape sequence",
                  SourceLoc(filename, line, col));
}

bool Scanner::eof() {
    return input->peek() == EOF;
}

char Scanner::getChar() {
    if (eof()) {
        throw FNError("scanner", "Unexpected EOF while scanning.",
                      SourceLoc(filename, line, col));
    }
    char c = input->get();
    advance(c);
    return c;
}

char Scanner::peekChar() {
    if (eof()) {
        throw FNError("scanner", "Unexpected EOF while scanning.",
                      SourceLoc(filename, line, col));
    }
    return input->peek();
}


}
