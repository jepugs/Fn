// Main interface to the interpreter
#ifndef __FN_INTERPRET_HPP
#define __FN_INTERPRET_HPP

#include "istate.hpp"

namespace fn {

void import_ns(istate* I, symbol_id ns_id);
void interpret_file(istate* I, const string& filename);
void interpret_main_file(istate* I, const string& filename);
void interpret_string(istate* I, const string& src);
void partial_interpret_string(istate* I,
        const string& src,
        u32* bytes_read);
}

#endif
