#ifndef __FN_PLATFORM_HPP
#define __FN_PLATFORM_HPP

#include "base.hpp"

// perform addition, adding 1 to *overflow if carry occurs
extern "C" fn::u64 asm_add_of(fn::u64* overflow, fn::u64 a, fn::u64 b);
// perform a 64x64 -> 128 bit multiplication
extern "C" fn::u64 asm_mul_of(fn::u64* overflow, fn::u64 a, fn::u64 b);
extern "C" fn::u64 asm_addc_of(fn::u64* overflow, fn::u64 a, fn::u64 b);

#endif
