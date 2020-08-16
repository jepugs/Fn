#include "scan.hpp"

#include <fstream>
#include <sstream>

namespace fn_scan {

using namespace std;

void ScanState::advance(char ch) {
    if (ch == '\n') {
        ++line;
        col = 1;
    } else {
        ++col;
    }
}

void ScanState::pushToken(TokenKind tk) {
    // apologies for the ratchet printf debugging
    //std::cout << "putting token of kind " << tk << endl;
    output->push_back(Token(tk, CodeLoc(filename, line, col)));
}
void ScanState::pushToken(TokenKind tk, string str) {
    //std::cout << "putting token of kind " << tk << endl;
    output->push_back(Token(tk, CodeLoc(filename, line, col), str));
}
void ScanState::pushToken(TokenKind tk, double num) {
    //std::cout << "putting token of kind " << tk << endl;
    output->push_back(Token(tk, CodeLoc(filename, line, col), num));
}

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

void ScanState::partialScan() {
    char c = getChar();
    if (isWS(c)) {
        return;
    }
    switch (c) {
    case ';': // comment
        while (getChar() != '\n');
        break;

    // paired delimiters
    case '{':
        pushToken(TKLBrace);
        break;
    case '}':
        pushToken(TKRBrace);
        break;
    case '[':
        pushToken(TKLBracket);
        break;
    case ']':
        pushToken(TKRBracket);
        break;
    case '(':
        pushToken(TKLParen);
        break;
    case ')':
        pushToken(TKRParen);
        break;

    // quotation
    case '\'':
        pushToken(TKQuote);
        break;
    case '`':
        pushToken(TKBacktick);
        break;
    case ',':
        // check if next character is @
        // IMPLNOTE: an EOF at this point would be a syntax error, but we let it slide up to the
        // parser for the sake of better error generation
        if (!eof() && peekChar() == '@') {
            getChar();
            pushToken(TKCommaSplice);
        } else {
            pushToken(TKComma);
        }
        break;

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
            pushToken(TKDollarBacktick);
            break;
        case '{':
            pushToken(TKDollarBrace);
            break;
        case '[':
            pushToken(TKDollarBracket);
            break;
        case '(':
            pushToken(TKDollarParen);
            break;
        }
        break;

    // string literals
    case '"':
        scanstringLiteral();
        break;

    // symbol or number
    default:
        scanSymOrNum(c);
    }
}


void ScanState::scan() {
    while(!eof()) {
        partialScan();
    }
    pushToken(TKEOF);
}

void ScanState::scanSymOrNum(char first) {
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


    string s(buf.data());
    // TODO: rather than rely on stod, we shoud probably use our own number scanner
    double d;
    try {
        d = stod(s);
        pushToken(TKNumber, d);
    } catch(...) { // TODO: handle out_of_range
        pushToken(TKSymbol, s);
    }
}

void ScanState::scanstringLiteral() {
    char c = getChar();
    vector<char> buf;

    while (c != '"') {
        if (c == '\\') {
            c = getstringEscapeChar();
        }
        buf.push_back(c);
        c = getChar();
    }

    pushToken(TKString, string(buf.data()));
}

char ScanState::getstringEscapeChar() {
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
                  CodeLoc(filename, line, col));
}

bool ScanState::eof() {
    return input->peek() == EOF;
}

char ScanState::getChar() {
    if (eof()) {
        throw FNError("scanner", "Unexpected EOF while scanning.",
                      CodeLoc(filename, line, col));
    }
    char c = input->get();
    advance(c);
    return c;
}

char ScanState::peekChar() {
    if (eof()) {
        throw FNError("scanner", "Unexpected EOF while scanning.",
                      CodeLoc(filename, line, col));
    }
    return input->peek();
}

vector<Token> scan(istream& input) {
    vector<Token> output;
    ScanState ss(&input, &output);
    ss.scan();
    return output;
}

vector<Token> scan(const string& filename) {
    vector<Token> output;
    ifstream input(filename.c_str(), ios_base::in);
    ScanState ss(&input, &output);
    ss.scan();
    return output;
}

vector<Token> scanstring(const string& str) {
    istringstream input(str);
    return scan(input);
}


}
