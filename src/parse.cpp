#include "parse.hpp"

namespace fn_parse {

using namespace fn;
using namespace fn_scan;

ast_atom::ast_atom(f64 num)
    : type(at_number) {
    datum.num = num;
}

ast_atom::ast_atom(const fn_string& str)
    : type(at_string) {
    datum.str = new fn_string(str);
}

ast_atom::ast_atom(fn_string&& str)
    : type(at_string) {
    datum.str = new fn_string(str);
}

ast_atom::ast_atom(const symbol& sym)
    : type(at_symbol) {
    datum.sym = sym.id;
}

ast_atom::ast_atom(const ast_atom& at)
    : type(at.type) {
    if (type == at_string) {
        datum.str = new fn_string(*at.datum.str);
    } else {
        datum = at.datum;
    }
}

ast_atom::ast_atom(ast_atom&& at)
    : type(at.type) 
    , datum(at.datum) {
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
        datum.str = new fn_string(*at.datum.str);
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
        //delete datum.str;
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
    , datum{.atom = new ast_atom(at)} {
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

string ast_node::as_string(const symbol_table* symtab) {
    string res = "";
    switch (kind) {
    case ak_atom:
        switch (datum.atom->type) {
        case at_number:
            return std::to_string(datum.atom->datum.num);
            break;
        case at_string:
            return "\"" + datum.atom->datum.str->as_string() + "\"";
            break;
        case at_symbol:
            return (*symtab)[datum.atom->datum.sym].name;
            break;
        }
        break;

    case ak_list:
        res = "(";
        for (auto x : *datum.list) {
            res = res + x->as_string(symtab) + " ";
        }
        res = res + ")";
        break;

    case ak_error:
        break;
    }

    return res;
}


#define parse_error(msg, loc) throw fn_error("fn_parse", msg, loc)

void parse_to_delimiter(scanner* sc,
                        symbol_table* symtab,
                        vector<ast_node*>& buf,
                        token_kind end) {
    auto tok = sc->next_token();
    while (tok.tk != end) {
        if (tok.tk == tk_eof) {
            parse_error("Unexpected EOF searching for delimiter.", tok.loc);
        }
        buf.push_back(parse_node(sc, symtab, std::make_optional(tok)));
        tok = sc->next_token();
    }
}

void parse_prefix(scanner* sc,
                  symbol_table* symtab,
                  vector<ast_node*>& buf,
                  const string& op,
                  const source_loc& loc,
                  optional<token> t0 = std::nullopt) {
    buf.push_back(new ast_node(ast_atom(*(symtab->intern(op))),
                               loc));
    buf.push_back(parse_node(sc, symtab, t0));
}

ast_node* parse_node(scanner* sc, symbol_table* symtab, optional<token> t0) {
    auto tok = t0.has_value() ? *t0 : sc->next_token();
    ast_node* res = nullptr;
    string* str = tok.datum.str;
    vector<ast_node*> buf;

    ast_atom at(1.0);

    switch (tok.tk) {
    case tk_eof:
        return nullptr;

    case tk_number:
        res = new ast_node(ast_atom(tok.datum.num), tok.loc);
        break;
    case tk_string:
        // at = ast_atom(fn_string(*str));
        // res = new ast_node(at, tok.loc);
        res = new ast_node(ast_atom(fn_string(*str)), tok.loc);
        break;
    case tk_symbol:
        res = new ast_node(ast_atom(*(*symtab).intern(*str)), tok.loc);
        break;

    case tk_lparen:
        parse_to_delimiter(sc, symtab, buf, tk_rparen);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_rparen:
        parse_error("Unmatched delimiter ')'.", tok.loc);
        break;
    case tk_lbrace:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("Table")), tok.loc));
        parse_to_delimiter(sc, symtab, buf, tk_rbrace);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_rbrace:
        parse_error("Unmatched delimiter '}'.", tok.loc);
        break;
    case tk_lbracket:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("List")), tok.loc));
        parse_to_delimiter(sc, symtab, buf, tk_rbracket);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_rbracket:
        parse_error("Unmatched delimiter ']'.", tok.loc);
        break;

    case tk_dot:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("dot")), tok.loc));
        parse_error("Unimplemented syntax (token_kind = " + tok.tk + string(")"),
                    tok.loc);
        break;

    case tk_quote:
        parse_prefix(sc, symtab, buf, "quote", tok.loc);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_backtick:
        parse_prefix(sc, symtab, buf, "quasiquote", tok.loc);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_comma:
        parse_prefix(sc, symtab, buf, "unquote", tok.loc);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_comma_at:
        parse_prefix(sc, symtab, buf, "unquote-splicing", tok.loc);
        res = new ast_node(buf, tok.loc);
        break;

    case tk_dollar_backtick:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token(tk_backtick, tok.loc));
        res = new ast_node(buf, tok.loc);
        break;
    case tk_dollar_brace:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token(tk_lbrace, tok.loc));
        res = new ast_node(buf, tok.loc);
        break;
    case tk_dollar_bracket:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token(tk_lbracket, tok.loc));
        res = new ast_node(buf, tok.loc);
        break;
    case tk_dollar_paren:
        parse_prefix(sc, symtab, buf, "dollar-fn", tok.loc,
                     token(tk_lparen, tok.loc));
        res = new ast_node(buf, tok.loc);
        break;
    }
    return res;
}

}

