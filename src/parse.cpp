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

ast_atom::ast_atom(const symbol& sym)
    : type(at_number) {
    datum.sym = sym.id;
}

ast_atom::ast_atom(const ast_atom& at)
    : type(at.type) 
    , datum(at.datum) {
    datum = at.datum;
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
        delete datum.str;
    }
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
    if (kind == ak_atom) {
        delete datum.atom;
    } else {
        for (auto ptr : *datum.list) {
            delete ptr;
        }
        delete datum.list;
    }
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

ast_node* parse_node(scanner* sc, symbol_table* symtab, optional<token> t0) {
    auto tok = t0.has_value() ? *t0 : sc->next_token();
    ast_node* res = nullptr;
    string* str = tok.datum.str;
    vector<ast_node*> buf;

    switch (tok.tk) {
    case tk_eof:
        return nullptr;

    case tk_number:
        res = new ast_node(ast_atom(tok.datum.num), tok.loc);
        break;
    case tk_string:
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
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("Object")), tok.loc));
        parse_to_delimiter(sc, symtab, buf, tk_rparen);
        res = new ast_node(buf, tok.loc);
        break;
    case tk_rbrace:
        parse_error("Unmatched delimiter '}'.", tok.loc);
        break;
    case tk_lbracket:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("List")), tok.loc));
        parse_to_delimiter(sc, symtab, buf, tk_rparen);
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
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("quote")), tok.loc));
        buf.push_back(parse_node(sc,symtab));
        res = new ast_node(buf, tok.loc);
        break;
    case tk_backtick:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("quasiquote")), tok.loc));
        buf.push_back(parse_node(sc,symtab));
        res = new ast_node(buf, tok.loc);
        break;
    case tk_comma:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("unquote")), tok.loc));
        buf.push_back(parse_node(sc,symtab));
        res = new ast_node(buf, tok.loc);
        break;
    case tk_comma_at:
        buf.push_back(new ast_node(ast_atom(*(*symtab).intern("unquote-splicing")), tok.loc));
        buf.push_back(parse_node(sc,symtab));
        res = new ast_node(buf, tok.loc);
        break;

    case tk_dollar_backtick:
    case tk_dollar_brace:
    case tk_dollar_bracket:
    case tk_dollar_paren:
        parse_error("Unimplemented syntax (token_kind = " + tok.tk + string(")"),
                    tok.loc);
        break;
    }
    return res;
}

}

