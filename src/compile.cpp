#include "compile.hpp"

#include <iostream>
#include <memory>

namespace fn {

using namespace fn_scan;
using namespace fn_bytes;

// compile def expressions
void compileDef(Scanner* sc, Bytecode* dest) {
    Token tok = sc->nextToken();
    if (tok.tk != TKSymbol) {
        // TODO: unimplemented error
    }

    // compile the value expression
    compileExpr(sc, dest);

    // make sure there's a close paren
    Token last = sc->nextToken();
    if (last.tk != TKRParen) {
        std::cerr << "error: unmatched left paren\n";
        // TODO: error
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

void compileExpr(Scanner* sc, Bytecode* dest, Token* t0) {
    Token tok = t0 == nullptr ? sc->nextToken() : *t0;
    Token next;
    dest->setLoc(tok.loc);

    u16 id;
    Value v;
    switch (tok.tk) {
    case TKEOF:
        // just exit
        return;

    // constants
    case TKNumber:
        id = dest->addConstant(makeNumValue(tok.datum.num));
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
        break;
    case TKString:
        v = makeStringValue(tok.datum.str);
        id = dest->addConstant(v);
        dest->writeByte(OP_CONST);
        dest->writeShort(id);
        break;

    // symbol dispatch
    case TKSymbol:
        if (*tok.datum.str == "null") {
            dest->writeByte(OP_NULL);
        } else if(*tok.datum.str == "false") {
            dest->writeByte(OP_FALSE);
        } else if(*tok.datum.str == "true") {
            dest->writeByte(OP_TRUE);
        } else {
            id = dest->addConstant(makeStringValue(tok.datum.str));
            dest->writeByte(OP_CONST);
            dest->writeShort(id);
            dest->writeByte(OP_GET_GLOBAL);
            
            // TODO: local variable lookup
        }
        break;

    // parentheses
    case TKLParen:
        next = sc->nextToken();
        if (next.tk == TKSymbol) {
            string *op = next.datum.str;
            if (*op == "def") {
                compileDef(sc,dest);
            }
        } else {
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

void compile(Scanner* sc, Bytecode* dest) {
    Token tok = sc->nextToken();
    while (tok.tk != TKEOF) {
        compileExpr(sc, dest, &tok);
        tok = sc->nextToken();
        dest->writeByte(OP_POP);
    }
}


}
