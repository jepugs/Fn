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
constexpr u64 TAG_FOREIGN = 5;
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
struct Cons {
    Value head;
    Cons *tail;
};

// Foreign functions
struct alignas(8) ForeignFunc {
    u8 minArgs;
    bool varArgs;
    Value (*func)(u16, Value*, VM*);
};

inline Value makeNumValue(f64 f) {
    Value res = { .num=f };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}

inline f64 valueNum(Value v) {
    return v.num;
}

inline Value makeStringValue(const string* ptr) {
    // FIXME: this assumes malloc has 8-bit alignment.
    string* aligned =  new (malloc(sizeof(string))) string(*ptr);
    u64 raw = reinterpret_cast<u64>(aligned);
    Value res = { .raw=raw|TAG_STR };
    return res;
}

inline string* valueString(Value v) {
    return (string*) getPointer(v);
}

// ptr must be 8-bit aligned
inline Value makeForeignValue(ForeignFunc* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FOREIGN };
}

inline ForeignFunc* valueForeign(Value v) {
    return (ForeignFunc*) getPointer(v);
}

// ptr must be 8-bit aligned
inline Value makeFuncValue(Function* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FUNC };
}

inline Function* valueFunc(Value v) {
    return (Function*) getPointer(v);
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
inline int isForeign(Value v) {
    return ckTag(v, TAG_FOREIGN);
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

/// get values as strings
inline string showValue(Value v) {
    if (isString(v)) {
        return "\"" + *valueString(v) + "\"";
    } else if (isNum(v)) {
        return to_string(valueNum(v));
    } else if (isNull(v)) {
        return "null";
    } else if (isFalse(v)) {
        return "false";
    } else if (isTrue(v)) {
        return "true";
    } else if (isForeign(v)) {
        return "<foreign-function>";
    } else if (isFunc(v)) {
        return "<function>";
    } else {
        return "<unprintable-value>";
    }
}

}

#endif
