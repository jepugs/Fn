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


// on error, returns nullptr and sets err, but DOES NOT
static ast_form* parse_to_delimiter(scanner& sc,
        symbol_table& symtab,
        vector<ast_form*>& buf,
        token_kind end,
        parse_error* err) {
    auto tok = sc.next_token();
    while (tok.tk != end) {
        if (tok.tk == tk_eof) {
            err->origin = tok.loc;
            err->message = "Encountered EOF while expecting closing delimiter.";
        };
        return nullptr;
    }
    return mk_list_form(tok.loc, buf);
}

static ast_form* parse_prefix(scanner& sc,
        symbol_table& symtab,
        const source_loc& loc,
        const string& op,
        token t0,
        parse_error* err) {
    vector<ast_form*> buf;
    buf.push_back(mk_symbol_form(loc, symtab.intern(op)));
    auto x = parse_form(sc, symtab, t0, err);
    if (!x) {
        free_ast_form(buf[0]);
        return nullptr;
    }
    
    buf.push_back(x);
    return mk_list_form(loc, buf);
}

static ast_form* parse_prefix(scanner& sc,
        symbol_table& symtab,
        const source_loc& loc,
        const string& op,
        parse_error* err) {
    auto tok = sc.next_token();
    return parse_prefix(sc, symtab, loc, op, tok, err);
}

ast_form* parse_form(scanner& sc, symbol_table& symtab, parse_error* err) {
    return parse_form(sc, symtab, sc.next_token(), err);
}

ast_form* parse_form(scanner& sc,
        symbol_table& symtab,
        token t0,
        parse_error* err) {
    ast_form* res = nullptr;
    string* str = t0.datum.str;
    vector<ast_form*> buf;
    auto& loc = t0.loc;

    switch (t0.tk) {
    case tk_eof:
        err->origin = loc;
        err->message = "Unexpected EOF.";
        return nullptr;

    case tk_number:
        res = mk_number_form(loc, t0.datum.num);
        break;
    case tk_string:
        res = mk_string_form(loc, fn_string{*str});
        break;
    case tk_symbol:
        res = mk_symbol_form(loc, symtab.intern(*str));
        break;

    case tk_lparen:
        // this will give res=nullptr and set err if there's an error
        if (!(res=parse_to_delimiter(sc, symtab, buf, tk_rparen, err))) {
            for (auto x : buf) {
                free_ast_form(x);
            }
        }
        break;
    case tk_rparen:
        err->origin = loc;
        err->message = "Unmatched delimiter ')'.";
        res = nullptr;
        break;
    case tk_lbrace:
        buf.push_back(mk_symbol_form(loc, symtab.intern("Table")));
        if (!(res=parse_to_delimiter(sc, symtab, buf, tk_rbrace, err))) {
            for (auto x : buf) {
                free_ast_form(x);
            }
        }
        break;
    case tk_rbrace:
        err->origin = loc;
        err->message = "Unmatched delimiter '}'.";
        res = nullptr;
        break;
    case tk_lbracket:
        buf.push_back(mk_symbol_form(loc, symtab.intern("List")));
        if (!(res=parse_to_delimiter(sc, symtab, buf, tk_rbracket, err))) {
            for (auto x : buf) {
                free_ast_form(x);
            }
        }
        break;
    case tk_rbracket:
        err->origin = loc;
        err->message = "Unmatched delimiter '}'.";
        res = nullptr;
        break;

    case tk_dot:
        buf.push_back(mk_symbol_form(loc, symtab.intern("dot")));
        for (auto s : *t0.datum.ids) {
            buf.push_back(mk_symbol_form(loc, symtab.intern(s)));
        }
        res = mk_list_form(loc, buf);
        break;

    case tk_quote:
        res = parse_prefix(sc, symtab, loc, "quote", err);
        break;
    case tk_backtick:
        res = parse_prefix(sc, symtab, loc, "quasiquote", err);
        break;
    case tk_comma:
        res = parse_prefix(sc, symtab, loc, "unquote", err);
        break;
    case tk_comma_at:
        res = parse_prefix(sc, symtab, loc, "unquote-splicing", err);
        break;
    case tk_dollar_backtick:
        res = parse_prefix(sc, symtab, loc, "dollar-fn", token{tk_backtick,loc},
                err);
        break;
    case tk_dollar_brace:
        res = parse_prefix(sc, symtab, loc, "dollar-fn", token{tk_lbrace,loc},
                err);
        res = mk_list_form(loc, buf);
        break;
    case tk_dollar_bracket:
        res = parse_prefix(sc, symtab, loc, "dollar-fn", token{tk_lbracket,loc},
                err);
        res = mk_list_form(loc, buf);
        break;
    case tk_dollar_paren:
        res = parse_prefix(sc, symtab, loc, "dollar-fn", token{tk_lparen,loc},
                err);
        res = mk_list_form(loc, buf);
        break;
    }
    return res;
}

}
