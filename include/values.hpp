// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "base.hpp"
// the Value type is defined in vm.hpp
#include "vm.hpp"

namespace fn {

inline void* getPointer(Value v) {
    // mask out the three LSB with 0's
    return (void*)(v.raw & (~7));
};


// 3-bit tags
const u64 TAG_NUM  = 0;
const u64 TAG_CONS = 1;
const u64 TAG_STR  = 2;
const u64 TAG_OBJ  = 3;
const u64 TAG_FUNC = 4;
const u64 TAG_EXT  = 7;

// 8-bit extended tags
const u64 TAG_NULL  = 0007;
const u64 TAG_EMPTY = 0017;
const u64 TAG_FALSE = 0027;
const u64 TAG_TRUE  = 0037;
const u64 TAG_SYM   = 0047;

// Cons cells.
typedef struct Cons {
    Value head;
    Cons *tail;
} Cons;

// A stub describing a function.
typedef struct FuncStub {
    u8 positional;         // number of positional arguments, including optional & keyword arguments
    u8 required;           // number of required positional arguments (minimum arity)
    bool varargs;          // whether this function has a variadic argument
    u32 addr;              // bytecode address of the function
} FuncStub;


/// functions for checking tags

inline int ckTag(Value v, u64 tag) {
    return (v.raw & tag) == tag;
}

inline int isNum(Value v) {
    return ckTag(v, TAG_NUM);
}

inline int isCons(Value v) {
    return ckTag(v, TAG_CONS);
}
inline int isEmpty(Value v) {
    return ckTag(v,TAG_EMPTY);
}
// a list is either a cons or the empty list
inline int isList(Value v) {
    return ckTag(v, TAG_CONS) || ckTag(v,TAG_EMPTY);
}


inline int isStr(Value v) {
    return ckTag(v, TAG_STR);
}
inline int isObj(Value v) {
    return ckTag(v, TAG_OBJ);
}
inline int isFunc(Value v) {
    return ckTag(v, TAG_FUNC);
}

inline int isNull(Value v) {
    return ckTag(v, TAG_NULL);
}
inline int isFalse(Value v) {
    return ckTag(v, TAG_FALSE);
}
inline int isTrue(Value v) {
    return ckTag(v, TAG_TRUE);
}
inline int isBool(Value v) {
    return ckTag(v, TAG_TRUE) || ckTag(v, TAG_FALSE);
}

inline int isSym(Value v) {
    return ckTag(v, TAG_SYM);
}

}

#endif
