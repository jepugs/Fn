#include "compile.hpp"

#include <memory>

namespace fn {

using namespace fn_scan;
using namespace fn_bytes;

locals::locals(locals* parent, func_stub* func)
    : vars()
    , parent(parent)
    , cur_func(func)
{ }

fs::path compiler::module_path(const vector<string>& id) {
    // t_od_o: i think this works on windows but i should really try it :)
    auto res = dir;
    for (auto s : id) {
        res /= s;
    }
    res += ".fn";
    return res;
}

// f_ix_me: this is probably a dumb algorithm
template<> u32 hash<vector<string>>(const vector<string>& v) {
    constexpr u32 p = 13729;
    u32 ct = 1;
    u32 res = 0;
    for (auto s : v) {
        res ^= (hash(s) + ct*p);
        ++ct;
    }
    return res;
}

// levels must be >= 1 and must be <= the depth of nested function bodies
u8 locals::add_upvalue(u32 levels, u8 pos) {
    // get to the next call frame
    // add an upvalue to cur_fun
    // climb up until src_env is reached, adding upvalues to each function along the way

    // find the most recent call frame
    auto call = this;
    while (call->cur_func == nullptr && call != nullptr) {
        call = call->parent;
    }

    // levels == 1 => this is a direct upvalue, so add it and return
    if (levels == 1) {
        return call->cur_func->get_upvalue(pos, true);
    }

    // levels > 1 => need to get the upvalue from an enclosing function
    u8 slot = call->parent->add_upvalue(levels - 1, pos);
    return call->cur_func->get_upvalue(slot, false);
}

static inline bool is_legal_name(const string& str) {
    if (str == "and" || str == "cond" || str == "def" || str == "def*" || str == "defmacro"
        || str == "defsym" || str == "do" || str == "dollar-fn" || str == "dot" || str == "fn"
        || str == "if" || str == "import" || str == "let" || str == "macrolet" || str == "or"
        || str == "quasi-quote" || str == "quote" || str == "set" || str == "symlet"
        || str == "unquote" || str == "unquote-splicing"
        || str == "null" || str == "false" || str == "true" || str == "ns" || str == "&") {
        return false;
    }
    return true;
}

compiler::compiler(const fs::path& dir, bytecode* dest, scanner* sc)
    : dest(dest)
    , sc(sc)
    , sp(0)
    , dir(dir)
    , modules()
{
    // the first module is fn.core
    // t_od_o: use allocator
    auto mod_id_val = dest->cons_const(dest->symbol("core"), V_EMPTY);
    mod_id_val = dest->cons_const(dest->symbol("fn"), dest->get_constant(mod_id_val));
    cur_mod_id = mod_id_val;
}

compiler::~compiler() {
    // t_od_o: free locals at least or something
}

static inline bool is_right_delim(token tok) {
    auto tk = tok.tk;
    return tk == tk_rbrace || tk == tk_rbracket || tk == tk_rparen;
}

// returns true when
static bool check_delim(token_kind expected, token tok) {
    if (tok.tk == expected) {
        return true;
    } else if (is_right_delim(tok)) {
        throw fn_error("compiler", "mismatched closing delimiter " + tok.to_string(), tok.loc);
    } else if (tok.tk == tk_eof) {
        throw fn_error("compiler", "encountered e_of while scanning", tok.loc);
    }
    return false;
}

optional<local_addr> compiler::find_local(locals* l, const string& name, u32* levels) {
    if (l == nullptr) {
        return { };
    }

    auto env = l;
    optional<u8*> res;
    *levels = 0;
    // keep track of how many enclosing functions we need to go into
    do {
        res = env->vars.get(name);
        if (res.has_value()) {
            break;
        }

        // here we're about to ascend to an enclosing function, so we need an upvalue
        if (env->cur_func != nullptr) {
            *levels += 1;
        }
    } while ((env = env->parent) != nullptr);

    if (*levels > 0 && res.has_value()) {
        return l->add_upvalue(*levels, **res);
    } else if (res.has_value()) {
        return **res;
    }

    return { };
}

void compiler::compile_var(locals* l, const string& name) {
    u32 levels;
    auto id = find_local(l, name, &levels);
    if (!id.has_value()) {
        // global
        auto id = dest->sym_const(name);
        dest->write_byte(OP_CONST);
        dest->write_short(id);
        dest->write_byte(OP_GLOBAL);
    } else {
        dest->write_byte(levels > 0 ? OP_UPVALUE : OP_LOCAL);
        dest->write_byte(*id);
    }
    ++sp;
}

// helper function that converts the string from a tk_dot token to a vector consisting of the names
// of its parts.
static inline vector<string> tokenize_dot_string(const string& s) {
    vector<string> res;
    u32 start = 0;
    u32 dot_pos = 0;
    bool escaped = false;

    while (dot_pos < s.size()) {
        // find the next unescaped dot
        while (dot_pos < s.size()) {
            ++dot_pos;
            if (escaped) {
                escaped = false;
            } else if (s[dot_pos] == '\\') {
                escaped = true;
            } else if (s[dot_pos] == '.') {
                break;
            }
        }
        res.push_back(s.substr(start, dot_pos-start));
        start = dot_pos+1;
        ++dot_pos;
    }
    return res;
}

vector<string> compiler::tokenize_name(optional<token> t0) {
    token tok;
    if (t0.has_value()) {
        tok = *t0;
    } else {
        tok = sc->next_token();
    }

    if (tok.tk == tk_symbol) {
        vector<string> v;
        v.push_back(*tok.datum.str);
        return v;
    }
    if (tok.tk == tk_dot) {
        return tokenize_dot_string(*tok.datum.str);
    }

    if (tok.tk != tk_lparen) {
        // not a symbol or a dot form
        throw fn_error("compiler",
                      "name is not a symbol or a dot form: " + tok.to_string(),
                      tok.loc);
    }

    tok = sc->next_token();
    if (tok.tk != tk_symbol || *tok.datum.str != "dot") {
        throw fn_error("compiler",
                      "name is not a symbol or a dot form",
                      tok.loc);
    }

    vector<string> res;
    tok = sc->next_token();
    while (!check_delim(tk_rparen, tok)) {
        if (tok.tk != tk_symbol) {
            throw fn_error("compiler", "arguments to dot must be symbols.", tok.loc);
        }
        res.push_back(*tok.datum.str);
        tok = sc->next_token();
    }
    return res;
}


void compiler::compile_block(locals* l) {
    auto tok = sc->next_token();
    // location on stack to put result
    auto old_sp = sp;
    dest->write_byte(OP_NULL);
    ++sp;
    if(check_delim(tk_rparen, tok)) {
        // empty body yields a null value
        return;
    }

    // create a new environment
    auto new_env = new locals(l);

    compile_expr(new_env, &tok);
    while (!check_delim(tk_rparen, tok = sc->next_token())) {
        dest->write_byte(OP_POP);
        --sp;
        compile_expr(new_env, &tok);
    }

    dest->write_byte(OP_SET_LOCAL);
    dest->write_byte(old_sp);
    --sp;
    dest->write_byte(OP_CLOSE);
    dest->write_byte(sp - old_sp - 1);

    sp = old_sp + 1;

    // don't need this any more :)
    delete new_env;
}

void compiler::compile_and(locals* l) {
    forward_list<bc_addr> patch_locs;

    auto tok = sc->next_token();
    if(check_delim(tk_rparen, tok)) {
        // and with no arguments yields a true value
        dest->write_byte(OP_TRUE);
        ++sp;
        return;
    }

    compile_expr(l, &tok);
    // copy the top of the stack because cjump consumes it
    dest->write_byte(OP_COPY);
    dest->write_byte(0);
    dest->write_byte(OP_CJUMP);
    dest->write_short(0);
    patch_locs.push_front(dest->get_size());
    while (!check_delim(tk_rparen, (tok=sc->next_token()))) {
        dest->write_byte(OP_POP);
        compile_expr(l, &tok);
        dest->write_byte(OP_COPY);
        dest->write_byte(0);
        dest->write_byte(OP_CJUMP);
        dest->write_short(0);
        patch_locs.push_front(dest->get_size());
    }
    dest->write_byte(OP_JUMP);
    dest->write_short(2);
    auto end_addr = dest->get_size();
    dest->write_byte(OP_POP);
    dest->write_byte(OP_FALSE);

    for (auto u : patch_locs) {
        dest->patch_short(u-2, end_addr - u);
    }
}

void compiler::compile_apply(locals* l) {
    auto old_sp = sp;

    auto tok = sc->next_token();
    if (check_delim(tk_rparen, tok)) {
        throw fn_error("compiler", "too few arguments to apply.", tok.loc);
    }
    compile_expr(l, &tok);

    tok = sc->next_token();
    if (check_delim(tk_rparen, tok)) {
        throw fn_error("compiler", "too few arguments to apply.", tok.loc);
    }
    u32 num_args = 0;
    do {
        ++num_args;
        compile_expr(l, &tok);
    } while (!check_delim(tk_rparen,tok=sc->next_token()));
    if (num_args > 255) {
        throw fn_error("compiler", "too many arguments to apply.", tok.loc);
    }
    dest->write_byte(OP_APPLY);
    dest->write_byte(num_args);

    sp = old_sp+1;
}

void compiler::compile_cond(locals* l) {
    auto tok = sc->next_token();
    if (check_delim(tk_rparen, tok)) {
        dest->write_byte(OP_NULL);
        ++sp;
        return;
    }
    // locations where we need to patch the end address
    forward_list<bc_addr> patch_locs;
    while (!check_delim(tk_rparen, tok)) {
        compile_expr(l,&tok);
        --sp;
        dest->write_byte(OP_CJUMP);
        dest->write_short(0);
        auto patch_addr = dest->get_size();
        compile_expr(l);
        --sp;
        dest->write_byte(OP_JUMP);
        dest->write_short(0);
        patch_locs.push_front(dest->get_size());

        // patch in the else jump address
        dest->patch_short(patch_addr-2,dest->get_size() - patch_addr);
        tok = sc->next_token();
    }

    // return null when no tests success
    dest->write_byte(OP_NULL);
    ++sp;
    // patch in the end address for non-null results
    for (auto a : patch_locs) {
        dest->patch_short(a-2, dest->get_size() - a);
    }
}

// compile def expressions
void compiler::compile_def(locals* l) {
    token tok = sc->next_token();
    if (tok.tk != tk_symbol) {
        throw fn_error("compiler", "first argument to def must be a symbol.", tok.loc);
    }
    // t_od_o: check for legal symbols
    if(!is_legal_name(*tok.datum.str)) {
        throw fn_error("compiler", "illegal variable name " + *tok.datum.str, tok.loc);
    }

    // write the name symbol
    constant(dest->sym_const(*tok.datum.str));
    ++sp;
    // compile the value expression
    compile_expr(l);
    // set the global. this leaves the symbol on the stack
    dest->write_byte(OP_SET_GLOBAL);
    --sp;

    // make sure there's a close paren
    token last = sc->next_token();
    if (!check_delim(tk_rparen, last)) {
        throw fn_error("compiler", "too many arguments to def", last.loc);
    }

}

void compiler::compile_do(locals* l) {
    compile_block(l);
}

void compiler::compile_dot_token(locals* l, token& tok) {
    auto toks = tokenize_dot_string(*tok.datum.str);
    compile_var(l,toks[0]);
    // note: this compile_var call already sets sp to what we want it at the end
    for (u32 i = 1; i < toks.size(); ++i) {
        dest->write_byte(OP_CONST);
        dest->write_short(dest->sym_const(toks[i]));
        dest->write_byte(OP_OBJ_GET);
    }
}

void compiler::compile_dot_expr(locals* l) {
    vector<string> toks;

    auto tok = sc->next_token();
    if (check_delim(tk_rparen, tok)) {
        throw fn_error("compiler", "too few arguments to dot.", tok.loc);
    }
    while (!check_delim(tk_rparen, tok)) {
        if (tok.tk != tk_symbol) {
            throw fn_error("compiler", "arguments to dot must be symbols.", tok.loc);
        }
        toks.push_back(*tok.datum.str);
        tok = sc->next_token();
    }
    compile_var(l,toks[0]);
    // note: this compile_var call already sets sp to what we want it at the end
    for (u32 i = 1; i < toks.size(); ++i) {
        constant(dest->sym_const(toks[i]));
        dest->write_byte(OP_OBJ_GET);
    }
}

void compiler::compile_fn(locals* l) {
    // first, read all arguments and set up l
    token tok = sc->next_token();
    if (tok.tk != tk_lparen) {
        throw fn_error("compiler", "second argument of fn must be an argument list.", tok.loc);
    }

    // start out by jumping to the end of the function body. we will patch in the distance to jump
    // later on.
    dest->write_byte(OP_JUMP);
    auto patch_addr = dest->get_size();
    // write the placholder offset
    dest->write_short(0);

    auto enclosed = new locals(l);
    auto old_sp = sp;
    sp = 0;

    bool vararg = false;
    // t_od_o: add new function object
    // t_od_o: check args < 256
    while (!check_delim(tk_rparen, tok=sc->next_token())) {
        if (tok.tk != tk_symbol) {
            throw fn_error("compiler", "argument names must be symbols.", tok.loc);
        }
        // & indicates a variadic argument
        if (*tok.datum.str == "&") {
            vararg = true;
            break;
        } else if (!is_legal_name(*tok.datum.str)) {
            throw fn_error("compiler", "illegal variable name " + *tok.datum.str, tok.loc);
        }

        // t_od_o: check for repeated names
        enclosed->vars.insert(*tok.datum.str, sp);
        ++sp;
    }

    if (vararg) {
        // check that we have a symbol for the variable name
        tok = sc->next_token();
        if (tok.tk != tk_symbol) {
            throw fn_error("compiler", "argument names must be symbols.", tok.loc);
        }
        enclosed->vars.insert(*tok.datum.str, sp);
        ++sp;

        tok = sc->next_token();
        if (!check_delim(tk_rparen, tok)) {
            throw fn_error("compiler",
                          "trailing tokens after variadic parameter in fn argument list.",
                          tok.loc);
        }
    }

    auto func_id = dest->add_function(sp, vararg, dest->get_constant(cur_mod_id));
    enclosed->cur_func = dest->get_function(func_id);

    // compile the function body
    compile_block(enclosed);
    dest->write_byte(OP_RETURN);

    delete enclosed;

    // f_ix_me: since jump takes a signed offset, need to ensure that offset is positive if converted
    // to a signed number. otherwise emit an error.
    auto offset = dest->get_size() - patch_addr - 2;
    dest->patch_short(patch_addr, offset);

    dest->write_byte(OP_CLOSURE);
    dest->write_short(func_id);
    sp = old_sp + 1;
}

void compiler::compile_if(locals* l) {
    // compile the test
    compile_expr(l);
    dest->write_byte(OP_CJUMP);
    --sp;
    // this will hold the else address
    dest->write_short(0);

    // then clause
    auto then_addr = dest->get_size();
    compile_expr(l);
    --sp;
    // jump to the end of the if
    dest->write_byte(OP_JUMP);
    dest->write_short(0);

    // else clause
    auto else_addr = dest->get_size();
    compile_expr(l);
    // sp is now where we want it

    dest->patch_short(then_addr - 2, else_addr - then_addr);
    dest->patch_short(else_addr - 2, dest->get_size() - else_addr);

    auto tok = sc->next_token();
    if (!check_delim(tk_rparen, tok)) {
        throw fn_error("compiler", "too many arguments to if", tok.loc);
    }
}

void compiler::compile_import(locals* l) {
    // t_od_o: handle dot form
    auto tok = sc->next_token();
    auto strs = tokenize_name(tok);

    auto x = modules.get(strs);
    u16 mod_id;                  // a constant holding the module i_d
    if (!x.has_value()) {
        // build the module i_d as a value (a cons)
        // t_od_o: use allocator
        auto mod_id_val = V_EMPTY;
        const_id last_id;
        for (int i = strs.size(); i > 0; --i) {
            last_id = dest->cons_const(dest->symbol(strs[i-1]), mod_id_val);
            mod_id_val = dest->get_constant(last_id);
        }
        mod_id = last_id;
    } else {
        mod_id = **x;
    }

    // t_od_o: check the name is legal
    // push module name to the stack
    auto name_id = dest->sym_const(strs[strs.size()-1]);
    dest->write_byte(OP_CONST);
    dest->write_short(name_id);

    // push the module id
    dest->write_byte(OP_CONST);
    dest->write_short(mod_id);
    dest->write_byte(OP_IMPORT);

    // load a new module
    if (!x.has_value()) {
        // switch to the new module
        dest->write_byte(OP_COPY);
        dest->write_byte(0);
        dest->write_byte(OP_MODULE);
        auto prev_mod_id = cur_mod_id;
        cur_mod_id = mod_id;

        // t_od_o: find and compile file contents
        auto src = module_path(strs);
        compile_file(src);

        // switch back
        dest->write_byte(OP_CONST);
        dest->write_short(prev_mod_id);
        dest->write_byte(OP_IMPORT);
        dest->write_byte(OP_MODULE);
        cur_mod_id = prev_mod_id;
    }

    // bind the global variable
    dest->write_byte(OP_SET_GLOBAL);
    ++sp;

    if(!check_delim(tk_rparen, tok=sc->next_token())) {
        throw fn_error("compiler", "too many arguments to import.", tok.loc);
    }
}

void compiler::compile_let(locals* l) {
    auto tok = sc->next_token();
    if (check_delim(tk_rparen, tok)) {
        throw fn_error("compiler", "too few arguments to let.", tok.loc);
    }

    // toplevel
    if (l == nullptr) {
        throw fn_error("compiler",
                      "let cannot occur at the top level.",
                      tok.loc);
    }

    // t_od_o: check for duplicate variable names
    do {
        // t_od_o: islegalname
        if (tok.tk != tk_symbol) {
            throw fn_error("compiler",
                          "illegal argument to let. variable name must be a symbol.",
                          tok.loc);
        }

        auto loc = sp++; // location of the new variable on the stack
        // initially bind the variable to null (early binding enables recursion)
        dest->write_byte(OP_NULL);
        l->vars.insert(*tok.datum.str,loc);

        // compile value expression
        compile_expr(l);
        dest->write_byte(OP_SET_LOCAL);
        dest->write_byte(loc);
        --sp;
    } while (!check_delim(tk_rparen, tok=sc->next_token()));

    // return null
    dest->write_byte(OP_NULL);
    ++sp;
}

void compiler::compile_or(locals* l) {
    forward_list<bc_addr> patch_locs;

    auto tok = sc->next_token();
    if(check_delim(tk_rparen, tok)) {
        // or with no arguments yields a false value
        dest->write_byte(OP_FALSE);
        ++sp;
        return;
    }

    compile_expr(l, &tok);
    // copy the top of the stack because cjump consumes it
    dest->write_byte(OP_COPY);
    dest->write_byte(0);
    dest->write_byte(OP_CJUMP);
    dest->write_short(3);
    dest->write_byte(OP_JUMP);
    dest->write_short(0);
    patch_locs.push_front(dest->get_size());
    while (!check_delim(tk_rparen, (tok=sc->next_token()))) {
        dest->write_byte(OP_POP);
        compile_expr(l, &tok);
        dest->write_byte(OP_COPY);
        dest->write_byte(0);
        dest->write_byte(OP_CJUMP);
        dest->write_short(3);
        dest->write_byte(OP_JUMP);
        dest->write_short(0);
        patch_locs.push_front(dest->get_size());
    }
    dest->write_byte(OP_POP);
    dest->write_byte(OP_FALSE);
    auto end_addr = dest->get_size();

    for (auto u : patch_locs) {
        dest->patch_short(u-2, end_addr - u);
    }
}

// prefix tells if we're using the prefix notation 'sym or paren notation (quote sym)
void compiler::compile_quote(locals* l, bool prefix) {
    auto tok = sc->next_token();

    if(tok.tk != tk_symbol) {
        throw fn_error("compiler", "argument to quote must be a symbol.", tok.loc);
    }

    dest->write_byte(OP_CONST);
    auto id = dest->sym_const(*tok.datum.str);

    // scan for the close paren unless we're using prefix notation
    if (!prefix && !check_delim(tk_rparen, tok=sc->next_token())) {
        throw fn_error("compiler","too many arguments in quote form", tok.loc);
    }    dest->write_short(id);
    ++sp;
}

void compiler::compile_set(locals* l) {
    // tokenize the name
    auto tok = sc->next_token();
    auto name = tokenize_name(tok);

    // note: assume name.size() >= 1
    if (name.size() == 1) {
        // variable set
        u32 levels;
        auto id = find_local(l, name[0], &levels);
        auto sym = dest->sym_const(name[0]);
        if (id.has_value()) {
            // local
            compile_expr(l);
            dest->write_byte(levels > 0 ? OP_SET_UPVALUE : OP_SET_LOCAL);
            dest->write_byte(*id);
            --sp;
        } else {
            // global
            constant(sym);
            ++sp;
            compile_expr(l);
            dest->write_byte(OP_SET_GLOBAL);
            sp -= 2;
        }
        constant(sym);
        ++sp;
    } else {
        // object set
        // compute the object
        compile_var(l, name[0]);
        for (size_t i = 1; i < name.size()-1; ++i) {
            // push the key symbol
            constant(dest->sym_const(name[i]));
            dest->write_byte(OP_OBJ_GET);
        }
        // final symbol
        auto sym = dest->sym_const(name[name.size()-1]);
        constant(sym);

        ++sp; // at this point the stack is ->[key] obj (initial-stack-pointer) ...

        // compile the value expression
        compile_expr(l);
        dest->write_byte(OP_OBJ_SET);
        sp -= 2;

        // return symbol name
        constant(sym);
        ++sp;
    }

    if (!check_delim(tk_rparen, tok=sc->next_token())) {
        throw fn_error("compiler", "too many arguments to set.", tok.loc);
    }
}

// braces expand to (object args ...) forms
void compiler::compile_braces(locals* l) {
    auto old_sp = sp;
    // get the object variable
    compile_var(l, "object");
    // compile the arguments
    auto tok = sc->next_token();
    u32 num_args = 0;
    while (!check_delim(tk_rbrace, tok)) {
        compile_expr(l,&tok);
        ++num_args;
        tok = sc->next_token();
    }
    
    if (num_args > 255) {
        throw fn_error("compiler","too many arguments (more than 255) between braces", tok.loc);
    }

    // do the call
    dest->write_byte(OP_CALL);
    dest->write_byte((u8)num_args);
    sp = old_sp + 1;
}

// brackets expand to (list args ...) forms
void compiler::compile_brackets(locals* l) {
    auto old_sp = sp;
    // get the object variable
    compile_var(l, "list");
    // compile the arguments
    auto tok = sc->next_token();
    u32 num_args = 0;
    while (!check_delim(tk_rbracket, tok)) {
        compile_expr(l,&tok);
        ++num_args;
        tok = sc->next_token();
    }
    
    if (num_args > 255) {
        throw fn_error("compiler","too many arguments (more than 255) between braces", tok.loc);
    }

    // do the call
    dest->write_byte(OP_CALL);
    dest->write_byte((u8)num_args);
    sp = old_sp + 1;
}

// compile function call
void compiler::compile_call(locals* l, token* t0) {
    // first, compile the operator
    token tok = *t0;
    auto old_sp = sp;
    compile_expr(l, t0);

    // now, compile the arguments
    u32 num_args = 0;
    while (!check_delim(tk_rparen, tok=sc->next_token())) {
        ++num_args;
        compile_expr(l, &tok);
    }

    if (num_args > 255) {
        throw fn_error("compiler","too many arguments (more than 255) for function call", tok.loc);
    }

    // finally, compile the call itself
    dest->write_byte(OP_CALL);
    dest->write_byte((u8)num_args);
    sp = old_sp + 1;
}

void compiler::compile_expr(locals* l, token* t0) {
    token tok = t0 == nullptr ? sc->next_token() : *t0;
    token next;
    dest->set_loc(tok.loc);

    u16 id;

    if (is_right_delim(tok)) {
        throw fn_error("compiler", "unexpected closing delimiter '" + tok.to_string() +"'.", tok.loc);
    }

    switch (tok.tk) {
    case tk_eof:
        // just exit
        return;

    // constants
    case tk_number:
        id = dest->num_const(tok.datum.num);
        constant(id);
        sp++;
        break;
    case tk_string:
        id = dest->sym_const(*tok.datum.str);
        constant(id);
        sp++;
        break;

    // symbol dispatch
    case tk_symbol:
        if (*tok.datum.str == "null") {
            dest->write_byte(OP_NULL);
            sp++;
        } else if(*tok.datum.str == "false") {
            dest->write_byte(OP_FALSE);
            sp++;
        } else if(*tok.datum.str == "true") {
            dest->write_byte(OP_TRUE);
            sp++;
        } else {
            compile_var(l, *tok.datum.str);
        }
        break;

    case tk_dot:
        compile_dot_token(l,tok);
        break;

    case tk_lbrace: 
        compile_braces(l);
        break;
    case tk_lbracket:
        compile_brackets(l);
        break;

    case tk_dollar_brace:
    case tk_dollar_bracket:
    case tk_dollar_paren:
    case tk_dollar_backtick:
        throw fn_error("compiler", "unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;

    case tk_quote:
        compile_quote(l, true);
        break;

    case tk_backtick:
        throw fn_error("compiler", "unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;
    case tk_comma:
        throw fn_error("compiler", "unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;
    case tk_comma_at:
        throw fn_error("compiler", "unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;

    // parentheses
    case tk_lparen:
        next = sc->next_token();
        if (next.tk == tk_symbol) {
            string* op = next.datum.str;
            if (*op == "and") {
                compile_and(l);
            } else if (*op == "apply") {
                compile_apply(l);
            } else if (*op == "cond") {
                compile_cond(l);
            } else if (*op == "def") {
                compile_def(l);
            } else if (*op == "dot") {
                compile_dot_expr(l);
            } else if (*op == "do") {
                compile_do(l);
            } else if (*op == "fn") {
                compile_fn(l);
            } else if (*op == "if") {
                compile_if(l);
            } else if (*op == "import") {
                compile_import(l);
            } else if (*op == "let") {
                compile_let(l);
            } else if (*op == "or") {
                compile_or(l);
            } else if (*op == "quote") {
                compile_quote(l, false);
            } else if (*op == "set") {
                compile_set(l);
            } else {
                compile_call(l, &next);
            }
        } else {
            compile_call(l, &next);
        }
        break;

    default:
        // unimplemented
        throw fn_error("compiler", "unexpected token " + tok.to_string(), tok.loc);
        std::cerr << "compiler warning:  expr type\n";
        dest->write_byte(OP_NOP);
        break;
    }

}

void compiler::compile() {
    token tok = sc->next_token();
    while (tok.tk != tk_eof) {
        compile_expr(nullptr, &tok);
        tok = sc->next_token();
        dest->write_byte(OP_POP);
        --sp;
    }
}

void compiler::compile_file(const string& filename) {
    compile_file(fs::path(filename));
}
void compiler::compile_file(const fs::path& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        auto err_str = new string("compiler");
        source_loc loc(err_str, 0, 0);
        // t_od_o: get error from perror
        throw fn_error("compiler", "error opening file '" + filename.string() + "'.", loc);
        delete err_str;
    }

    // basically just need to set the scanner
    auto old_sc = sc;
    sc = new scanner(&in, filename);
    compile();
    delete sc;
    sc = old_sc;
}


}
