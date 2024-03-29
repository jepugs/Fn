#include "scan.hpp"

#include <cmath>
#include <sstream>

namespace fn {

sst_id scanner_intern(scanner_string_table& sst, const string& str) {
    auto e = sst.by_name.get2(str);
    if (e != nullptr) {
        return e->val;
    } else {
        auto id = sst.by_id.size;
        sst.by_name.insert(str, id);
        sst.by_id.push_back(str);
        return id;
    }
}

const string& scanner_name(const scanner_string_table& sst, sst_id id) {
    return sst.by_id[id];
}

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
}

scanner_string_table& scanner::get_sst() {
    return *sst;
}

source_loc scanner::get_loc() {
    return source_loc{line, col, false, 0};
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

token scanner::make_token(token_kind kind) const {
    return token{source_loc{line, col, false, 0}, kind};
}
token scanner::make_token(token_kind kind, const string& str) const {
    token res{source_loc{line, col, false, 0}, kind};
    res.d.str_id = scanner_intern(*sst, str);
    return res;
}
token scanner::make_float_token(double num) const {
    token res{source_loc{line, col, false, 0}, tk_float};
    res.d.f = num;
    return res;
}
token scanner::make_int_token(i32 num) const {
    token res{source_loc{line, col, false, 0}, tk_int};
    res.d.i = num;
    return res;
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
            while (!eof()) {
                if(get_char() == '\n') {
                    break;
                }
            }
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
            // IMPLNOTE: unlike the case for unquote, EOF here could still
            // result in a syntactically valid (albeit probably dumb) program
            if (eof()) {
                return scan_atom(c);
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
            default:
                return scan_atom('$');
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

token scanner::scan_atom(char first) {
    dyn_array<char> buf;
    bool escaped = first == '\\';

    if (!escaped) {
        buf.push_back(first);
        auto num = try_scan_num(buf, first);
        if (num.has_value()) {
            return *num;
        }
    }
    while (!eof()) {
        if (escaped) {
            buf.push_back(get_char());
            escaped = false;
        } else {
            char c = peek_char();
            if (c == '\\') {
                get_char();
                escaped = true;
            } else if (is_sym_char(c)) {
                get_char();
                buf.push_back(c);
            } else {
                break;
            }
        }
    }
    if (escaped) {
        error("Unexpected EOF after escape character.");
    }
    // handle colonated symbols. This only happens when there's an unescaped
    // colon as the first character of the symbol
    if (first == ':') {
        string str{buf.data, buf.size};
        auto id = expand_symbol(S, intern_id(S, str));
        return make_token(tk_symbol, symname(S, id));
    }
    return make_token(tk_symbol, string{buf.data, buf.size});
}

optional<token> scanner::try_scan_num(dyn_array<char>& buf, char first) {
    char ch;
    int sign = 1;

    // account for signs first:
    if (first == '+') {
        if (eof()) {
            return std::nullopt;
        }
        first = peek_char();
        if (is_sym_char(first)) {
            buf.push_back(get_char());
        } else {
            return std::nullopt;
        }
    } else if (first == '-') {
        if (eof()) {
            return std::nullopt;
        }
        sign = -1;
        first = peek_char();
        if (is_sym_char(first)) {
            buf.push_back(get_char());
        } else {
            return std::nullopt;
        }
    }

    // We have two main problems: dealing with +/- and dealing with hexadecimal.
    // In order to avoid backtracking, this involves a lot of nasty conditional
    // code.
    if (first == '0') {
        // possibilities: hex, dec w/ leading 0, symbol w/ leading 0
        if (eof() || !(is_sym_char(peek_char()))) {
            return make_int_token(0);
        }
        ch = get_char();
        buf.push_back(ch);
        if (ch == 'x' || ch == 'X') {
            if (eof()) {
                return std::nullopt;
            }
            ch = get_char();
            buf.push_back(ch);
            return try_scan_digits(buf, ch, sign, 16);
        } else if (ch == '.') {
            if (eof()) {
                return make_float_token(0);
            }
            return try_scan_digits(buf, ch, sign, 10);
        } else {
            return try_scan_digits(buf, ch, sign, 10);
        }
    } else if (is_digit(first)) {
        return try_scan_digits(buf, first, sign, 10);
    } else if (first == '.') {
        // the extra code here accounts for when the dot character comes by
        // itself, in which case it should be read as a symbol.
        auto start = buf.size;
        auto res = try_scan_digits(buf, first, sign, 10);
        if (start == buf.size) {
            // no characters read
            return std::nullopt;
        } else {
            return res;
        }
    }

    return std::nullopt;
}

// apply scientific notation exponent to num
inline f64 apply_exp(f64 num, i32 exp, u32 base=10) {
    if (exp > 0) {
        for (; exp > 0; --exp) {
            num *= base;
        }
    } else {
        for (; exp < 0; ++exp) {
            num /= base;
        }
    }
    return num;
}

optional<token> scanner::try_scan_digits(dyn_array<char>& buf,
                                       char first,
                                       int sign,
                                       u32 base) {
    u64 total = 0;

    if (is_digit(first, base)) {
        total = digit_val(first);
        if (eof()) {
            return make_int_token(total*sign);
        }
    } else if (first == '.') {
        i32 exp = 0;
        auto f = try_scan_frac(buf, &exp, base);
        if (f.has_value()) {
            return make_float_token(apply_exp(sign*(*f), exp));
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    char ch = peek_char();

    while (is_digit(ch, base)) {
        buf.push_back(get_char());
        total = base*total + digit_val(ch);
        if (eof()) {
            return make_int_token(total*sign);
        }
        ch = peek_char();
    }

    if (ch == '.') {
        get_char();
        if (eof()) {
            return make_float_token((f64)total*sign);
        }
        i32 exp = 0;
        auto f = try_scan_frac(buf, &exp, base);
        if (f.has_value()) {
            return make_float_token(apply_exp((*f + (f64)total)*sign, exp));
        } else {
            error("Dot token may not begin with an integer.");
            return std::nullopt;
        }
    } else if (base == 10 && (ch == 'e' || ch == 'E')) {
        // scientific notation
        get_char();
        auto p = try_scan_exp(buf);
        if (p.has_value()) {
            return make_float_token(apply_exp(total*sign, *p));
        } else {
            return std::nullopt;
        }
    } else if (is_sym_char(ch)) {
        return std::nullopt;
    } else {
        return make_int_token(total*sign);
    }
}

optional<f64> scanner::try_scan_frac(dyn_array <char>& buf, i32* exp, u32 base) {
    // error on EOF (desired behavior)
    char ch = peek_char();
    f64 total = 0;
    int pow = 0;
    *exp = 0;

    while (is_digit(ch, base)) {
        buf.push_back(get_char());
        total = total*base + digit_val(ch);
        ++pow;
        if (eof()) {
            for (; pow > 0; --pow) {
                total /= base;
            }
            return total;
        }
        ch = peek_char();
    }

    if (ch == '.') {
        error("Illegal dot syntax.");
    } else if (base == 10 && (ch == 'e' || ch == 'E')) {
        buf.push_back(get_char());
        auto p = try_scan_exp(buf);
        if (p.has_value()) {
            *exp = *p;
        } else {
            return std::nullopt;
        }
    } else if (is_sym_char(ch)) {
        return std::nullopt;
    }

    for (; pow > 0; --pow) {
        total /= base;
    }
    return total;
}

optional<i32> scanner::try_scan_exp(dyn_array<char>& buf) {
    if (eof()) {
        return std::nullopt;
    }

    int sign = 1;
    u32 res = 0;
    char ch = peek_char();

    if (ch == '+') {
        if (eof()) {
            return std::nullopt;
        }
        buf.push_back(ch);
        get_char();
        ch = peek_char();
    } else if (ch == '-') {
        if (eof()) {
            return std::nullopt;
        }
        sign = -1;
        buf.push_back(ch);
        get_char();
        ch = peek_char();
    }


    while (is_digit(ch)) {
        buf.push_back(ch);
        get_char();
        res = (res*10) + digit_val(ch);
        if (eof()) {
            return sign*res;
        }
        ch = peek_char();
    }

    if (eof() || !(is_sym_char(ch))) {
        return sign*res;
    }

    return std::nullopt;
}

token scanner::scan_string_literal() {
    char c = get_char();
    dyn_array<char> buf;

    while (c != '"') {
        if (c == '\\') {
            get_string_escape_char(buf);
        } else {
            buf.push_back(c);
        }
        c = get_char();
    }

    return make_token(tk_string, string(buf.data,buf.size));
}

void scanner::hex_digits_to_bytes(dyn_array<char>& buf, u32 num_bytes) {
    for (u32 i = 0; i < num_bytes; ++i) {
        char ch1;
        char ch2;
        if (eof()
            || !is_digit(ch1 = get_char(), 16)
            || eof()
            || !is_digit(ch2 = get_char(), 16)) {
            error("Too few hexadecimal digits in string escape code.");
        }
        u8 val = (digit_val(ch1) << 4) + digit_val(ch2);
        buf.push_back((char)val);
    }
}

// read (up to 3) octal digits and write a byte.
void scanner::octal_to_byte(dyn_array<char>& buf, u8 first) {
    u8 total = first;
    // read up to 2 more digits
    for (int i = 0; i < 2; ++i) {
        char ch = peek_char();
        if (is_digit(ch, 8)) {
            get_char();
            total = (total << 3) + digit_val(ch);
        } else {
            break;
        }
    }
    buf.push_back((char)total);
}

void scanner::get_string_escape_char(dyn_array<char>& buf) {
    // TODO: implement multi-character escape sequences
    char ch = get_char();
    switch (ch) {
    case '\'':
        buf.push_back('\'');
        break;
    case '\"':
        buf.push_back('\"');
        break;
    case '?':
        buf.push_back('\?');
        break;
    case '\\':
        buf.push_back('\\');
        break;
    case 'a':
        buf.push_back('\a');
        break;
    case 'b':
        buf.push_back('\b');
        break;
    case 'f':
        buf.push_back('\f');
        break;
    case 'n':
        buf.push_back('\n');
        break;
    case 'r':
        buf.push_back('\r');
        break;
    case 't':
        buf.push_back('\t');
        break;
    case 'v':
        buf.push_back('\v');
        break;
    case 'x':
        hex_digits_to_bytes(buf, 1);
        break;
    case 'u':
        hex_digits_to_bytes(buf, 2);
        break;
    case 'U':
        hex_digits_to_bytes(buf, 4);
        break;
    default:
        // check for octal
        if (is_digit(ch, 8)) {
            octal_to_byte(buf, digit_val(ch));
        } else {
            error("Unrecognized string escape sequence.");
        }
    }
}

bool scanner::eof() {
    return input->peek() == EOF;
}

bool scanner::eof_skip_ws() {
    while (!eof()) {
        auto ch = peek_char();
        // skip comments
        if (ch == ';') {
            while (!eof()) {
                if(get_char() == '\n') {
                    break;
                }
            }
        } else if (!is_ws(ch)) {
            break;
        } else {
            get_char();
        }
    }
    return eof();
}

char scanner::get_char() {
    if (eof()) {
        error("Unexpected EOF while scanning.");
    }
    char c = input->get();
    advance(c);
    return c;
}

char scanner::peek_char() {
    if (eof()) {
        error("Unexpected EOF while scanning.");
    }
    return input->peek();
}

size_t scanner::tellg() {
    return input->tellg();
}


}
