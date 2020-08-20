#ifndef __FN_COMPILE_HPP
#define __FN_COMPILE_HPP

#include "bytes.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

namespace fn {

using namespace fn_scan;

// compile a single expression, consuming tokens. t0 is an optional first token. Running will leave
// the expression on top of the stack.
void compileExpr(Scanner* sc, Bytecode* dest, Token* t0=nullptr);

// compile all scanner input until EOF. Running the generated code should leave the interpreter
// stack empty, with lastPop() returning the result of the final toplevel expression
void compile(Scanner* sc, Bytecode* dest);

}

#endif
