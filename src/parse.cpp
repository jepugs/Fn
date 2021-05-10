#include "parse.hpp"

namespace fn_parse {

using namespace fn;
using namespace fn_scan;

ast_atom::ast_atom(f64 num)
    : type{at_number} {
    datum.num = num;
}

ast_atom::ast_atom(const fn_string& str)
    : type{at_string} {
    datum.str = new fn_string{str};
}

ast_atom::ast_atom(fn_string&& str)
    : type{at_string} {
    datum.str = new fn_string{str};
}

ast_atom::ast_atom(const symbol& sym)
    : type{at_symbol} {
    datum.sym = sym.id;
}

ast_atom::ast_atom(const ast_atom& at)
    : type{at.type} {
    if (type == at_string) {
        datum.str = new fn_string{*at.datum.str};
    } else {
        datum = at.datum;
    }
}

ast_atom::ast_atom(ast_atom&& at)
    : type{at.type}
    , datum{at.datum} {
    datum = at.datum;
    // so the destructor doesn't free the string
    at.type = at_number;
}

ast_atom& ast_atom::operator=(const ast_atom& at) {
    if (type == at_string) {
        delete datum.str;
    }
    type = at.type;
    if (type == at_string) {
        datum.str = new fn_string{*at.datum.str};
    } else {
        datum = at.datum;
    }

    return *this;
}

ast_atom& ast_atom::operator=(ast_atom&& at) {
    if (type == at_string) {
        delete datum.str;
    }
    type = at.type;
    datum = at.datum;

    // make sure that the associated string isn't freed
    at.type = at_number;

    return *this;
}


ast_atom::~ast_atom() {
    if (type == at_string) {
        delete datum.str;
    }
}

ast_node::ast_node(const source_loc& loc)
    : loc{loc}
    , kind{ak_error}
    , datum{.atom = nullptr} {
}

ast_node::ast_node(const ast_atom& at, const source_loc& loc)
    : loc{loc}
    , kind{ak_atom}
    , datum{.atom = new ast_atom{at}} {
}

ast_node::ast_node(const vector<ast_node*>& list, const source_loc& loc)
    : loc{loc}
    , kind{ak_list}
    , datum{.list = new vector<ast_node*>(list)} {
}

ast_node::~ast_node() {
    switch (kind) {
    case ak_atom:
        delete datum.atom;
        break;
    case ak_error:
        break;
    case ak_list:
        for (auto ptr : *datum.list) {
            delete ptr;
        }
        delete datum.list;
        break;
    }
}

ast_node* ast_node::copy() const {
    switch (kind) {
    case ak_atom:
        return new ast_node{ast_atom{*datum.atom}, loc};
    case ak_error:
        return new ast_node{loc};
    case ak_list:
        break;
    }

    // list behavior
    vector<ast_node*> list;
    for (auto v : *datum.list) {
        list.push_back(v->copy());
    }
    return new ast_node{list, loc};
}

static string print_grouped(const symbol_table& symtab,
                            char open,
                            char close,
                            const vector<ast_node*>& list) {
    string res{open};
    if (list.size() == 0) {
        return res + close;
    }
    u32 u;
    ast_node* node;
    for (u = 0; u < list.size() - 1; ++u) {
        node = list[u];
        res = res + node->as_string(symtab) + " ";
    }
    node = list[u];
    res = res + node->as_string(symtab) + close;
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

string ast_node::as_string(const symbol_table& symtab) const {
    string res = "";
    switch (kind) {
    case ak_atom:
        switch (datum.atom->type) {
        case at_number:
            return std::to_string(datum.atom->datum.num);
            break;
        case at_string:
            return "\""
                + with_str_escapes(datum.atom->datum.str->as_string())
                + "\"";
            break;
        case at_symbol:
            return with_escapes(symtab[datum.atom->datum.sym].name);
            break;
        }
        break;

    case ak_list:
        res = print_grouped(symtab, '(', ')', *datum.list);
        break;

    case ak_error:
        break;
    }

    return res;
}

bool ast_node::is_symbol() const {
    return kind == ak_atom && datum.atom->type == at_symbol;
}

bool ast_node::is_keyword(const symbol_table& symtab) const {
    if (!is_symbol()) {
        return false;
    }
    auto& x = symtab[datum.atom->datum.sym];
    return x.name.length() > 0 && x.name[0] == ':';
}

const symbol& ast_node::get_symbol(const symbol_table& symtab) const {
    return symtab[datum.atom->datum.sym];
}



#define parse_error(msg, loc) throw fn_error("fn_parse", msg, loc)

void parse_to_delimiter(scanner& sc,
                        symbol_table& symtab,
                        vector<ast_node*>& buf,
                        token_kind end) {
    auto tok = sc.next_token();
    while (tok.tk != end) {
        if (tok.tk == tk_eof) {
            parse_error("Unexpected EOF searching for delimiter.", tok.loc);
        }
        buf.push_back(parse_node(sc, symtab, std::make_optional(tok)));
        tok = sc.next_token();
    }
}

void parse_prefix(scanner& sc,
                  symbol_table& symtab,
                  vector<ast_node*>& buf,
                  const string& op,
                  const source_loc& loc,
                  optional<token> t0 = std::nullopt) {
    buf.push_back(new ast_node{ast_atom{*(symtab.intern(op))},
                               loc});
    buf.push_back(parse_node(sc, symtab, t0));
}

ast_node* parse_node(scanner& sc, symbol_table& symtab, optional<token> t0) {
    auto tok = t0.has_value() ? *t0 : sc.next_token();
    ast_node* res = nullptr;
    string* str = tok.datum.str;
    vector<ast_node*> buf;

    ast_atom at{1.0};

    switch (tok.tk) {
    case tk_eof:
        return nullptr;

    case tk_number:
        res = new ast_node{ast_atom{tok.datum.num}, tok.loc};
        break;
    case tk_string:
        // at = ast_atom(fn_string(*str));
        // res = new ast_node(at, tok.loc);
        res = new ast_node{ast_atom{fn_string{*str}}, tok.loc};
        break;
    case tk_symbol:
        res = new ast_node{ast_atom{*symtab.intern(*str)}, tok.loc};
        break;

    case tk_lparen:
        parse_to_delimiter(sc, symtab, buf, tk_rparen);
        res = new ast_node{buf, tok.loc};
        break;
    case tk_rparen:
        parse_error("Unmatched delimiter ')'.", tok.loc);
        break;
    case tk_lbrace:
        buf.push_back(new ast_node{ast_atom{*symtab.intern("Table")}, tok.loc});
        parse_to_delimiter(sc, symtab, buf, tk_rbrace);
        res = new ast_node{buf, tok.loc};
        break;
    case tk_rbrace:
        parse_error("Unmatched delimiter '}'.", tok.loc);
        break;
    case tk_lbracket:
        buf.push_back(new ast_node{ast_atom{*symtab.intern("List")}, tok.loc});
        parse_to_delimiter(sc, symtab, buf, tk_rbracket);
        res = new ast_node{buf, tok.loc};
        break;
    case tk_rbracket:
        parse_error("Unmatched delimiter ']'.", tok.loc);
        break;

    case tk_dot:
        buf.push_back(new ast_node{ast_atom{*symtab.intern("dot")}, tok.loc});
        for (auto s : *tok.datum.ids) {
            buf.push_back(new ast_node{ast_atom{*symtab.intern(s)}, tok.loc});
        }
        res = new ast_node{buf, tok.loc};
        break;

    case tk_quote:
        parse_prefix(sc, symtab, buf, "quote", tok.loc);
        res = new ast_node{buf, tok.loc};
        break;
    case tk_backtick:
        parse_prefix(sc, symtab, buf, "quasiquote", tok.loc);
        res = new ast_node{buf, tok.loc};
        break;
    case tk_comma:
        parse_prefix(sc, symtab, buf, "unquote", tok.loc);
        res = new ast_node{buf, tok.loc};
        break;
    case tk_comma_at:
        parse_prefix(sc, symtab, buf, "unquote-splicing", tok.loc);
        res = new ast_node{buf, tok.loc};
        break;

    case tk_dollar_backtick:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_backtick, tok.loc});
        res = new ast_node{buf, tok.loc};
        break;
    case tk_dollar_brace:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_lbrace, tok.loc});
        res = new ast_node{buf, tok.loc};
        break;
    case tk_dollar_bracket:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_lbracket, tok.loc});
        res = new ast_node{buf, tok.loc};
        break;
    case tk_dollar_paren:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token{tk_lparen, tok.loc});
        res = new ast_node{buf, tok.loc};
        break;
    }
    return res;
}

#define check_name(name) (symtab.intern(name)->id == sym)

static bool is_legal_name(symbol_table& symtab, symbol_id sym) {
    bool reserved =
        (check_name("&") || check_name("and") || check_name("cond")
         || check_name("def") || check_name("defmacro") || check_name("defn")
         || check_name("do") || check_name("dollar-fn") || check_name("dot")
         || check_name("fn") || check_name("if") || check_name("import")
         || check_name("let") || check_name("letfn") || check_name("ns")
         || check_name("or") || check_name("quasiquote") || check_name("quote")
         || check_name("unquote") || check_name("unquote-splicing")
         || check_name("set") || check_name("with"));
    if (reserved) {
        return false;
    }

    auto& s = symtab[sym];
    // keywords
    if (s.name[0] == ':') {
        return false;
    }

    return true;
}

// it's not great writing monster functions like this, but at the time of
// writing, I don't see a shorter way to write it that can still generate
// errors at this level of granularity.
param_list parse_params(symbol_table& symtab, const ast_node& form) {
    if (form.kind != ak_list) {
        parse_error("Found atom instead of parameter list.", form.loc);
    }

    auto& list = *form.datum.list;
    auto amp = symtab.intern("&")->id;
    auto kamp = symtab.intern(":&")->id;

    param_list res;
    auto& pos = res.positional;
    // save a list of names for duplicate detection
    vector<symbol_id> names;

    // first, get parameters without init forms (so just symbols)
    u32 i = 0;
    while (i < list.size()) {
        if (list[i]->is_symbol()) {
            auto sym = list[i]->datum.atom->datum.sym;
            // important: check for ampersand before checking for legal name
            if (sym == amp || sym == kamp) {
                break;
            }
            if (!is_legal_name(symtab, sym)) {
                parse_error("Illegal name in parameter list.", list[i]->loc);
            }
            for (auto x : names) {
                if (x == sym) {
                    parse_error("Duplicate name in parameter list.", list[i]->loc);
                }
            }
            names.push_back(sym);
            pos.push_back(parameter{sym});
        } else {
            break;
        }
        ++i;
    }

    // parameters with init form
    while (i < list.size()) {
        if (list[i]->kind == ak_list) {
            // check syntax
            auto& x = *list[i]->datum.list;
            if (x.size() != 2 || !x[0]->is_symbol()) {
                parse_error("Illegal element in parameter list.", list[i]->loc);
            }
            auto sym = x[0]->datum.atom->datum.sym;
            if (!is_legal_name(symtab, sym)) {
                parse_error("Illegal name in parameter list.", list[i]->loc);
            }
            for (auto x : names) {
                if (x == sym) {
                    parse_error("Duplicate name in parameter list.", list[i]->loc);
                }
            }
            names.push_back(sym);
            pos.push_back(parameter{sym, x[1]});
        } else {
            break;
        }
        ++i;
    }

    // check for variadic arguments
    while (i < list.size()) {
        if (!list[i]->is_symbol()) {
            parse_error("Illegal element in parameter list.", list[i]->loc);
        }
        auto sym = list[i]->datum.atom->datum.sym;
        if (sym == amp) {
            if (res.var_list.has_value()) {
                parse_error("Two occurences of & in parameter list.", list[i]->loc);
            } else if (list.size() <= i+1) {
                parse_error("Missing variadic parameter name.", list[i]->loc);
            } else if (!list[i+1]->is_symbol()) {
                parse_error("Variadic parameter name must be a symbol.", list[i+1]->loc);
            }

            res.var_list = list[i+1]->datum.atom->datum.sym;
            if (!is_legal_name(symtab, *res.var_list)) {
                parse_error("Illegal name in parameter list.", list[i]->loc);
            }
            for (auto x : names) {
                if (x == *res.var_list) {
                    parse_error("Duplicate name in parameter list.", list[i]->loc);
                }
            }
            names.push_back(*res.var_list);
        } else if (sym == kamp) {
            if (res.var_table.has_value()) {
                parse_error("Two occurences of :& in parameter list.", list[i]->loc);
            } else if (list.size() <= i+1) {
                parse_error("Missing variadic parameter name.", list[i]->loc);
            } else if (!list[i+1]->is_symbol()) {
                parse_error("Variadic parameter name must be a symbol.", list[i+1]->loc);
            }

            res.var_table = list[i+1]->datum.atom->datum.sym;
            if (!is_legal_name(symtab, *res.var_table)) {
                parse_error("Illegal name in parameter list.", list[i]->loc);
            }
            for (auto x : names) {
                if (x == *res.var_table) {
                    parse_error("Duplicate name in parameter list.", list[i]->loc);
                }
            }
            names.push_back(*res.var_table);
        } else {
            parse_error("Required parameters must come at the beginning of the parameter list.",
                        list[i]->loc);
        }
        // go two at a time on these
        i += 2;
    }

    if (i < list.size()) {
        parse_error("Illegal element in parameter list.", list[i]->loc);
    }

    return res;
}

}

