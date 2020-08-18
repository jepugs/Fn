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
constexpr u64 TAG_NUM  = 0;
constexpr u64 TAG_CONS = 1;
constexpr u64 TAG_STR  = 2;
constexpr u64 TAG_OBJ  = 3;
constexpr u64 TAG_FUNC = 4;
constexpr u64 TAG_EXT  = 7;

// 8-bit extended tags
constexpr u64 TAG_NULL  = 0007;
constexpr u64 TAG_EMPTY = 0017;
constexpr u64 TAG_FALSE = 0027;
constexpr u64 TAG_TRUE  = 0037;
constexpr u64 TAG_SYM   = 0047;

// constant values
constexpr Value V_NULL  = { .raw = TAG_NULL };
constexpr Value V_FALSE = { .raw = TAG_FALSE };
constexpr Value V_TRUE  = { .raw = TAG_TRUE };
constexpr Value V_EMPTY = { .raw = TAG_EMPTY };

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


inline Value makeNumValue(f64 f) {
    Value res = { .num=f };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}

inline Value makeStringValue(string *ptr) {
    // FIXME: this assumes malloc has 8-bit alignment.
    string* aligned =  new (malloc(sizeof(string))) string(*ptr);
    u64 raw = reinterpret_cast<u64>(aligned);
    Value res = { .raw=raw|TAG_STR };
    return res;
}

inline string* valueString(Value v) {
    return (string*) getPointer(v);
}


/// functions for checking tags

inline int ckTag(Value v, u64 tag) {
    // check first three bits first
    if ((v.raw & 7) != (tag & 7)) {
        return false;
    }
    if ((v.raw & 7) == TAG_EXT) {
        return (v.raw & 0xff) == (tag & 0xff);
    }
    return true;
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


inline int isString(Value v) {
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


/// boolean truthiness
inline bool isTruthy(Value v) {
    return (!isFalse(v)) && (!isNull(v));
}

}

#endif
