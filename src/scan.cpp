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
    char max_cap = 'A' + base - 11;
    if (c >= 'A' && c <= max_cap) {
        return true;
    }
    char max_low = 'a' + base - 11;
    if (c >= 'a' && c <= max_low) {
        return true;
    }
    return false;
}

// get the value of a number/letter as an integer (e.g. '7'->7, 'b'->11). Gives
// junk on anything other than numerals or latin letters.
static inline i32 digit_val(char c) {
    if (c <= '9') {
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

token scanner::make_token(token_kind tk) const {
    return token(tk, source_loc(filename, line, col));
}
token scanner::make_token(token_kind tk, const string& str) const {
    return token(tk, source_loc(filename, line, col), str);
}
token scanner::make_token(token_kind tk, double num) const {
    return token(tk, source_loc(filename, line, col), num);
}
token scanner::make_token(token_kind tk, const vector<string>& ids) const {
    return token(tk, source_loc(filename, line, col), ids);
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
            return make_token(tk_lbrace);
        case '}':
            return make_token(tk_rbrace);
        case '[':
            return make_token(tk_lbracket);
        case ']':
            return make_token(tk_rbracket);
        case '(':
            return make_token(tk_lparen);
        case ')':
            return make_token(tk_rparen);

        // quotation
        case '\'':
            return make_token(tk_quote);
        case '`':
            return make_token(tk_backtick);
        case ',':
            // check if next character is @
            // IMPLNOTE: an EOF at this point would be a syntax error, but we
            // let it slide up to the parser for the sake of better error
            // generation
            if (!eof() && peek_char() == '@') {
                get_char();
                return make_token(tk_comma_at);
            } else {
                return make_token(tk_comma);
            }

        // dollar sign
        case '$':
            // i_mp_ln_ot_e: unlike the case for unquote, eof here could still result in a syntactically
            // valid (albeit probably dumb) program
            if (eof()) {
                scan_atom(c);
                break;
            }
            c = peek_char();
            switch (c) {
            case '`':
                get_char();
                return make_token(tk_dollar_backtick);
            case '{':
                get_char();
                return make_token(tk_dollar_brace);
            case '[':
                get_char();
                return make_token(tk_dollar_bracket);
            case '(':
                get_char();
                return make_token(tk_dollar_paren);
            }
            break;

            // string literals
        case '"':
            return scan_string_literal();

            // symbol or number
        default:
            return scan_atom(c);
        }
    }
    // if we get here, we encountered EOF
    return make_token(tk_eof);
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

token scanner::scan_dot(string first) {
    vector<string> ids;
    ids.push_back(first);
    
    vector<char> buf;

    // this will be an error if on EOF (desired behavior)
    char c = get_char();
    while (true) {
        if (c == '.') {
            error("multiple successive dots.");
        }
        do {
            if (c == '\\') {
                c = get_char();
            }
            buf.push_back(c);
            if (eof()) {
                break;
            }
            c = get_char();
        } while (c != '.' && is_sym_char(c));

        ids.push_back(string(buf.data(), buf.size()));
        buf.clear();
        if (c != '.') {
            break;
        }
    }

    return make_token(tk_dot, ids);
}

// atom processor is a hand-programmed state machine. This leads to
// unfortunately opaque code. Each of the _state functions corresponds to some
// point in the state machine. Along the way, a buffer of read characters is
// passed around.
token scanner::scan_atom(char first) {
    // stores the characters read so far, needed if we end up reading a symbol.
    vector<char> buf;
    buf.push_back(first);

    char ch;
    switch (first) {
    case '+':
        if (eof()) {
            return scan_sym_state(buf);
        }
        ch = get_char();
        buf.push_back(ch);
        return scan_num_state(buf, ch, 1);
    case '-':
        if (eof()) {
            return scan_sym_state(buf);
        }
        ch = get_char();
        buf.push_back(ch);
        return scan_num_state(buf, ch, -1);
    case '.':
        return scan_frac_state(0, 1, 10);
    default:
        if (is_digit(first)) {
            return scan_num_state(buf, first, 1);
        } else {
            return scan_sym_state(buf);
        }
    }
}

// Assumptions:
// - first is first character of atom after sign (if present).
// - buf.back() == first
// - scanner starts one character after first
token scanner::scan_num_state(vector<char>& buf, char first, int sign) {
    char ch;

    // the main issue is dealing with hexadecimal
    if (first == '0') {
        // possibilities: hex, dec w/ leading 0, symbol w/ leading 0
        if (eof()) {
            return make_token(tk_number, 0);
        }
        ch = get_char();
        buf.push_back(ch);
        if (ch == 'x' || ch == 'X') {
            // possibilities: hexadecimal, symbol leading with "0x" or "0X"
            if (eof()) {
                // return symbol 0x
                return scan_sym_state(buf);
            } else {
                return scan_digit_state(buf, std::nullopt, sign, 16);
            }
        } else if (ch == '.') {
            // possibilities: "0." (number 0), 0.fraction
            if (eof()) {
                return make_token(tk_number, 0);
            } else {
                return scan_frac_state(0, sign, 10);
            }
        } else {
            // possibilities: number, symbol leading with a digit
            return scan_digit_state(buf, std::nullopt, sign, 10);
        }
    } else if (is_digit(first)) {
        return scan_digit_state(buf, first, sign, 10);
    } else if (first == '.') {
        return scan_frac_state(0, sign, 10);
    } else {
        return scan_sym_state(buf);
    }
}

// assumptions:
// - if first is provided, *first == buf.back() and scanner starts right after
// - otherwise, scanner starts on first digit character.
token scanner::scan_digit_state(vector<char>& buf,
                                optional<char> first,
                                int sign,
                                u32 base) {
    u64 total = 0;

    if (first.has_value()) {
        if (is_digit(*first, base)) {
            total = digit_val(*first);
            if (eof()) {
                return make_token(tk_number, (f64)total*sign);
            }
        } else {
            return scan_sym_state(buf);
        }
    } else if (eof()) {
        // no more characters so fall symbol
        return scan_sym_state(buf);
    }

    char ch = peek_char();

    while (is_digit(ch, base)) {
        buf.push_back(get_char());
        total = base*total + digit_val(ch);
        if (eof()) {
            return make_token(tk_number, (f64)total*sign);
        }
        ch = peek_char();
    }

    if (ch == '.') {
        get_char();
        if (eof()) {
            return make_token(tk_number, (f64)total*sign);
        }
        return scan_frac_state(total*sign, sign, base);
    } else if (is_sym_char(ch)) {
        buf.push_back(get_char());
        return scan_sym_state(buf);
    } else {
        return make_token(tk_number, (f64)total*sign);
    }
}

// Assumptions:
// - scanner starts on the first character after '.'
// Note that on failure, this indicates illegal dot syntax and causes an error.
token scanner::scan_frac_state(f64 integral, int sign, u32 base) {
    if (eof()) {
        error("Dot by itself is not a valid atom.");
    }

    char ch = peek_char();
    f64 frac = 0;
    while (is_digit(ch, base)) {
        get_char();
        frac = (frac + digit_val(ch)) / base;

        if (eof()) {
            return make_token(tk_number, integral + frac*sign);
        }
        ch = peek_char();
    }

    if (is_sym_char(ch) || ch == '.') {
        error("Illegal dot syntax.");
    }
    return make_token(tk_number, integral + frac*sign);
}

token scanner::scan_sym_state(vector<char>& buf) {
    return make_token(tk_symbol, "no-reader");
}

// token scanner::scan_atom(char first) {
//     // Note: for the sake of clarity and readability, this algorithm makes
//     // multiple passes over the input.
//     vector<char> buf;
//     vector<string> ids;
//     char c = first;

//     // while (is_sym_char(c)) {
//     //     if (c == '\\') {
//     //         buf.push_back(c);
//     //         c = get_char();
//     //     }
//     //     buf.push_back(c);
//     //     if (eof()) {
//     //         break;
//     //     }
//     //     c = get_char();
//     // }

//     while (true) {
//         if (c == '.') {
//             error("Illegal dot syntax");
//         }
//         do {
//             if (c == '\\') {
//                 c = get_char();
//             }
//             buf.push_back(c);
//             if (eof()) {
//                 break;
//             }
//             c = get_char();
//         } while (c != '.' && is_sym_char(c));

//         ids.push_back(string(buf.data(), buf.size()));
//         buf.clear();
//         if (c != '.') {
//             break;
//         }    // collect all symbol consituents/escapes in the buffer
//     }

//     // check if the token is a number
//     optional<double> d = std::nullopt;
//     if (ids.size() == 1) {
//         d = parse_num(ids[0]);
//     } else if (ids.size() == 2) {
//         // FIXME: we have already split the num
//     }
//     if (d.has_value()) {
//         return *d;
//     } else {
//         // scan a dot or a symbol
//     }
// }

// token scanner::parse_atom(const vector<char>& chars) {
//     // check for number first
//     auto d = parse_num(chars);
//     if (d.has_value()) {
//         return *d;
//     }

//     vector<char> buf;
//     bool escaped = false;
//     char c;
//     for (c : chars) {
//         if (escaped) {
//             escaped = false;
//         } else if (c == '\\') {
//             escaped = true;
//             continue;
//         } else if (c == '.') {
//             break;
//             // read dot
//         }
//         buf.push_back(c);
//     }
// }

token scanner::scan_sym_or_num(char first) {
    if (first == '.') {
        error("tokens may not begin with '.'.");
    }
    bool escaped = first == '\\';
    vector<char> buf;
    buf.push_back(first);

    char c;
    while (true) {
        if (escaped) {
            // IMPLNOTE: this throws an exception at EOF, which is the desired behavior
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
            return scan_dot(string(buf.data(), buf.size()));
        } else if (c == '\\') {
            escaped = true;
        }
        buf.push_back(c);
    }

    auto d = parse_num(buf);
    if (d.has_value()) {
        return make_token(tk_number, *d);
    }

    if (buf[buf.size()-1] == '.') {
        error("non-numeric tokens may not end with a dot.");
    }

    string s(buf.data(),buf.size());
    return make_token(tk_symbol, strip_escape_chars(s));
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

    return make_token(tk_string, string(buf.data(),buf.size()));
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
