// Main interface to the interpreter
#ifndef __FN_INTERPRET_HPP
#define __FN_INTERPRET_HPP

#include "istate.hpp"

namespace fn {

void require_file(istate* S, const string& path);
void require_package(istate* S, const string& id);
void require_string(istate* S);

}

#endif
