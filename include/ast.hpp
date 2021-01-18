#ifndef __FN_AST_HPP
#define __FN_AST_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {

enum AstKind {
    AKAtom,
    AKList
};

enum AtomType {
    ATNumber,
    ATString,
    ATSymbol
};

struct AstAtom  {
    AtomType type;
    union {
        f64 num;
        FnString* str;
        Symbol* sym;
    } datum;
};

struct Ast {
    SourceLoc loc;
    AstKind k;
    union {
        AstAtom* atom;
        forward_list<Ast>* list;
    } datum;
};

}

#endif
