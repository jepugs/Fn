#include "istate.hpp"
#include "parse.hpp"

#include <sstream>
#include <cstring>

namespace fn {

using namespace fn;

static void parse_error(istate* S, const source_loc& loc, const string& msg) {
    std::ostringstream os;
    os << "File " << S->filename << ", line " << loc.line
       << ", col " << loc.col << ":\n  " << msg;
    ierror(S, os.str());
}

namespace ast {

node* mk_number(const source_loc& loc, f64 num) {
    return new node{loc, ak_number, num};
}

node* mk_string(const source_loc& loc, u32 str_id) {
    return new node{loc, ak_string, str_id};
}

node* mk_symbol(const source_loc& loc, u32 str_id) {
    return new node{loc, ak_symbol, str_id};
}

node* mk_list(const source_loc& loc, u32 list_length, node** lst) {
    return new node{loc, ak_list, list_length, lst};
}

node* mk_list(const source_loc& loc, const dyn_array<ast::node*>& lst) {
    auto len = lst.size;
    auto new_lst = new node*[len];
    for (u32 i = 0; i < len; ++i) {
        new_lst[i] = lst[i];
    }
    return mk_list(loc, len, new_lst);
}

node::node(const source_loc& loc, ast_kind k, f64 num)
    : loc{loc}
    , kind{k} {
    datum.num = num;
}
node::node(const source_loc& loc, ast_kind k, u32 str_id)
    : loc{loc}
    , kind{k} {
    datum.str_id = str_id;
}
// this takes ownership of list. It will be freed using delete[]
node::node(const source_loc& loc, ast_kind k, u32 list_length,
        node** list)
    : loc{loc}
    , kind{k}
    , list_length{list_length} {
    datum.list = new node*[list_length];
    for (u32 i = 0; i < list_length; ++i) {
        datum.list[i] = copy_graph(list[i]);
    }
}

node* copy_graph(const node* root) {
    node* res = nullptr;
    switch (root->kind) {
    case ak_list: {
        auto new_list = new node*[root->list_length];
        for (u32 i = 0; i < root->list_length; ++i) {
            new_list[i] = copy_graph(root->datum.list[i]);
        }
        res = mk_list(root->loc, root->list_length, new_list);
    }
        break;
    case ak_number:
        res = mk_number(root->loc, root->datum.num);
        break;
    case ak_string:
        res = mk_string(root->loc, root->datum.str_id);
        break;
    case ak_symbol:
        res = mk_symbol(root->loc, root->datum.str_id);
        break;
    }
    return res;
}

void free_graph(node* root) {
    if (root->kind == ak_list) {
        for (u32 i = 0; i < root->list_length; ++i) {
            free_graph(root->datum.list[i]);
        }
        delete[] root->datum.list;
    }
    delete root;
}


} // end namespace fn::ast

parser::parser(istate* S, scanner& sc)
    : S{S}
    , sst{&sc.get_sst()}
    , sc{&sc}
    , err_resumable{false} {
}

ast::node* parser::parse() {
    return parse_la(sc->next_token());
}

ast::node* parser::parse_la(const token& t0) {
    ast::node* res = nullptr;
    dyn_array<ast::node*> buf;
    auto& loc = t0.loc;

    switch (t0.kind) {
    case tk_eof:
        parse_error(S, loc, "Unexpected EOF.");
        err_resumable = true;
        return nullptr;

    case tk_number:
        res = ast::mk_number(loc, t0.d.num);
        break;
    case tk_string:
        res = ast::mk_string(loc, t0.d.str_id);
        break;
    case tk_symbol:
        res = ast::mk_symbol(loc, t0.d.str_id);
        break;

    case tk_lparen:
        // this will give res=nullptr and set err if there's an error
        if (!parse_to_delimiter(buf, tk_rparen)) {
            for (auto x : buf) {
                ast::free_graph(x);
            }
        } else {
            res = ast::mk_list(loc, buf);
        }
        break;
    case tk_rparen:
        parse_error(S, loc, "Unmatched delimiter ')'.");
        err_resumable = false;
        res = nullptr;
        break;
    case tk_lbrace:
        buf.push_back(ast::mk_symbol(loc, scanner_intern(*sst, "Table")));
        if (!parse_to_delimiter(buf, tk_rbrace)) {
            for (auto x : buf) {
                ast::free_graph(x);
            }
        } else {
            res = ast::mk_list(loc, buf);
        }
        break;
    case tk_rbrace:
        parse_error(S, loc, "Unmatched delimiter '}'.");
        err_resumable = false;
        res = nullptr;
        break;
    case tk_lbracket:
        buf.push_back(ast::mk_symbol(loc, scanner_intern(*sst, "List")));
        if (!parse_to_delimiter(buf, tk_rbracket)) {
            for (auto x : buf) {
                ast::free_graph(x);
            }
        } else {
            res = ast::mk_list(loc, buf);
        }
        break;
    case tk_rbracket:
        parse_error(S, loc, "Unmatched delimiter ']'.");
        err_resumable = false;
        res = nullptr;
        break;

    case tk_quote:
        res = parse_prefix(loc, "quote", sc->next_token());
        break;
    case tk_backtick:
        res = parse_prefix(loc, "quasiquote", sc->next_token());
        break;
    case tk_comma:
        res = parse_prefix(loc, "unquote", sc->next_token());
        break;
    case tk_comma_at:
        res = parse_prefix(loc, "unquote-splicing", sc->next_token());
        break;
    case tk_dollar_backtick:
        res = parse_prefix(loc, "dollar-fn", token{loc, tk_backtick});
        break;
    case tk_dollar_brace:
        res = parse_prefix(loc, "dollar-fn", token{loc, tk_lbrace}); 
        break;
    case tk_dollar_bracket:
        res = parse_prefix(loc, "dollar-fn", token{loc, tk_lbracket});
        break;
    case tk_dollar_paren:
        res = parse_prefix(loc, "dollar-fn", token{loc, tk_lparen});
        break;
    }
    return res;
}

ast::node* parser::parse_prefix(const source_loc& loc, const string& op,
        const token& t0) {
    dyn_array<ast::node*> buf;
    buf.push_back(ast::mk_symbol(loc, scanner_intern(*sst, op)));
    auto x = parse_la(t0);
    if (!x) {
        ast::free_graph(buf[0]);
        return nullptr;
    }

    buf.push_back(x);
    return ast::mk_list(loc, buf);
}

bool parser::parse_to_delimiter(dyn_array<ast::node*>& buf,
        token_kind end) {
    auto tok = sc->next_token();
    while (tok.kind != end) {
        if (tok.kind == tk_eof) {
            parse_error(S, tok.loc,
                    "Encountered EOF while expecting closing delimiter.");
            err_resumable = true;
            return false;
        };
        auto x = parse_la(tok);
        if (!x) {
            return false;
        }
        buf.push_back(x);
        tok = sc->next_token();
    }

    return true;
}

ast::node* parse_next_node(istate* S, scanner& sc, bool* resumable) {
    parser p{S, sc};
    auto res = p.parse();
    *resumable = p.err_resumable;
    return res;
}

ast::node* pop_syntax(istate* S, scanner_string_table& sst,
        const source_loc& loc) {
    auto v = peek(S);
    ast::node* res;
    if (vis_symbol(v)) {
        res = ast::mk_symbol(loc, scanner_intern(sst, symname(S, vsymbol(v))));
    } else if (vis_number(v)) {
        res = ast::mk_number(loc, vnumber(v));
    } else if (vis_string(v)) {
        res = ast::mk_string(loc,
                scanner_intern(sst, (const char*)vstring(v)->data));
    } else if (vis_list(v)) {
        auto lst = v;
        dyn_array<ast::node*> buf;
        while (lst != V_EMPTY) {
            push(S, vhead(lst));
            buf.push_back(pop_syntax(S, sst, loc));
            lst = vtail(lst);
        }
        res = mk_list(loc, buf);
    } else {
        ierror(S, "Cannot convert value to syntax\n");
        res = nullptr;
    }
    pop(S);

    return res;
}

}
