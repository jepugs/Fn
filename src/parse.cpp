#include "parse.hpp"

#include <cstring>

namespace fn_parse {

using namespace fn;
using namespace fn_scan;

ast_form* mk_number_form(source_loc loc, f64 num, ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form{
        .loc = loc,
        .kind = ak_number_atom,
        .datum = {.num=num}
    };
}

ast_form* mk_string_form(source_loc loc,
        const fn_string& str,
        ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form{
        .loc = loc,
        .kind = ak_string_atom,
        .datum = {.str=new fn_string{str}}
    };
}

ast_form* mk_string_form(source_loc loc,
        fn_string&& str,
        ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form{
        .loc = loc,
        .kind = ak_string_atom,
        .datum = {.str=new fn_string{str}}
    };
}

ast_form* mk_symbol_form(source_loc loc, symbol_id sym, ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form {
        .loc = loc,
        .kind = ak_symbol_atom,
        .datum = {.sym=sym}
    };
}

ast_form* mk_list_form(source_loc loc,
        u32 length,
        ast_form** lst,
        ast_form* dest) {
    return new(dest) ast_form {
        .loc = loc,
        .kind = ak_list,
        .list_length = length,
        .datum = {.list=lst}
    };
}

ast_form* mk_list_form(source_loc loc,
        const vector<ast_form*>& lst,
        ast_form* dest) {
    auto sz = static_cast<u32>(lst.size());
    auto res = mk_list_form(loc, sz, new ast_form*[sz], dest);
    memcpy(res->datum.list, lst.data(), sz);
    return res;
}


// ast_form* ast_form::copy() const {
//     switch (kind) {
//     case ak_number_atom:
//         return new ast_form{ast_atom{*datum.atom}, loc};
//     case ak_error:
//         return new ast_node{loc};
//     case ak_list:
//         break;
//     }

//     // list behavior
//     vector<ast_node*> list;
//     for (auto v : *datum.list) {
//         list.push_back(v->copy());
//     }
//     return new ast_node{list, loc};
// }

static string print_grouped(const symbol_table& symtab,
        char open,
        char close,
        ast_form** list,
        size_t list_length) {
    string res{open};
    if (list_length == 0) {
        return res + close;
    }
    u32 u;
    ast_form* form;
    for (u = 0; u < list_length - 1; ++u) {
        form = list[u];
        res = res + form->as_string(symtab) + " ";
    }
    form = list[u];
    res = res + form->as_string(symtab) + close;
    return res;
}

string with_escapes(const string& src) {
    vector<char> buf;
    for (u32 u = 0; u < src.length(); ++u) {
        auto ch = src.at(u);
        switch (ch) {
        case '\\':
        case '.':
        case '(':
        case ')':
        case '\'':
        case '"':
        case '`':
        case ',':
        case '[':
        case ']':
        case '{':
        case '}':
        case ' ':
        case '\n':
        case '\t':
            buf.push_back('\\');
            break;
        }
        buf.push_back(ch);
    }
    return string{buf.data(), buf.size()};
}

string with_str_escapes(const string& src) {
    vector<char> buf;
    for (u32 u = 0; u < src.length(); ++u) {
        auto ch = src.at(u);
        switch (ch) {
        case '\\':
        case '"':
            buf.push_back('\\');
            break;
        }
        buf.push_back(ch);
    }
    return string{buf.data(), buf.size()};
}

string ast_form::as_string(const symbol_table& symtab) const {
    string res = "";
    switch (kind) {
    case ak_number_atom:
        return std::to_string(datum.num);
        break;
    case ak_string_atom:
        return "\""
            + with_str_escapes(datum.str->as_string())
            + "\"";
        break;
    case ak_symbol_atom:
        return with_escapes(symtab[datum.sym]);
        break;
    case ak_list:
        res = print_grouped(symtab, '(', ')', datum.list, list_length);
        break;
    case ak_error:
        break;
    }

    return res;
}

bool ast_form::is_symbol() const {
    return kind == ak_symbol_atom;
}

bool ast_form::is_keyword(const symbol_table& symtab) const {
    if (!is_symbol()) {
        return false;
    }
    auto name = symtab[datum.sym];
    return name.length() > 0 && name[0] == ':';
}

symbol_id ast_form::get_symbol() const {
    return datum.sym;
}



#define parse_error(msg, loc) throw fn_error("fn_parse", msg, loc)

void parse_to_delimiter(scanner& sc,
                        symbol_table& symtab,
                        vector<ast_form*>& buf,
                        token_kind end) {
    auto tok = sc.next_token();
    while (tok.tk != end) {
        if (tok.tk == tk_eof) {
            parse_error("Unexpected EOF searching for delimiter.", tok.loc);
        }
        buf.push_back(parse_form(sc, symtab, std::make_optional(tok)));
        tok = sc.next_token();
    }
}

void parse_prefix(scanner& sc,
                  symbol_table& symtab,
                  vector<ast_form*>& buf,
                  const string& op,
                  const source_loc& loc,
                  optional<token> t0 = std::nullopt) {
    buf.push_back(mk_symbol_form(loc, symtab.intern(op)));
    buf.push_back(parse_form(sc, symtab, t0));
}

ast_form* parse_form(scanner& sc, symbol_table& symtab, optional<token> t0) {
    auto tok = t0.has_value() ? *t0 : sc.next_token();
    ast_form* res = nullptr;
    string* str = tok.datum.str;
    vector<ast_form*> buf;

    switch (tok.tk) {
    case tk_eof:
        return nullptr;

    case tk_number:
        res = mk_number_form(tok.loc, tok.datum.num);
        break;
    case tk_string:
        res = mk_string_form(tok.loc, fn_string{*str});
        break;
    case tk_symbol:
        res = mk_symbol_form(tok.loc, symtab.intern(*str));
        break;

    case tk_lparen:
        parse_to_delimiter(sc, symtab, buf, tk_rparen);
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_rparen:
        parse_error("Unmatched delimiter ')'.", tok.loc);
        break;
    case tk_lbrace:
        buf.push_back(mk_symbol_form(tok.loc, symtab.intern("Table")));
        parse_to_delimiter(sc, symtab, buf, tk_rbrace);
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_rbrace:
        parse_error("Unmatched delimiter '}'.", tok.loc);
        break;
    case tk_lbracket:
        buf.push_back(mk_symbol_form(tok.loc, symtab.intern("List")));
        parse_to_delimiter(sc, symtab, buf, tk_rbracket);
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_rbracket:
        parse_error("Unmatched delimiter ']'.", tok.loc);
        break;

    case tk_dot:
        buf.push_back(mk_symbol_form(tok.loc, symtab.intern("dot")));
        for (auto s : *tok.datum.ids) {
            buf.push_back(mk_symbol_form(tok.loc, symtab.intern(s)));
        }
        res = mk_list_form(tok.loc, buf);
        break;

    case tk_quote:
        parse_prefix(sc, symtab, buf, "quote", tok.loc);
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_backtick:
        parse_prefix(sc, symtab, buf, "quasiquote", tok.loc);
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_comma:
        parse_prefix(sc, symtab, buf, "unquote", tok.loc);
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_comma_at:
        parse_prefix(sc, symtab, buf, "unquote-splicing", tok.loc);
        res = mk_list_form(tok.loc, buf);
        break;

    case tk_dollar_backtick:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_backtick, tok.loc});
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_dollar_brace:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_lbrace, tok.loc});
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_dollar_bracket:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_lbracket, tok.loc});
        res = mk_list_form(tok.loc, buf);
        break;
    case tk_dollar_paren:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_lparen, tok.loc});
        res = mk_list_form(tok.loc, buf);
        break;
    }
    return res;
}

}
