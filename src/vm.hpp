// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "base.hpp"
#include "obj.hpp"
#include "istate.hpp"

namespace fn {

// return value of false indicates no such variable
void mutate_global(istate* S, symbol_id name, value v);
void execute_fun(istate* S);

}

#endif
