#ifndef __FN_ALLOC_HPP
#define __FN_ALLOC_HPP

#include "base.hpp"
#include "compile.hpp"
#include "gc.hpp"
#include "istate.hpp"

namespace fn {

// gc_bytes are byte arrays used internally by some objects. These routines will
// not trigger collection, since this could cause the object containing the
// array to be moved.
gc_bytes* alloc_gc_bytes(istate* S, u64 nbytes, u8 level=GC_GEN_NURSERY);
gc_bytes* realloc_gc_bytes(gc_bytes* src, istate* S, u64 new_size);

// object creation routines. Each of these may trigger a collection at the
// beginning. The "where" argument is a stack address that must be < sp;
// allocation routines without a where argument push their result to the top of
// the stack.
void alloc_string(istate* S, u32 stack_pos, u32 size);
void alloc_string(istate* S, u32 stack_pos, const string& str);
void alloc_cons(istate* S, u32 stack_pos, u32 hd, u32 tl);
void alloc_table(istate* S, u32 stack_pos, u32 init_cap=FN_TABLE_INIT_CAP);
// grow a table to a new minimum capacity. This will also 
void grow_table(istate* S, u32 stack_pos, u32 min_cap);

struct bc_compiler_output;
// create a toplevel function from bytecode compiler output
bool reify_function(istate* S, const scanner_string_table& sst,
        const bc_compiler_output& compiled);
// create a function
void alloc_fun(istate* S, u32 enclosing, constant_id fid);
// create a foreign function
void alloc_foreign_fun(istate* S,
        u32 stack_pos,
        void (*foreign)(istate*),
        u32 num_args,
        bool vari,
        const string& name);

// allocate a function stub with a handle. Technically, we could avoid creating
// any handles here by attaching the stub directly to a function on the stack,
// but this is more straightforward, and stub creation DOES NOT need to be fast,
// since it's only invoked by the compiler.
gc_handle<function_stub>* gen_function_stub(istate* S,
        const scanner_string_table& sst, const bc_compiler_output& compiled);

// create the istate object
istate* alloc_istate(const string& filename, const string& wd);

}

#endif
