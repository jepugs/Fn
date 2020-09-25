#include "scan.hpp"

#include <cmath>
#include <sstream>

namespace fn_scan {

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

// tell if a number/letter is a digit in the given base (e.g. in base 16, digits are [0-9A-Fa-f])
// supports base from 2 to 36
static inline bool isDigit(char c, u32 base=10) {
    char maxDigit = base < 10 ? '0'+base-1 : '9';
    if (c >= '0' && c <= maxDigit) {
        return true;
    }
    char maxCap = 'A' + base - 11;
    if (c >= 'A' && c <= maxCap) {
        return true;
    }
    char maxLow = 'a' + base - 11;
    if (c >= 'a' && c <= maxLow) {
        return true;
    }
    return false;
}

// get the value of a number/letter as an integer (e.g. '7'->7, 'b'->11)
static inline i32 digitVal(char c) {
    if (c <= 'A') {
        // number
        return c - '0';
    } else if (c < 'a') {
        // capital letter
        return c - 'A' + 10;
    } else {
        // lowercase letter
        return c - 'a' + 10;
    }
}

Scanner::~Scanner() {
    if (closeStream) {
        std::ifstream* ptr = dynamic_cast<std::ifstream*>(input);
        ptr->close();
    }
}

// increment the scanner position, keeping track of lines and columns
void Scanner::advance(char ch) {
    if (ch == '\n') {
        ++line;
        col = 0;
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

string stripEscapeChars(const string& s) {
    vector<char> buf;
    bool escaped = false;
    for (char ch : s) {
        if (escaped) {
            buf.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        buf.push_back(ch);
    }
    return string(buf.data(),buf.size());
}

static optional<f64> parseNum(const vector<char>& buf) {
    // index
    u32 i = 0;

    // check for sign
    f64 sign = 1;
    if (buf[i] == '-') {
        sign = -1;
        ++i;
    } else if (buf[i] == '+') {
        ++i;
    }

    // check for hexadecimal
    f64 base = 10;
    if (i+1 < buf.size() && buf[i] == '0' && buf[i+1] == 'x') {
        base = 16;
        i += 2;
    }

    // we need characters after the sign bit
    if (i >= buf.size()) {
        return std::nullopt;
    }

    // whether we encounter a digit
    bool digit = false;

    // get integer part
    f64 res = 0;
    char ch;
    // first integer character
    while (i < buf.size() && isDigit(ch=buf[i], base)) {
        digit = true;
        res *= base;
        res += digitVal(ch);
        ++i;
    }

    // check if we got to the end
    if (i == buf.size()) {
        return sign*res;
    }

    // check for decimal point

    // place value
    f64 place = 1/base;
    if (ch != '.') {
        // only other possibility after this is scientific notation
        goto scient;
    }
    // parse digits
    ++i;
    while (i < buf.size() && isDigit(ch=buf[i], base)) {
        digit = true;
        res += digitVal(ch) * place;
        place /= base;
        ++i;
    }

    // check if we got to the end
    if (i == buf.size()) {
        return sign*res;
    }

    // make sure there's at least one digit read
    if (!digit) {
        return { };
    }

    // label to check for scientific notation
    scient:
    // scientific notation only supports base 10
    if ((ch != 'e' && ch != 'E') || base != 10) {
        // not a number
        return { };
    }
    // parse base 10 exponent
    ++i;
    // check for sign
    f64 expSign = 1;
    if (i < buf.size()) {
        if (buf[i] == '-') {
            ++i;
            expSign = -1;
        } else if (buf[i] == '+') {
            ++i;
        }
    }

    // make sure the exponent is there
    if (i == buf.size()) {
        // numbers cannot end with e
        return std::nullopt;
    }

    f64 exponent = 0;
    while (i < buf.size() && isDigit(ch=buf[i])) {
        exponent *= 10;
        exponent += digitVal(ch);
        ++i;
    }
    // check if we got to the end
    if (i == buf.size()) {
        return sign*res*pow(10,expSign*exponent);
    }

    // this means we found an illegal character
    return std::nullopt;
}


Token Scanner::scanSymOrNum(char first) {
    if (first == '.') {
        error("Tokens may not begin with '.'.");
    }
    bool escaped = first == '\\';
    // whether this is really a symbol or a dot form
    bool dot = false;
    // whether the previous symbol was a dot
    bool prevDot = false;
    vector<char> buf;
    buf.push_back(first);

    char c;
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

        if (!isSymChar(c = peekChar())) {
            break;
        }
        getChar();
        if (c == '.') {
            if (prevDot) {
                error("Successive unescaped dots.");
            }
            dot = true;
            prevDot = true;
        } else {
            prevDot = false;
        }
        if (c == '\\') {
            escaped = true;
        }
        buf.push_back(c);
    }


    // TODO: rather than rely on stod, we shoud probably use our own number scanner
    auto d = parseNum(buf);
    if (d.has_value()) {
        return makeToken(TKNumber, *d);
    }

    if (buf[buf.size()-1] == '.') {
        error("Non-numeric tokens may not end with a dot.");
    }

    string s(buf.data(),buf.size());
    if (dot) {
        return makeToken(TKDot, s);
    }

    return makeToken(TKSymbol, stripEscapeChars(s));
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
    case '?':
        return '\?';
    case '\\':
        return '\\';
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return '\v';
    }

    error("Unrecognized string escape sequence.");
    return '\0'; // this is just to shut up the interpreter
}

bool Scanner::eof() {
    return input->peek() == EOF;
}

char Scanner::getChar() {
    if (eof()) {
        error("Unexpected EOF while scanning.");
    }
    char c = input->get();
    advance(c);
    return c;
}

char Scanner::peekChar() {
    if (eof()) {
        error("Unexpected EOF while scanning.");
    }
    return input->peek();
}


}
