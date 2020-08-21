#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include "bytes.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

namespace fn {

using namespace fn_scan;

// Locals object tracks all state
struct Locals {
    // table of local variable locations
    Table<u8> vars;
    // parent environment
    Locals* parent;

    // the function we're currently compiling. This is needed to keep track of upvalues
    FuncStub* curFunc;

    Locals(Locals* parent=nullptr, FuncStub* curFunc=nullptr);
    // add an upvalue which has the specified number of levels of indirection (each level corresponds
    // to one more enclosing function before)
    u8 addUpvalue(u32 levels, u8 pos);
    // returns nullptr on failure
    u8* search(string name);
};

class Compiler {
private:
    Bytecode* dest;
    Scanner* sc;
    // compiler's internally-tracked stack pointer
    u8 sp;

    // compile a single expression, consuming tokens. t0 is an optional first token. Running will leave
    // the expression on top of the stack.
    void compileExpr(Locals* locals, Token* t0=nullptr);

    void compileCall(Locals* locals, Token* t0);
    void compileDef(Locals* locals);
    void compileDo(Locals* locals);
    void compileFn(Locals* locals);
    void compileLet(Locals* locals);

    void compileVar(Locals* locals, const string& name);

public:
    Compiler(Bytecode* dest, Scanner* sc=nullptr);
    ~Compiler();
    // compile all scanner input until EOF. Running the generated code should leave the interpreter
    // stack empty, with lastPop() returning the result of the final toplevel expression
    void compile();

    // set a new scanner
    void setScanner(Scanner* sc);
};

}

#endif
