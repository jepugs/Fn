// init.hpp -- initialization routines for the virtual_machine
#ifndef __FN_INIT_HPP
#define __FN_INIT_HPP

#include "interpret.hpp"

namespace fn {

// adds all builtin functions to the interpreter
void init_builtin(interpreter* inter);

}

#endif
