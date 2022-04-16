#ifndef __FN_BUILTIN_HPP
#define __FN_BUILTIN_HPP

#include "api.hpp"
#include "base.hpp"
#include "istate.hpp"

namespace fn {

void install_internal(istate* S);
void install_builtin(istate* S);

}

#endif
