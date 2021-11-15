#ifndef __FN_FFI_BUILTIN_HPP
#define __FN_FFI_BUILTIN_HPP

#include "base.hpp"
#include "interpret.hpp"
#include "ffi/interpreter_handle.hpp"
#include "values.hpp"

namespace fn {

// Install the builtin functions into the fn/builtin namespace in the given
// interpreter. This also causes the bindings to be imported into the fn/repl
// namespace.
void install_builtin(interpreter& inter);

}

#endif
