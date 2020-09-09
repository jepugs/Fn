#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include <filesystem>

#include "base.hpp"
#include "bytes.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

namespace fn {

using namespace fn_scan;

namespace fs = std::filesystem;

// Locals object tracks all state
struct Locals {
    // table of local variable locations
    Table<string,u8> vars;
    // parent environment
    Locals* parent;

    // the function we're currently compiling. This is needed to keep track of upvalues
    FuncStub* curFunc;

    Locals(Locals* parent=nullptr, FuncStub* curFunc=nullptr);
    // add an upvalue which has the specified number of levels of indirection (each level corresponds
    // to one more enclosing function before)
    u8 addUpvalue(u32 levels, u8 pos);
};

class Compiler {
private:
    Bytecode* dest;
    Scanner* sc;
    // compiler's internally-tracked stack pointer
    u8 sp;

    // compiler working directory. This is used as an import path.
    fs::path dir;
    // table of imported modules. Associates a sequence of strings (i.e. the symbols in a dot
    // expression) to a constant containing that module's ID.
    Table<vector<string>,u16> modules;
    // constant holding the current module's ID
    u16 curModId;

    // compile a single expression, consuming tokens. t0 is an optional first token. Running will
    // leave the expression on top of the stack.
    void compileExpr(Locals* locals, Token* t0=nullptr);

    // special forms
    void compileAnd(Locals* locals);
    void compileCond(Locals* locals);
    void compileDef(Locals* locals);
    void compileDo(Locals* locals);
    void compileDotExpr(Locals* locals);
    void compileDotToken(Locals* locals, Token& tok);
    void compileFn(Locals* locals);
    void compileIf(Locals* locals);
    void compileImport(Locals* locals); // TODO
    void compileLet(Locals* locals);
    void compileOr(Locals* locals);
    void compileQuote(Locals* locals, bool prefix);
    void compileSet(Locals* locals); // TODO

    // braces and brackets
    void compileBraces(Locals* locals);
    void compileBrackets(Locals* locals);

    // parentheses
    void compileCall(Locals* locals, Token* t0);

    // variables

    // Find a local variable by its name. Returns std::nullopt when no global variable is found,
    // otherwise the corresponding Local value (i.e. stack position or upvalue ID). The value
    // pointed to by levels will be set to the number of layers of enclosing functions that need to
    // be visited to access the variable, thus a value of 0 indicates a local variable on the stack
    // while a value greater than 0 indicates an upvalue.
    optional<Local> findLocal(Locals* locals, const string& name, u32* levels);
    // compile a variable reference
    void compileVar(Locals* locals, const string& name);

    // helpers functions

    // note: this doesn't update the stack pointer
    inline void constant(u16 id) {
        dest->writeByte(fn_bytes::OP_CONST);
        dest->writeShort(id);
    }
    // attempt to parse a name, i.e. a symbol, a dot form, or a dot token. Returns a vector
    // consisting of the names of its constitutent symbols.
    vector<string> tokenizeName(optional<Token> t0=std::nullopt);

public:
    Compiler(const fs::path& dir, Bytecode* dest, Scanner* sc=nullptr);
    ~Compiler();
    // compile all scanner input until EOF. Running the generated code should leave the interpreter
    // stack empty, with lastPop() returning the result of the final toplevel expression
    void compile();

    // set a new scanner
    void setScanner(Scanner* sc);
};

// hash function used by the compiler for module IDs
template<> u32 hash<vector<string>>(const vector<string>& v);

}

#endif
