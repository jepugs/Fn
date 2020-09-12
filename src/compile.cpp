#include "compile.hpp"

#include <memory>

namespace fn {

using namespace fn_scan;
using namespace fn_bytes;

Locals::Locals(Locals* parent, FuncStub* func) : vars(), parent(parent), curFunc(func) { }

// FIXME: this is probably a dumb algorithm
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
u8 Locals::addUpvalue(u32 levels, u8 pos) {
    // get to the next call frame
    // add an upvalue to curFun
    // climb up until srcEnv is reached, adding upvalues to each function along the way

    // find the most recent call frame
    auto call = this;
    while (call->curFunc == nullptr && call != nullptr) {
        call = call->parent;
    }

    // if (call == nullptr) {
    //     //        throw FNError("compiler", "tried to add upvalue with insufficient functions",
    //     //                      );
    // }

    // levels == 1 => this is a direct upvalue, so add it and return
    if (levels == 1) {
        return call->curFunc->getUpvalue(pos, true);
    }

    // levels > 1 => need to get the upvalue from an enclosing function
    u8 slot = call->parent->addUpvalue(levels - 1, pos);
    return call->curFunc->getUpvalue(slot, false);
}

static inline bool isLegalName(const string& str) {
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

Compiler::Compiler(const fs::path& dir, Bytecode* dest, Scanner* sc)
    : dest(dest), sc(sc), sp(0), dir(dir), modules() {
    // The first module is fn.core
    // TODO: use allocator
    auto modIdVal = new Cons { dest->symbol("core"), V_EMPTY };
    modIdVal = new Cons { dest->symbol("fn"), value(modIdVal) };
    curModId = dest->addConstant(value(modIdVal));
}

Compiler::~Compiler() {
    // TODO: free locals at least or something
}

static inline bool isRightDelim(Token tok) {
    auto tk = tok.tk;
    return tk == TKRBrace || tk == TKRBracket || tk == TKRParen;
}

// returns true when
static bool checkDelim(TokenKind expected, Token tok) {
    if (tok.tk == expected) {
        return true;
    } else if (isRightDelim(tok)) {
        throw FNError("compiler", "Mismatched closing delimiter " + tok.to_string(), tok.loc);
    } else if (tok.tk == TKEOF) {
        throw FNError("compiler", "Encountered EOF while scanning", tok.loc);
    }
    return false;
}

optional<Local> Compiler::findLocal(Locals* locals, const string& name, u32* levels) {
    if (locals == nullptr) {
        return std::nullopt;
    }

    auto env = locals;
    optional<u8*> res;
    *levels = 0;
    // keep track of how many enclosing functions we need to go into
    do {
        res = env->vars.get(name);
        if (res.has_value()) {
            break;
        }

        // here we're about to ascend to an enclosing function, so we need an upvalue
        if (env->curFunc != nullptr) {
            *levels += 1;
        }
    } while ((env = env->parent) != nullptr);

    if (*levels > 0 && res.has_value()) {
        return locals->addUpvalue(*levels, **res);
    } else if (res.has_value()) {
        return **res;
    }

    return { };
}

void Compiler::compileVar(Locals* locals, const string& name) {
    u32 levels;
    auto id = findLocal(locals, name, &levels);
    if (!id.has_value()) {
        // global
        auto id = dest->addConstant(value(*&name));
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
        dest->writeByte(OP_GLOBAL);
    } else {
        dest->writeByte(levels > 0 ? OP_UPVALUE : OP_LOCAL);
        dest->writeByte(*id);
    }
    ++sp;
}

// helper function that converts the string from a TKDot token to a vector consisting of the names
// of its parts.
static inline vector<string> tokenizeDotString(const string& s) {
    vector<string> res;
    u32 start = 0;
    u32 dotPos = 0;
    bool escaped = false;

    while (dotPos < s.size()) {
        // find the next unescaped dot
        while (dotPos < s.size()) {
            ++dotPos;
            if (escaped) {
                escaped = false;
            } else if (s[dotPos] == '\\') {
                escaped = true;
            } else if (s[dotPos] == '.') {
                break;
            }
        }
        res.push_back(s.substr(start, dotPos-start));
        start = dotPos+1;
        ++dotPos;
    }
    return res;
}

vector<string> Compiler::tokenizeName(optional<Token> t0) {
    Token tok;
    if (t0.has_value()) {
        tok = *t0;
    } else {
        tok = sc->nextToken();
    }

    if (tok.tk == TKSymbol) {
        vector<string> v;
        v.push_back(*tok.datum.str);
        return v;
    }
    if (tok.tk == TKDot) {
        return tokenizeDotString(*tok.datum.str);
    }

    if (tok.tk != TKLParen) {
        // not a symbol or a dot form
        throw FNError("compiler",
                      "Name is not a symbol or a dot form: " + tok.to_string(),
                      tok.loc);
    }

    tok = sc->nextToken();
    if (tok.tk != TKSymbol || *tok.datum.str != "dot") {
        throw FNError("compiler",
                      "Name is not a symbol or a dot form",
                      tok.loc);
    }

    vector<string> res;
    tok = sc->nextToken();
    while (!checkDelim(TKRParen, tok)) {
        if (tok.tk != TKSymbol) {
            throw FNError("compiler", "Arguments to dot must be symbols.", tok.loc);
        }
        res.push_back(*tok.datum.str);
        tok = sc->nextToken();
    }
    return res;
}


void Compiler::compileBlock(Locals* locals) {
    auto tok = sc->nextToken();
    if(checkDelim(TKRParen, tok)) {
        // empty body yields a null value
        dest->writeByte(OP_NULL);
        ++sp;
        return;
    }

    // create a new environment
    auto newEnv = new Locals(locals);

    compileExpr(newEnv, &tok);
    while (!checkDelim(TKRParen, tok = sc->nextToken())) {
        dest->writeByte(OP_POP);
        compileExpr(newEnv, &tok);
    }
    ++sp;

    // don't need this any more :)
    delete newEnv;
}

void Compiler::compileAnd(Locals* locals) {
    forward_list<Addr> patchLocs;

    auto tok = sc->nextToken();
    if(checkDelim(TKRParen, tok)) {
        // and with no arguments yields a true value
        dest->writeByte(OP_TRUE);
        ++sp;
        return;
    }

    compileExpr(locals, &tok);
    // copy the top of the stack because cjump consumes it
    dest->writeByte(OP_COPY);
    dest->writeByte(0);
    dest->writeByte(OP_CJUMP);
    dest->writeShort(0);
    patchLocs.push_front(dest->getSize());
    while (!checkDelim(TKRParen, (tok=sc->nextToken()))) {
        dest->writeByte(OP_POP);
        compileExpr(locals, &tok);
        dest->writeByte(OP_COPY);
        dest->writeByte(0);
        dest->writeByte(OP_CJUMP);
        dest->writeShort(0);
        patchLocs.push_front(dest->getSize());
    }
    dest->writeByte(OP_JUMP);
    dest->writeShort(2);
    auto endAddr = dest->getSize();
    dest->writeByte(OP_POP);
    dest->writeByte(OP_FALSE);

    for (auto u : patchLocs) {
        dest->patchShort(u-2, endAddr - u);
    }
}

void Compiler::compileApply(Locals* locals) {
    auto oldSp = sp;

    auto tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        throw FNError("compiler", "Too few arguments to apply.", tok.loc);
    }
    compileExpr(locals, &tok);

    tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        throw FNError("compiler", "Too few arguments to apply.", tok.loc);
    }
    u32 numArgs = 0;
    do {
        ++numArgs;
        compileExpr(locals, &tok);
    } while (!checkDelim(TKRParen,tok=sc->nextToken()));
    if (numArgs > 255) {
        throw FNError("compiler", "Too many arguments to apply.", tok.loc);
    }
    dest->writeByte(OP_APPLY);
    dest->writeByte(numArgs);

    sp = oldSp+1;
}

void Compiler::compileCond(Locals* locals) {
    auto tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        dest->writeByte(OP_NULL);
        ++sp;
        return;
    }
    // locations where we need to patch the end address
    forward_list<Addr> patchLocs;
    while (!checkDelim(TKRParen, tok)) {
        compileExpr(locals,&tok);
        --sp;
        dest->writeByte(OP_CJUMP);
        dest->writeShort(0);
        auto patchAddr = dest->getSize();
        compileExpr(locals);
        --sp;
        dest->writeByte(OP_JUMP);
        dest->writeShort(0);
        patchLocs.push_front(dest->getSize());

        // patch in the else jump address
        dest->patchShort(patchAddr-2,dest->getSize() - patchAddr);
        tok = sc->nextToken();
    }

    // return null when no tests success
    dest->writeByte(OP_NULL);
    ++sp;
    // patch in the end address for non-null results
    for (auto a : patchLocs) {
        dest->patchShort(a-2, dest->getSize() - a);
    }
}

// compile def expressions
void Compiler::compileDef(Locals* locals) {
    Token tok = sc->nextToken();
    if (tok.tk != TKSymbol) {
        throw FNError("compiler", "First argument to def must be a symbol.", tok.loc);
    }
    // TODO: check for legal symbols
    if(!isLegalName(*tok.datum.str)) {
        throw FNError("compiler", "Illegal variable name " + *tok.datum.str, tok.loc);
    }

    // write the name symbol
    constant(dest->addConstant(dest->symbol(*tok.datum.str)));
    ++sp;
    // compile the value expression
    compileExpr(locals);
    // set the global. This leaves the symbol on the stack
    dest->writeByte(OP_SET_GLOBAL);
    --sp;

    // make sure there's a close paren
    Token last = sc->nextToken();
    if (!checkDelim(TKRParen, last)) {
        throw FNError("compiler", "Too many arguments to def", last.loc);
    }

}

void Compiler::compileDo(Locals* locals) {
    compileBlock(locals);
}

void Compiler::compileDotToken(Locals* locals, Token& tok) {
    auto toks = tokenizeDotString(*tok.datum.str);
    compileVar(locals,toks[0]);
    // note: this compileVar call already sets sp to what we want it at the end
    for (u32 i = 1; i < toks.size(); ++i) {
        dest->writeByte(OP_CONST);
        dest->writeShort(dest->addConstant(dest->symbol(toks[i])));
        dest->writeByte(OP_OBJ_GET);
    }
}

void Compiler::compileDotExpr(Locals* locals) {
    vector<string> toks;

    auto tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        throw FNError("compiler", "Too few arguments to dot.", tok.loc);
    }
    while (!checkDelim(TKRParen, tok)) {
        if (tok.tk != TKSymbol) {
            throw FNError("compiler", "Arguments to dot must be symbols.", tok.loc);
        }
        toks.push_back(*tok.datum.str);
        tok = sc->nextToken();
    }
    compileVar(locals,toks[0]);
    // note: this compileVar call already sets sp to what we want it at the end
    for (u32 i = 1; i < toks.size(); ++i) {
        constant(dest->addConstant(dest->symbol(toks[i])));
        dest->writeByte(OP_OBJ_GET);
    }
}

void Compiler::compileFn(Locals* locals) {
    // first, read all arguments and set up locals
    Token tok = sc->nextToken();
    if (tok.tk != TKLParen) {
        throw FNError("compiler", "Second argument of fn must be an argument list.", tok.loc);
    }

    // start out by jumping to the end of the function body. We will patch in the distance to jump
    // later on.
    dest->writeByte(OP_JUMP);
    auto patchAddr = dest->getSize();
    // write the placholder offset
    dest->writeShort(0);

    auto enclosed = new Locals(locals);
    auto oldSp = sp;
    sp = 0;

    bool vararg = false;
    // TODO: add new function object
    // TODO: check args < 256
    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        if (tok.tk != TKSymbol) {
            throw FNError("compiler", "Argument names must be symbols.", tok.loc);
        }
        // & indicates a variadic argument
        if (*tok.datum.str == "&") {
            vararg = true;
            break;
        } else if (!isLegalName(*tok.datum.str)) {
            throw FNError("compiler", "Illegal variable name " + *tok.datum.str, tok.loc);
        }

        // TODO: check for repeated names
        enclosed->vars.insert(*tok.datum.str, sp);
        ++sp;
    }

    if (vararg) {
        // check that we have a symbol for the variable name
        tok = sc->nextToken();
        if (tok.tk != TKSymbol) {
            throw FNError("compiler", "Argument names must be symbols.", tok.loc);
        }
        enclosed->vars.insert(*tok.datum.str, sp);
        ++sp;

        tok = sc->nextToken();
        if (!checkDelim(TKRParen, tok)) {
            throw FNError("compiler",
                          "Trailing tokens after variadic parameter in fn argument list.",
                          tok.loc);
        }
    }

    auto funcId = dest->addFunction(sp, vararg);
    enclosed->curFunc = dest->getFunction(funcId);

    // compile the function body
    compileBlock(enclosed);
    dest->writeByte(OP_RETURN);

    // FIXME: since jump takes a signed offset, need to ensure that offset is positive if converted
    // to a signed number. Otherwise emit an error.
    auto offset = dest->getSize() - patchAddr - 2;
    dest->patchShort(patchAddr, offset);

    dest->writeByte(OP_CLOSURE);
    dest->writeShort(funcId);
    sp = oldSp + 1;
}

void Compiler::compileIf(Locals* locals) {
    // compile the test
    compileExpr(locals);
    dest->writeByte(OP_CJUMP);
    --sp;
    // this will hold the else address
    dest->writeShort(0);

    // then clause
    auto thenAddr = dest->getSize();
    compileExpr(locals);
    --sp;
    // jump to the end of the if
    dest->writeByte(OP_JUMP);
    dest->writeShort(0);

    // else clause
    auto elseAddr = dest->getSize();
    compileExpr(locals);
    // sp is now where we want it

    dest->patchShort(thenAddr - 2, elseAddr - thenAddr);
    dest->patchShort(elseAddr - 2, dest->getSize() - elseAddr);

    auto tok = sc->nextToken();
    if (!checkDelim(TKRParen, tok)) {
        throw FNError("compiler", "Too many arguments to if", tok.loc);
    }
}

void Compiler::compileImport(Locals* locals) {
    // TODO: handle dot form
    auto tok = sc->nextToken();
    auto strs = tokenizeName(tok);

    auto x = modules.get(strs);
    u16 modId;                  // a constant holding the module ID
    if (!x.has_value()) {
        // build the module ID as a value (a cons)
        // TODO: use allocator
        auto modIdVal = V_EMPTY;
        for (int i = strs.size(); i > 0; --i) {
            modIdVal = value(new Cons(dest->symbol(strs[i-1]), modIdVal));
        }
        modId = dest->addConstant(modIdVal);
    } else {
        modId = **x;
    }

    // TODO: check the name is legal
    // push module name to the stack
    auto nameId = dest->addConstant(dest->symbol(strs[strs.size()-1]));
    dest->writeByte(OP_CONST);
    dest->writeShort(nameId);

    // push the module id
    dest->writeByte(OP_CONST);
    dest->writeShort(modId);
    dest->writeByte(OP_IMPORT);

    // load a new module
    if (!x.has_value()) {
        // switch to the new module
        dest->writeByte(OP_COPY);
        dest->writeByte(0);
        dest->writeByte(OP_MODULE);
        auto prevModId = curModId;
        curModId = modId;

        // TODO: find and compile file contents

        // switch back
        dest->writeByte(OP_CONST);
        dest->writeShort(prevModId);
        dest->writeByte(OP_IMPORT);
        dest->writeByte(OP_MODULE);
        curModId = prevModId;
    }

    // bind the global variable
    dest->writeByte(OP_SET_GLOBAL);
    ++sp;

    if(!checkDelim(TKRParen, tok=sc->nextToken())) {
        throw FNError("compiler", "Too many arguments to import.", tok.loc);
    }
}

void Compiler::compileLet(Locals* locals) {
    auto tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        throw FNError("compiler", "Too few arguments to let.", tok.loc);
    }

    // toplevel
    if (locals == nullptr) {
        throw FNError("compiler",
                      "Let cannot occur at the top level.",
                      tok.loc);
    }

    // TODO: check for duplicate variable names
    do {
        // TODO: islegalname
        if (tok.tk != TKSymbol) {
            throw FNError("compiler",
                          "Illegal argument to let. Variable name must be a symbol.",
                          tok.loc);
        }

        auto loc = sp++; // location of the new variable on the stack
        // initially bind the variable to null (early binding enables recursion)
        dest->writeByte(OP_NULL);
        locals->vars.insert(*tok.datum.str,loc);

        // compile value expression
        compileExpr(locals);
        dest->writeByte(OP_SET_LOCAL);
        dest->writeByte(loc);
        --sp;
    } while (!checkDelim(TKRParen, tok=sc->nextToken()));

    // return null
    dest->writeByte(OP_NULL);
    ++sp;
}

void Compiler::compileOr(Locals* locals) {
    forward_list<Addr> patchLocs;

    auto tok = sc->nextToken();
    if(checkDelim(TKRParen, tok)) {
        // or with no arguments yields a false value
        dest->writeByte(OP_FALSE);
        ++sp;
        return;
    }

    compileExpr(locals, &tok);
    // copy the top of the stack because cjump consumes it
    dest->writeByte(OP_COPY);
    dest->writeByte(0);
    dest->writeByte(OP_CJUMP);
    dest->writeShort(3);
    dest->writeByte(OP_JUMP);
    dest->writeShort(0);
    patchLocs.push_front(dest->getSize());
    while (!checkDelim(TKRParen, (tok=sc->nextToken()))) {
        dest->writeByte(OP_POP);
        compileExpr(locals, &tok);
        dest->writeByte(OP_COPY);
        dest->writeByte(0);
        dest->writeByte(OP_CJUMP);
        dest->writeShort(3);
        dest->writeByte(OP_JUMP);
        dest->writeShort(0);
        patchLocs.push_front(dest->getSize());
    }
    dest->writeByte(OP_POP);
    dest->writeByte(OP_FALSE);
    auto endAddr = dest->getSize();

    for (auto u : patchLocs) {
        dest->patchShort(u-2, endAddr - u);
    }
}

// prefix tells if we're using the prefix notation 'sym or paren notation (quote sym)
void Compiler::compileQuote(Locals* locals, bool prefix) {
    auto tok = sc->nextToken();

    if(tok.tk != TKSymbol) {
        throw FNError("compiler", "Argument to quote must be a symbol.", tok.loc);
    }

    dest->writeByte(OP_CONST);
    auto id = dest->addConstant(dest->symbol(*tok.datum.str));

    // scan for the close paren unless we're using prefix notation
    if (!prefix && !checkDelim(TKRParen, tok=sc->nextToken())) {
        throw FNError("compiler","Too many arguments in quote form", tok.loc);
    }    dest->writeShort(id);
    ++sp;
}

void Compiler::compileSet(Locals* locals) {
    // tokenize the name
    auto tok = sc->nextToken();
    auto name = tokenizeName(tok);

    // note: assume name.size() >= 1
    if (name.size() == 1) {
        // variable set
        u32 levels;
        auto id = findLocal(locals, name[0], &levels);
        auto sym = dest->addConstant(value(name[0]));
        if (id.has_value()) {
            // local
            compileExpr(locals);
            dest->writeByte(levels > 0 ? OP_SET_UPVALUE : OP_SET_LOCAL);
            dest->writeByte(*id);
            // put the constant
            constant(sym);
        } else {
            // global
            constant(sym);
            compileExpr(locals);
            dest->writeByte(OP_SET_GLOBAL);
        }
        ++sp;
    } else {
        // object set
        // compute the object
        compileVar(locals, name[0]);
        for (size_t i = 1; i < name.size()-1; ++i) {
            // push the key symbol
            constant(dest->addConstant(dest->symbol(name[i])));
            dest->writeByte(OP_OBJ_GET);
        }
        // final symbol
        auto sym = dest->addConstant(dest->symbol(name[name.size()-1]));
        constant(sym);

        sp += 2; // at this point the stack is ->[key] obj (initial-stack-pointer) ...

        // compile the value expression
        compileExpr(locals);
        dest->writeByte(OP_OBJ_SET);

        // return symbol name
        constant(sym);
        --sp;
    }

    if (!checkDelim(TKRParen, tok=sc->nextToken())) {
        throw FNError("compiler", "Too many arguments to set.", tok.loc);
    }
}

// braces expand to (Object args ...) forms
void Compiler::compileBraces(Locals* locals) {
    auto oldSp = sp;
    // get the Object variable
    compileVar(locals, "Object");
    // compile the arguments
    auto tok = sc->nextToken();
    u32 numArgs = 0;
    while (!checkDelim(TKRBrace, tok)) {
        compileExpr(locals,&tok);
        ++numArgs;
        tok = sc->nextToken();
    }
    
    if (numArgs > 255) {
        throw FNError("compiler","Too many arguments (more than 255) between braces", tok.loc);
    }

    // do the call
    dest->writeByte(OP_CALL);
    dest->writeByte((u8)numArgs);
    sp = oldSp + 1;
}

// brackets expand to (List args ...) forms
void Compiler::compileBrackets(Locals* locals) {
    auto oldSp = sp;
    // get the Object variable
    compileVar(locals, "List");
    // compile the arguments
    auto tok = sc->nextToken();
    u32 numArgs = 0;
    while (!checkDelim(TKRBracket, tok)) {
        compileExpr(locals,&tok);
        ++numArgs;
        tok = sc->nextToken();
    }
    
    if (numArgs > 255) {
        throw FNError("compiler","Too many arguments (more than 255) between braces", tok.loc);
    }

    // do the call
    dest->writeByte(OP_CALL);
    dest->writeByte((u8)numArgs);
    sp = oldSp + 1;
}

// compile function call
void Compiler::compileCall(Locals* locals, Token* t0) {
    // first, compile the operator
    Token tok = *t0;
    auto oldSp = sp;
    compileExpr(locals, t0);

    // now, compile the arguments
    u32 numArgs = 0;
    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        ++numArgs;
        compileExpr(locals, &tok);
    }

    if (numArgs > 255) {
        throw FNError("compiler","Too many arguments (more than 255) for function call", tok.loc);
    }

    // finally, compile the call itself
    dest->writeByte(OP_CALL);
    dest->writeByte((u8)numArgs);
    sp = oldSp + 1;
}

void Compiler::compileExpr(Locals* locals, Token* t0) {
    Token tok = t0 == nullptr ? sc->nextToken() : *t0;
    Token next;
    dest->setLoc(tok.loc);

    u16 id;
    Value v;

    if (isRightDelim(tok)) {
        throw FNError("compiler", "Unexpected closing delimiter '" + tok.to_string() +"'.", tok.loc);
    }

    switch (tok.tk) {
    case TKEOF:
        // just exit
        return;

    // constants
    case TKNumber:
        id = dest->addConstant(value(tok.datum.num));
        constant(id);
        sp++;
        break;
    case TKString:
        v = value(*tok.datum.str);
        id = dest->addConstant(v);
        constant(id);
        sp++;
        break;

    // symbol dispatch
    case TKSymbol:
        if (*tok.datum.str == "null") {
            dest->writeByte(OP_NULL);
            sp++;
        } else if(*tok.datum.str == "false") {
            dest->writeByte(OP_FALSE);
            sp++;
        } else if(*tok.datum.str == "true") {
            dest->writeByte(OP_TRUE);
            sp++;
        } else {
            compileVar(locals, *tok.datum.str);
        }
        break;

    case TKDot:
        compileDotToken(locals,tok);
        break;

    case TKLBrace: 
        compileBraces(locals);
        break;
    case TKLBracket:
        compileBrackets(locals);
        break;

    case TKDollarBrace:
    case TKDollarBracket:
    case TKDollarParen:
    case TKDollarBacktick:
        throw FNError("compiler", "Unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;

    case TKQuote:
        compileQuote(locals, true);
        break;

    case TKBacktick:
        throw FNError("compiler", "Unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;
    case TKComma:
        throw FNError("compiler", "Unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;
    case TKCommaSplice:
        throw FNError("compiler", "Unimplemented syntax: '" + tok.to_string() + "'.", tok.loc);
        break;

    // parentheses
    case TKLParen:
        next = sc->nextToken();
        if (next.tk == TKSymbol) {
            string* op = next.datum.str;
            if (*op == "and") {
                compileAnd(locals);
            } else if (*op == "apply") {
                compileApply(locals);
            } else if (*op == "cond") {
                compileCond(locals);
            } else if (*op == "def") {
                compileDef(locals);
            } else if (*op == "dot") {
                compileDotExpr(locals);
            } else if (*op == "do") {
                compileDo(locals);
            } else if (*op == "fn") {
                compileFn(locals);
            } else if (*op == "if") {
                compileIf(locals);
            } else if (*op == "import") {
                compileImport(locals);
            } else if (*op == "let") {
                compileLet(locals);
            } else if (*op == "or") {
                compileOr(locals);
            } else if (*op == "quote") {
                compileQuote(locals, false);
            } else if (*op == "set") {
                compileSet(locals);
            } else {
                compileCall(locals, &next);
            }
        } else {
            compileCall(locals, &next);
        }
        break;

    default:
        // unimplemented
        throw FNError("compiler", "Unexpected token " + tok.to_string(), tok.loc);
        std::cerr << "compiler warning:  expr type\n";
        dest->writeByte(OP_NOP);
        break;
    }

}

void Compiler::compile() {
    Token tok = sc->nextToken();
    while (tok.tk != TKEOF) {
        compileExpr(nullptr, &tok);
        tok = sc->nextToken();
        dest->writeByte(OP_POP);
        --sp;
    }
}


}
