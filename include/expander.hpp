#ifndef __FN_EXPANDER
#define __FN_EXPANDER

#include "base.hpp"
#include "llir.hpp"
#include "parse.hpp"

namespace fn {

struct interpreter;

llir_form* expand_ast(symbol_table& symtab,
        interpreter& macro_vm,
        fn_parse::ast_node* src);

}

#endif
