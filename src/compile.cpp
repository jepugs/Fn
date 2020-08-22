#include "compile.hpp"

#include <iostream>
#include <memory>

namespace fn {

using namespace fn_scan;
using namespace fn_bytes;

Locals::Locals(Locals* parent, FuncStub* func) : vars(), parent(parent), curFunc(func) { }

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

u8* Locals::search(string name) {
    auto pos = vars.get(name);
    if (pos == nullptr) {
        if (parent == nullptr) {
            return nullptr;
        } else {
            return parent->search(name);
        }
    } else {
        return pos;
    }
}

Compiler::Compiler(Bytecode* dest, Scanner* sc) : dest(dest), sc(sc), sp(0) { }

Compiler::~Compiler() {
    // TODO: free locals at least or something
}

static inline bool isRightDelim(Token tok) {
    auto tk = tok.tk;
    return tk == TKRBrace || tk == TKRBracket || tk == TKRParen;
}

// returns true when
bool checkDelim(TokenKind expected, Token tok) {
    if (tok.tk == expected) {
        return true;
    } else if (isRightDelim(tok)) {
        throw FNError("parser", "Mismatched closing delimiter " + tok.to_string(), tok.loc);
    } else if (tok.tk == TKEOF) {
        throw FNError("parser", "Encountered EOF while scanning", tok.loc);
    }
    return false;
}

void Compiler::compileVar(Locals* locals, const string& name) {
    if (locals == nullptr) {
        // compile global
        auto id = dest->addConstant(makeStringValue(&name));
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
        dest->writeByte(OP_GLOBAL);
        sp++;
        return;
    }

    auto env = locals;
    u8* res;
    // keep track of how many enclosing functions we need to go into
    u32 levels = 0;
    do {
        res = env->vars.get(name);
        if (res != nullptr) {
            break;
        }

        // here we're about to ascend to an enclosing function, so we need an upvalue
        if (env->curFunc != nullptr) {
            ++levels;
        }
    } while ((env = env->parent) != nullptr);

    // TODO: set upvalues
    if (levels > 0 && res != nullptr) {
        u8 id = locals->addUpvalue(levels, *res);
        dest->writeByte(OP_UPVALUE);
        dest->writeByte(id);
    } else if (res != nullptr) {
        dest->writeByte(OP_LOCAL);
        dest->writeByte(*res);
    } else {
        // compile global
        auto id = dest->addConstant(makeStringValue(&name));
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
        dest->writeByte(OP_GLOBAL);
        sp++;
    }
}

// compile def expressions
void Compiler::compileDef(Locals* locals) {
    Token tok = sc->nextToken();
    if (tok.tk != TKSymbol) {
        throw FNError("parser", "First argument to def must be a symbol.", tok.loc);
        // TODO: unimplemented error
    }
    // TODO: check for legal symbols

    // compile the value expression
    compileExpr(locals);

    // make sure there's a close paren
    Token last = sc->nextToken();
    if (!checkDelim(TKRParen, last)) {
        throw FNError("parser", "Too many arguments to def", last.loc);
    }

    // copy the value expression
    dest->writeByte(OP_COPY);
    dest->writeByte(0);

    // put the name string
    Value v = makeStringValue(tok.datum.str);
    u16 id = dest->addConstant(v);
    dest->writeByte(OP_CONST);
    dest->writeShort(id);

    // make the global
    dest->writeByte(OP_SET_GLOBAL);
}

void Compiler::compileFn(Locals* locals) {
    // first, read all arguments and set up locals
    Token tok = sc->nextToken();
    if (tok.tk != TKLParen) {
        throw FNError("parser", "Second argument of fn must be an argument list.", tok.loc);
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

    // TODO: add new function object
    // TODO: check args < 256
    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        if (tok.tk != TKSymbol) {
            throw FNError("parser", "Argument names must be symbols.", tok.loc);
        }
        // TODO: check for repeated names
        enclosed->vars.insert(*tok.datum.str, sp);
        ++sp;
    }
    auto funcId = dest->addFunction(sp);
    enclosed->curFunc = dest->getFunction(funcId);

    // compile the function body
    tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        throw FNError("parser", "Empty fn body.", tok.loc);
    }
    compileExpr(enclosed, &tok);
    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        dest->writeByte(OP_POP);
        compileExpr(enclosed, &tok);
    }
    dest->writeByte(OP_RETURN);

    // FIXME: since jump takes a signed offset, need to ensure that offset is positive if converted
    // to a signed number. Otherwise emit an error.
    auto offset = dest->getSize() - patchAddr - 2;
    dest->patchShort(patchAddr, offset);

    dest->writeByte(OP_CLOSURE);
    dest->writeShort(funcId);
    sp = oldSp + 1;
}

void Compiler::compileLet(Locals* locals) {
    auto tok = sc->nextToken();
    if (tok.tk != TKLParen) {
        cerr << "error: let expects left paren, got something else.\n";
        // TODO: throw exception
    }

    Locals* prev = locals;
    auto oldSp = sp;
    u8 numLocals = 0;
    // save a space for the result. null is a fine placeholder
    dest->writeByte(OP_NULL);
    ++sp;
    // create new locals
    locals = new Locals(prev);

    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        if (tok.tk != TKSymbol) {
            throw FNError("parser", "let variable name not a symbol", tok.loc);
        }

        // TODO: check for repeated names
        locals->vars.insert(*tok.datum.str, sp);
        compileExpr(locals);
        ++sp;
        ++numLocals;
    }

    // now compile the body
    tok = sc->nextToken();
    if (checkDelim(TKRParen, tok)) {
        throw FNError("parser", "empty let body", tok.loc);
    }
    compileExpr(locals, &tok);

    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        dest->writeByte(OP_POP);
        compileExpr(locals, &tok);
    }

    // save the result. This will overwrite the earlier null value
    dest->writeByte(OP_SET_LOCAL);
    dest->writeByte(oldSp);
    // pop the variables
    dest->writeByte(OP_CLOSE);
    dest->writeByte(numLocals);

    // free up the new environment
    delete locals;
    // restore the stack pointer
    sp = oldSp+1;
}

// compile function call
void Compiler::compileCall(Locals* locals, Token* t0) {
    // first, compile the operator
    Token tok = *t0;
    compileExpr(locals, t0);
    auto oldSp = sp;
    ++sp;

    // now, compile the arguments
    u32 numArgs = 0;
    while (!checkDelim(TKRParen, tok=sc->nextToken())) {
        ++numArgs;
        compileExpr(locals, &tok);
        ++sp;
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
        throw FNError("parser", "Unexpected closing delimiter", tok.loc);
    }

    switch (tok.tk) {
    case TKEOF:
        // just exit
        return;

    // constants
    case TKNumber:
        id = dest->addConstant(makeNumValue(tok.datum.num));
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
        sp++;
        break;
    case TKString:
        v = makeStringValue(tok.datum.str);
        id = dest->addConstant(v);
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
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
            // auto local = locals == nullptr ? nullptr : locals->search(*tok.datum.str);
            // if (local == nullptr) {
            //     // perform global lookup
            //     id = dest->addConstant(makeStringValue(tok.datum.str));
            //     dest->writeByte(OP_CONST);
            //     dest->writeShort(id);
            //     dest->writeByte(OP_GET_GLOBAL);
            //     sp++;
            // } else {
            //     // local variable
            //     dest->writeByte(OP_LOCAL);
            //     dest->writeByte(*local);
            //     sp++;
            // }
        }
        break;

    // parentheses
    case TKLParen:
        next = sc->nextToken();
        if (next.tk == TKSymbol) {
            string *op = next.datum.str;
            if (*op == "def") {
                compileDef(locals);
            } else if (*op == "fn") {
                compileFn(locals);
            } else if (*op == "let") {
                compileLet(locals);
            } else {
                compileCall(locals, &next);
            }
        } else {
            compileCall(locals, &next);
            // unimplemented
        }
        break;

    default:
        // unimplemented
        std::cerr << "compiler warning: unimplemented expr type\n";
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
