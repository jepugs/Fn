#ifndef __FN_VECTOR_HPP
#define __FN_VECTOR_HPP

#include "base.hpp"
#include "obj.hpp"
#include "gc.hpp"
#include "values.hpp"

namespace fn {

// bit shift amount used for each layer of vector addressing
constexpr u64 VEC_INDEX_SHIFT = 5;
// trie branching factor
constexpr u8 VEC_BREADTH = 1 << VEC_INDEX_SHIFT;
// mask used when computing addresses
constexpr u64 VEC_INDEX_MASK = VEC_BREADTH - 1;


// (Destructively) add a new node to the vector node in the stack location
// parent.
void pop_to_vec(istate* S, u32 num);
void push_empty_vec(istate* S);
value& vec_ref(istate* S, u32 vec_pos, u64 index);
void vec_update(istate* S, u32 vec_pos, u32 val_pos, u64 index);
void vec_append(istate* S, u32 vec_pos, u32 val_pos);
void vec_pop(istate* S, u32 vec_pos);
bool vec_is_empty(istate* S, u32 vec_pos);

}

#endif
