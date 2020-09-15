// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "base.hpp"
#include "table.hpp"

#include <functional>
#include <cstring>

namespace fn {

/// value representation

// all values are 64-bits wide. Either 3 or 8 bits are used to hold the tag. The reason for the
// variable tag size is that 3-bit tags can be associated with an 8-byte-aligned pointer (so we only
// need 61 bits to hold the pointer, since the last 3 bits are 0). This requires that none of the
// 3-bit tags occur as prefixes to the 8-bit tags.
union Value {
    u64 raw;
    void* ptr;
    f64 num;

    // implemented in values.cpp
    bool operator==(const Value& v) const;
};

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
constexpr u64 TAG_BOOL  = 0017;
constexpr u64 TAG_EMPTY = 0027;
constexpr u64 TAG_SYM   = 0037;

// constant values
constexpr Value V_NULL  = { .raw = TAG_NULL };
constexpr Value V_FALSE = { .raw = TAG_BOOL };
constexpr Value V_TRUE  = { .raw = (1 << 8) | TAG_BOOL };
constexpr Value V_EMPTY = { .raw = TAG_EMPTY };


/// Value structures

// common header object for all objects requiring additional memory management
struct alignas(16) ObjHeader {
    // a value pointing to this. (Also encodes the type via the tag)
    Value ptr;
    // does the gc manage this object?
    bool gc;
    // gc dirty bit
    bool dirty;

    ObjHeader(Value ptr, bool gc);
};

struct alignas(16) Cons {
    ObjHeader h;
    Value head;
    Value tail;

    Cons(Value head, Value tail, bool gc=false);
};

struct alignas(16) FnString {
    ObjHeader h;
    u32 len;
    char* data;

    FnString(const string& src, bool gc=false);
    FnString(const char* src, bool gc=false);
    ~FnString();

    bool operator==(const FnString& s) const;
};

struct alignas(16) Obj {
    ObjHeader h;
    Table<Value,Value> contents;

    Obj(bool gc=false);
};

// upvalue
struct Upvalue {
    Local slot;
    bool direct;
};

// A stub describing a function. These go in the bytecode object
struct FuncStub {
    // arity information
    Local positional;   // number of positional arguments, including optional & keyword arguments
    Local required;     // number of required positional arguments (minimum arity)
    bool varargs;          // whether this function has a variadic argument

    // upvalues
    Local numUpvals;
    vector<Upvalue> upvals;

    // module ID as a list
    Value modId;

    // bytecode address
    Addr addr;              // bytecode address of the function

    // get an upvalue and return its id. Adds a new upvalue if one doesn't exist
    Local getUpvalue(Local id, bool direct);
};

// We will use pointers to UpvalueSlots to create two levels of indirection. In this way,
// UpvalueSlot objects can be shared between function objects. Meanwhile, UpvalueSlot contains a
// pointer to a Value, which is initially on the stack and migrates to the heap if the Upvalue
// outlives its lexical scope.

// reference counting is used to free UpvalueSlot objects
struct UpvalueSlot {
    // if true, val is a location on the interpreter stack
    bool open;
    Value* val;

    u32 refCount;

    void dereference();
};

struct alignas(16) Function {
    ObjHeader h;
    FuncStub* stub;
    UpvalueSlot** upvals;

    // warning: you must manually set up the upvalues
    Function(FuncStub* stub, const std::function<void (UpvalueSlot**)>& populate, bool gc=false);
    // TODO: use refcount on upvalues
    ~Function();
};

struct VM;

// Foreign functions
struct alignas(16) ForeignFunc {
    ObjHeader h;
    Local minArgs;
    bool varArgs;
    Value (*func)(Local, Value*, VM*);

    ForeignFunc(Local minArgs, bool varArgs, Value (*func)(Local, Value*, VM*), bool gc=false);
};

// Symbols in fn are represented by a 32-bit unsigned ID
struct Symbol {
    u32 id;
    string name;
};

// The point of the symbol table is to have fast two-way lookup going from a symbol's name to its id
// and vice versa.
class SymbolTable {
private:
    Table<string,Symbol> byName;
    vector<Symbol> byId;

public:
    SymbolTable();

    const Symbol* intern(string str);
    bool isInternal(string str);

    const Symbol& operator[](u32 id) {
        return byId[id];
    }
};


/// Utility functions

// Check if two values are the same in memory
inline bool vSame(const Value& v1, const Value& v2) {
    return v1.raw == v2.raw;
}
// Hash an arbitrary value. vEqual(v1,v2) implies the hashes are also equal.
template<> u32 hash<Value>(const Value& v);
// Convert a value to a string. symbols can be nullptr here only if we're really careful to know
// that there are no symbols contained in v.
string vToString(Value v, SymbolTable* symbols);

// These value(type) functions go from C++ values to fn values

inline Value value(f64 num) {
    Value res = { .num=num };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}
inline Value value(bool b) {
    return { .raw=(b << 8) | TAG_BOOL};
}
inline Value value(int num) {
    Value res = { .num=(f64)num };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}
// FIXME: we probably shouldn't have this function allocate memory for the string; rather, once the
// GC is up and running, we should pass in a pointer which we already know is 8-byte aligned
inline Value value(const FnString* str) {
    u64 raw = reinterpret_cast<u64>(str);
    return { .raw = raw | TAG_STR };
}
// NOTE: not sure whether it's a good idea to have this one
// inline Value value(bool b) {
//     return { .raw =  static_cast<u64>(b) | TAG_BOOL };
// }
inline Value value(Symbol s) {
    return { .raw = (s.id << 8) | TAG_SYM };
}
inline Value value(Function* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FUNC };
}
inline Value value(ForeignFunc* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FOREIGN };
}
inline Value value(Cons* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_CONS };
}
inline Value value(Obj* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_OBJ };
}

// naming convention: function names prefixed with a lowercase v are used to test/access properties
// of values.

// get the first 3 bits of the value
inline u64 vShortTag(Value v) {
    return v.raw & 7;
}
// get the entire tag of a value. The return value of this expression is intended to be used in a
// switch statement for handling different types of Value.
inline u64 vTag(Value v) {
    auto t = vShortTag(v);
    if (t != TAG_EXT) {
        return t;
    }
    return v.raw & 255;
}

// these functions make the opposite conversion, going from fn values to C++ values. None of them
// are safe (i.e. you should check the tags before doing any of these operations)

// all values corresponding to pointers are structured the same way
inline void* vPointer(Value v) {
    // mask out the three LSB with 0's
    return (void*)(v.raw & (~7));
}

inline f64 vNum(Value v) {
    return v.num;
}
inline FnString* vStr(Value v) {
    return (FnString*) vPointer(v);
}
inline Function* vFunc(Value v) {
    return (Function*) vPointer(v);
}
inline ForeignFunc* vForeign(Value v) {
    return (ForeignFunc*) vPointer(v);
}
inline Cons* vCons(Value v) {
    return (Cons*) vPointer(v);
}
inline Obj* vObj(Value v) {
    return (Obj*) vPointer(v);
}
inline bool vBool(Value v) {
    return static_cast<bool>(v.raw >> 8);
}


// since getting the entire Symbol object requires access to the symbol table, we provide vSymId to
// access the Symbol's id directly (rather than vSym, which would require a SymbolTable argument).
inline u32 vSymId(Value v) {
    return (u32) (v.raw >> 8);
}

// check the 'truthiness' of a value. This returns true for all values but V_NULL and V_FALSE.
inline bool vTruthy(Value v) {
    return v != V_FALSE && v != V_NULL;
}

// list accessors
inline Value vHead(Value v) {
    return vCons(v)->head;
}
inline Value vTail(Value v) {
    return vCons(v)->tail;
}

}

#endif
