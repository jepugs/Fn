// Main interface to the interpreter
#ifndef __FN_INTERPRET_HPP
#define __FN_INTERPRET_HPP

#include "istate.hpp"

namespace fn {

// find and load a file. How this is done depends on the import settings in the
// istate object.
void find_and_load(istate* I, symbol_id ns_id);
// interpret a file. This uses the namespace declaration from the file if
// present.
void interpret_file(istate* I, const string& filename);
// interpret a string in the current namespace
void interpret_string(istate* I, const string& src);
// attempt to interpret the first expression of an istream. If successful,
// returns true. If unsuccessful returns false. In the case that an incomplete
// but syntactically valid expression is found, the stream rewinds to the
// starting point, and the istate error is not set. Otherwise an appropriate
// interpreter error is set.
void partial_interpret_string(istate* I, std::istream* in);



}

#endif
