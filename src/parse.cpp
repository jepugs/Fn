#include "parse.hpp"

#include <sstream>
#include <cstring>

namespace fn_parse {

using namespace fn;
using namespace fn_scan;

static void parse_error(istate* S, const source_loc& loc, const string& msg) {
    std::ostringstream os;
    os << "File " << convert_fn_string(S->filename) << ", line " << loc.line
       << ", col " << loc.col << ":\n  " << msg;
    ierror(S, os.str());
}

ast_form* mk_number_form(const source_loc& loc, f64 num, ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form{
        .loc = loc,
        .kind = ak_number_atom,
        .datum = {.num=num}
    };
}

ast_form* mk_string_form(const source_loc& loc,
        const string& str,
        ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form{
        .loc = loc,
        .kind = ak_string_atom,
        .datum = {.str=new string{str} }
    };
}

ast_form* mk_string_form(const source_loc& loc,
        string&& str,
        ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form{
        .loc = loc,
        .kind = ak_string_atom,
        .datum = {.str=new string{str} }
    };
}

ast_form* mk_symbol_form(const source_loc& loc, symbol_id sym, ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form {
        .loc = loc,
        .kind = ak_symbol_atom,
        .datum = {.sym=sym}
    };
}

ast_form* mk_list_form(const source_loc& loc,
        u32 length,
        ast_form** lst,
        ast_form* dest) {
    if (dest == nullptr) {
        dest = new ast_form;
    }
    return new(dest) ast_form {
        .loc = loc,
        .kind = ak_list,
        .list_length = length,
        .datum = {.list=lst}
    };
}

ast_form* mk_list_form(const source_loc& loc,
        dyn_array<ast_form*>* lst,
        ast_form* dest) {
    ast_form** data = new ast_form*[lst->size];
    for (u32 i = 0; i < lst->size; ++i) {
        data[i] = (*lst)[i];
    }
    return mk_list_form(loc, lst->size, data, dest);
}

void clear_ast_form(ast_form* form, bool recursive) {
    switch (form->kind) {
    case ak_string_atom:
        delete form->datum.str;
        break;
    case ak_list:
        if (recursive) {
            for (u32 i = 0; i < form->list_length; ++i) {
                free_ast_form(form->datum.list[i], true);
            }
        }
        delete[] form->datum.list;
        break;
    default:
        break;
    }
}

void free_ast_form(ast_form* form, bool recursive) {
    clear_ast_form(form, recursive);
    delete form;
}


ast_form* ast_form::copy() const {
    switch (kind) {
    case ak_number_atom:
        return mk_number_form(loc, datum.num);
    case ak_string_atom:
        return mk_string_form(loc, *datum.str);
    case ak_symbol_atom:
        return mk_symbol_form(loc, datum.sym);
    default:
        break;
    }

    auto res = mk_list_form(loc, list_length, new ast_form*[list_length]);
    for (u32 i = 0; i < list_length; ++i) {
        res->datum.list[i] = datum.list[i]->copy();
    }
    return res;
}

static string print_grouped(const symbol_table* symtab,
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
    dyn_array<char> buf;
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
    return string{buf.data, buf.size};
}

string with_str_escapes(const string& src) {
    dyn_array<char> buf;
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
    return string{buf.data, buf.size};
}

string ast_form::as_string(const symbol_table* symtab) const {
    string res = "";
    switch (kind) {
    case ak_number_atom:
        return std::to_string(datum.num);
        break;
    case ak_string_atom:
        return "\""
            + with_str_escapes(*datum.str)
            + "\"";
        break;
    case ak_symbol_atom:
        return with_escapes((*symtab)[datum.sym]);
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

bool ast_form::is_keyword(const symbol_table* symtab) const {
    if (!is_symbol()) {
        return false;
    }
    auto name = (*symtab)[datum.sym];
    return name.length() > 0 && name[0] == ':';
}

symbol_id ast_form::get_symbol() const {
    return datum.sym;
}


// on error, returns nullptr and sets err, but DOES NOT
static ast_form* parse_to_delimiter(scanner* sc,
        istate* S,
        dyn_array<ast_form*>* buf,
        token_kind end,
        bool* resumable) {
    auto tok = sc->next_token();
    while (tok.tk != end) {
        if (tok.tk == tk_eof) {
            parse_error(S, tok.loc,
                    "Encountered EOF while expecting closing delimiter.");
            *resumable = true;
            return nullptr;
        };
        auto x = parse_next_form(sc, S, tok, resumable);
        if (!x) {
            return nullptr;
        }
        buf->push_back(x);
        tok = sc->next_token();
    }

    return mk_list_form(tok.loc, buf);
}

static ast_form* parse_prefix(scanner* sc,
        istate* S,
        const source_loc& loc,
        const string& op,
        token t0,
        bool* resumable) {
    dyn_array<ast_form*> buf;
    buf.push_back(mk_symbol_form(loc, intern(S, op)));
    auto x = parse_next_form(sc, S, t0, resumable);
    if (!x) {
        free_ast_form(buf[0]);
        return nullptr;
    }

    buf.push_back(x);
    return mk_list_form(loc, &buf);
}

static ast_form* parse_prefix(scanner* sc,
        istate* S,
        const source_loc& loc,
        const string& op,
        bool* resumable) {
    auto tok = sc->next_token();
    return parse_prefix(sc, S, loc, op, tok, resumable);
}

ast_form* parse_next_form(scanner* sc,
        istate* S,
        bool* resumable) {
    return parse_next_form(sc, S, sc->next_token(), resumable);
}

ast_form* parse_next_form(scanner* sc,
        istate* S,
        token t0,
        bool* resumable) {
    ast_form* res = nullptr;
    string* str = t0.datum.str;
    dyn_array<ast_form*> buf;
    auto& loc = t0.loc;

    switch (t0.tk) {
    case tk_eof:
        parse_error(S, t0.loc, "Unexpected EOF.");
        *resumable = true;
        return nullptr;

    case tk_number:
        res = mk_number_form(loc, t0.datum.num);
        break;
    case tk_string:
        res = mk_string_form(loc, string{*str});
        break;
    case tk_symbol:
        res = mk_symbol_form(loc, intern(S, *str));
        break;

    case tk_lparen:
        // this will give res=nullptr and set err if there's an error
        if (!(res=parse_to_delimiter(sc, S, &buf, tk_rparen,
                                resumable))) {
            for (auto x : buf) {
                free_ast_form(x);
            }
        }
        break;
    case tk_rparen:
        parse_error(S, loc, "Unmatched delimiter ')'.");
        *resumable = false;
        res = nullptr;
        break;
    case tk_lbrace:
        buf.push_back(mk_symbol_form(loc, intern(S, "Table")));
        if (!(res=parse_to_delimiter(sc, S, &buf, tk_rbrace, resumable))) {
            for (auto x : buf) {
                free_ast_form(x);
            }
        }
        break;
    case tk_rbrace:
        parse_error(S, loc, "Unmatched delimiter '}'.");
        *resumable = false;
        res = nullptr;
        break;
    case tk_lbracket:
        buf.push_back(mk_symbol_form(loc, intern(S, "List")));
        if (!(res=parse_to_delimiter(sc, S, &buf, tk_rbracket, resumable))) {
            for (auto x : buf) {
                free_ast_form(x);
            }
        }
        break;
    case tk_rbracket:
        parse_error(S, loc, "Unmatched delimiter ']'.");
        *resumable = false;
        res = nullptr;
        break;

    case tk_quote:
        res = parse_prefix(sc, S, loc, "quote", resumable);
        break;
    case tk_backtick:
        res = parse_prefix(sc, S, loc, "quasiquote", resumable);
        break;
    case tk_comma:
        res = parse_prefix(sc, S, loc, "unquote", resumable);
        break;
    case tk_comma_at:
        res = parse_prefix(sc, S, loc, "unquote-splicing", resumable);
        break;
    case tk_dollar_backtick:
        res = parse_prefix(sc, S, loc, "dollar-fn", token{tk_backtick,loc},
                resumable);
        break;
    case tk_dollar_brace:
        res = parse_prefix(sc, S, loc, "dollar-fn", token{tk_lbrace,loc},
                resumable); 
        break;
    case tk_dollar_bracket:
        res = parse_prefix(sc, S, loc, "dollar-fn", token{tk_lbracket,loc},
                resumable);
        break;
    case tk_dollar_paren:
        res = parse_prefix(sc, S, loc, "dollar-fn", token{tk_lparen,loc},
                resumable);
        break;
    }
    return res;
}

dyn_array<ast_form*> parse_from_scanner(scanner* sc,
        istate* S) {
    dyn_array<ast_form*> res;
    ast_form* a;
    bool resumable;
    while (!sc->eof_skip_ws()
            && (a = parse_next_form(sc, S, &resumable))) {
        // pos holds the position after last successful read
        res.push_back(a);
    }
    return res;
}

dyn_array<ast_form*> parse_string(const string& src,
        istate* S) {
    std::istringstream in{src};
    return parse_input(&in, S);
}

dyn_array<ast_form*> parse_input(std::istream* in,
        istate* S) {
    u32 bytes_used;
    bool resumable;
    scanner sc{in};
    auto res = partial_parse_input(&sc, S, &bytes_used,
            &resumable);
    if (S->err_happened) {
        for (auto x : res) {
            free_ast_form(x);
        }
        return dyn_array<ast_form*>{};
    }
    return res;
}

dyn_array<ast_form*> partial_parse_input(scanner* sc,
        istate* S,
        u32* bytes_used,
        bool* resumable) {
    auto start = sc->tellg();
    auto pos = start;

    dyn_array<ast_form*> res;
    ast_form* a;
    while (!sc->eof_skip_ws()
            && (a = parse_next_form(sc, S, resumable))) {
        // pos holds the position after last successful read
        pos = sc->tellg();
        res.push_back(a);
    }
    if (S->err_happened) {
        *bytes_used = pos - start;
    }
    return res;
}

ast_form* pop_syntax(istate* S, const source_loc& loc) {
    auto v = peek(S);
    ast_form* res;
    if (vis_symbol(v)) {
        res = mk_symbol_form(loc, vsymbol(v));
    } else if (vis_number(v)) {
        res = mk_number_form(loc, vnumber(v));
    } else if (vis_string(v)) {
        res = mk_string_form(loc, (const char*)vstring(v)->data);
    } else if (vis_list(v)) {
        auto lst = v;
        dyn_array<ast_form*> a;
        while (lst != V_EMPTY) {
            push(S, vhead(lst));
            a.push_back(pop_syntax(S, loc));
            lst = vtail(lst);
        }
        res = mk_list_form(loc, &a);
    } else {
        ierror(S, "Cannot convert value to syntax\n");
        res = nullptr;
    }
    pop(S);

    return res;
}

}
