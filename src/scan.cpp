#include "scan.hpp"

#include <cmath>
#include <sstream>

namespace fn_scan {

// is whitespace
static inline bool is_ws(char c) {
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
static inline bool is_sym_char(char c) {
    if (is_ws(c)) return false;

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

// tell if a number/letter is a digit in the given base (e.g. in base 16, digits are [0-9a-fa-f])
// supports base from 2 to 36
static inline bool is_digit(char c, u32 base=10) {
    char max_digit = base < 10 ? '0'+base-1 : '9';
    if (c >= '0' && c <= max_digit) {
        return true;
    }
    char max_cap = 'a' + base - 11;
    if (c >= 'a' && c <= max_cap) {
        return true;
    }
    char max_low = 'a' + base - 11;
    if (c >= 'a' && c <= max_low) {
        return true;
    }
    return false;
}

// get the value of a number/letter as an integer (e.g. '7'->7, 'b'->11)
static inline i32 digit_val(char c) {
    if (c <= 'a') {
        // number
        return c - '0';
    } else if (c < 'a') {
        // capital letter
        return c - 'a' + 10;
    } else {
        // lowercase letter
        return c - 'a' + 10;
    }
}

scanner::~scanner() {
    if (close_stream) {
        std::ifstream* ptr = dynamic_cast<std::ifstream*>(input);
        ptr->close();
    }
}

// increment the scanner position, keeping track of lines and columns
void scanner::advance(char ch) {
    if (ch == '\n') {
        ++line;
        col = 0;
    } else {
        ++col;
    }
}

token scanner::make_token(token_kind tk) {
    return token(tk, source_loc(filename, line, col));
}
token scanner::make_token(token_kind tk, string str) {
    return token(tk, source_loc(filename, line, col), str);
}
token scanner::make_token(token_kind tk, double num) {
    return token(tk, source_loc(filename, line, col), num);
}

// this is the main scanning function
token scanner::next_token() {
    while (!eof()) {
        auto c = get_char();
        if (is_ws(c)) {
            continue;
        }
        switch (c) {
        case ';': // comment
            while (get_char() != '\n');
            break;

        // paired delimiters
        case '{':
            return make_token(t_kl_brace);
        case '}':
            return make_token(t_kr_brace);
        case '[':
            return make_token(t_kl_bracket);
        case ']':
            return make_token(t_kr_bracket);
        case '(':
            return make_token(t_kl_paren);
        case ')':
            return make_token(t_kr_paren);

        // quotation
        case '\'':
            return make_token(t_kquote);
        case '`':
            return make_token(t_kbacktick);
        case ',':
            // check if next character is @
            // i_mp_ln_ot_e: an e_of at this point would be a syntax error, but we let it slide up to the
            // parser for the sake of better error generation
            if (!eof() && peek_char() == '@') {
                get_char();
                return make_token(t_kcomma_splice);
            } else {
                return make_token(t_kcomma);
            }

        // dollar sign
        case '$':
            // i_mp_ln_ot_e: unlike the case for unquote, eof here could still result in a syntactically
            // valid (albeit probably dumb) program
            if (eof()) {
                scan_sym_or_num(c);
                break;
            }
            c = peek_char();
            switch (c) {
            case '`':
                return make_token(t_kdollar_backtick);
                break;
            case '{':
                return make_token(t_kdollar_brace);
                break;
            case '[':
                return make_token(t_kdollar_bracket);
                break;
            case '(':
                return make_token(t_kdollar_paren);
                break;
            }
            break;

            // string literals
        case '"':
            return scan_string_literal();
            break;

            // symbol or number
        default:
            return scan_sym_or_num(c);
        }
    }
    // if we get here, we encountered e_of
    return make_token(t_ke_of);
}

string strip_escape_chars(const string& s) {
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

static optional<f64> parse_num(const vector<char>& buf) {
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
        return { };
    }

    // whether we encounter a digit
    bool digit = false;

    // get integer part
    f64 res = 0;
    char ch;
    // first integer character
    while (i < buf.size() && is_digit(ch=buf[i], base)) {
        digit = true;
        res *= base;
        res += digit_val(ch);
        ++i;
    }

    // check if we got to the end
    if (i == buf.size()) {
        return sign*res;
    }

    // check for decimal point
    if (ch == '.') {
        // place value
        f64 place = 1/base;
        // parse digits
        ++i;
        while (i < buf.size() && is_digit(ch=buf[i], base)) {
            digit = true;
            res += digit_val(ch) * place;
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
    }


    // only other possibility after this is scientific notation
    
    // scientific notation only supports base 10 at the moment
    if ((ch != 'e' && ch != 'e') || base != 10) {
        // not a number
        return { };
    }
    // parse base 10 exponent
    ++i;
    // check for sign
    f64 exp_sign = 1;
    if (i < buf.size()) {
        if (buf[i] == '-') {
            ++i;
            exp_sign = -1;
        } else if (buf[i] == '+') {
            ++i;
        }
    }

    // make sure the exponent is there
    if (i == buf.size()) {
        // numbers cannot end with e
        return { };
    }

    f64 exponent = 0;
    while (i < buf.size() && is_digit(ch=buf[i])) {
        exponent *= 10;
        exponent += digit_val(ch);
        ++i;
    }
    // check if we got to the end
    if (i == buf.size()) {
        return sign*res*pow(10,exp_sign*exponent);
    }

    // this means we found an illegal character
    return { };
}


token scanner::scan_sym_or_num(char first) {
    if (first == '.') {
        error("tokens may not begin with '.'.");
    }
    bool escaped = first == '\\';
    // whether this is really a symbol or a dot form
    bool dot = false;
    // whether the previous symbol was a dot
    bool prev_dot = false;
    vector<char> buf;
    buf.push_back(first);

    char c;
    while(true) {
        if (escaped) {
            // i_mp_ln_ot_e: this throws an exception at e_of, which is the desired behavior
            buf.push_back(get_char());
            escaped = false;
            continue;
        }
        if (eof()) {
            // it's fine for eof to terminate a symbol unless the last character is an escape
            break;
        }

        if (!is_sym_char(c = peek_char())) {
            break;
        }
        get_char();
        if (c == '.') {
            if (prev_dot) {
                error("successive unescaped dots.");
            }
            dot = true;
            prev_dot = true;
        } else {
            prev_dot = false;
        }
        if (c == '\\') {
            escaped = true;
        }
        buf.push_back(c);
    }


    // t_od_o: rather than rely on stod, we shoud probably use our own number scanner
    auto d = parse_num(buf);
    if (d.has_value()) {
        return make_token(t_knumber, *d);
    }

    if (buf[buf.size()-1] == '.') {
        error("non-numeric tokens may not end with a dot.");
    }

    string s(buf.data(),buf.size());
    if (dot) {
        return make_token(t_kdot, s);
    }

    return make_token(t_ksymbol, strip_escape_chars(s));
}

token scanner::scan_string_literal() {
    char c = get_char();
    vector<char> buf;

    while (c != '"') {
        if (c == '\\') {
            c = get_string_escape_char();
        }
        buf.push_back(c);
        c = get_char();
    }

    return make_token(t_kstring, string(buf.data(),buf.size()));
}

char scanner::get_string_escape_char() {
    // t_od_o: implement multi-character escape sequences
    char c = get_char();
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

    error("unrecognized string escape sequence.");
    return '\0'; // this is just to shut up the interpreter
}

bool scanner::eof() {
    return input->peek() == EOF;
}

char scanner::get_char() {
    if (eof()) {
        error("unexpected e_of while scanning.");
    }
    char c = input->get();
    advance(c);
    return c;
}

char scanner::peek_char() {
    if (eof()) {
        error("unexpected e_of while scanning.");
    }
    return input->peek();
}


}
