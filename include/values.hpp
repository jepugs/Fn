// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "base.hpp"
#include "table.hpp"

#include <functional>
#include <cstring>

namespace fn {

/// value representation

// all values are 64-bits wide. The 4 least significant bits are referred to as
// the tag, and are used to encode the type of the value. All the pointers used
// for Fn objects are 16-byte aligned (in fact 32-). This allows us to store an
// entire 64-bit pointer along with the tag, since we know the 4 least
// significant digits of the pointer address will all be 0.

// tags
constexpr u64 TAG_WIDTH     = 4;
constexpr u64 TAG_MASK      = (1 << TAG_WIDTH) - 1;


constexpr u64 TAG_NUM       = 0;

// NOTE: These four must line up with the GC_TYPE tags in base.hpp
constexpr u64 TAG_STRING    = 1;
constexpr u64 TAG_CONS      = 2;
constexpr u64 TAG_TABLE     = 3;
constexpr u64 TAG_FUNC      = 4;

constexpr u64 TAG_SYM       = 5;
constexpr u64 TAG_NIL       = 6;
constexpr u64 TAG_TRUE      = 7;
constexpr u64 TAG_FALSE     = 8;
constexpr u64 TAG_EMPTY     = 9;

struct fn_string;
struct cons;
struct fn_table;
struct function;

// Design notes for value:
// - we opt to make value a union with methods rather than a struct
// - union methods are used for accessing pointers and checking tags only. The
//   sole exception to this rule are some unsafe arithmetic operations.
// - values are created using as_value (or as_sym_value)
// - values 

// rather than providing a constructor for value, use the (lowercase 'v')
// value() functions defined below
union value {
    u64 raw;
    void* ptr;
    f64 num;
    
    // implemented in values.cpp
    bool operator==(const value& v) const;
    bool operator!=(const value& v) const;

    // functions to check for a tag
    inline u64 tag() const {
        return raw & TAG_MASK;
    }
    inline bool is_num() const {
        return (raw & TAG_MASK) == TAG_NUM;
    }
    inline bool is_symbol() const {
        return (raw & TAG_MASK) == TAG_SYM;
    }
    inline bool is_string() const {
        return (raw & TAG_MASK) == TAG_STRING;
    }
    inline bool is_null() const {
        return raw == TAG_NIL;
    }
    inline bool is_bool() const {
        return raw == TAG_TRUE || raw == TAG_FALSE;
    }
    inline bool is_empty() const {
        return raw == TAG_EMPTY;
    }
    inline bool is_cons() const {
        return (raw & TAG_MASK) == TAG_CONS;
    }
    inline bool is_table() const {
        return (raw & TAG_MASK) == TAG_TABLE;
    }
    inline bool is_function() const {
        return (raw & TAG_MASK) == TAG_FUNC;
    }

    // unsafe generic pointer accessor
    inline void* get_pointer() const {
        return reinterpret_cast<void*>(raw & (~TAG_MASK));
    }

    // // unsafe accessors are prefixed with u. they don't check type tags or throw value errors.
    // inline f64 unum() const {
    //     return val;
    // }
    // inline symbol_id usym_id() const {
    //     return static_cast<symbol_id>((raw & (~TAG_MASK)) >> 4);
    // }
    // inline fn_string* ustring() const {
    //     return reinterpret_cast<fn_string*>(raw & (~TAG_MASK));
    // }
    // inline bool ubool() const {
    //     return this->tag() == TAG_TRUE;
    // }
    // inline cons* ucons() const {
    //     return reinterpret_cast<cons*>(raw & (~TAG_MASK));
    // }
    // inline fn_table* utable() const {
    //     return reinterpret_cast<fn_table*>(raw & (~TAG_MASK));
    // }
    // inline function* ufunction() const {
    //     return reinterpret_cast<function*>(raw & (~TAG_MASK));
    // }
    // inline foreign_func* uforeign() const {
    //     return reinterpret_cast<foreign_func*>(raw & (~TAG_MASK));
    // }
    // inline fn_namespace* unamespace() const {
    //     return reinterpret_cast<fn_namespace*>(raw & (~TAG_MASK));
    // }

    // all functions below are safe. foreign functions can call them without
    // first checking the types of the arguments provided, and an appropriate
    // value error will be generated and handled by the VM

    // (Unsafe) arithmetic functions.
    // These are entirely unsafe.
    value operator+(const value& v) const;
    value operator-(const value& v) const;
    value operator*(const value& v) const;
    value operator/(const value& v) const;
    value operator%(const value& v) const;
    bool operator<(const value& v) const;
    bool operator>(const value& v) const;
    bool operator<=(const value& v) const;
    bool operator>=(const value& v) const;

    value pow(const value& expt) const;

    // used to get object header
    optional<gc_header*> header() const;
};

// constant values
constexpr value V_NIL  = { .raw = TAG_NIL };
constexpr value V_FALSE = { .raw = TAG_FALSE };
constexpr value V_TRUE  = { .raw = TAG_TRUE };
constexpr value V_EMPTY = { .raw = TAG_EMPTY };

inline void* get_pointer(value v) {
    // mask out the three l_sb with 0's
    return (void*)(v.raw & (~TAG_MASK));
};

// value type/tag checking

inline u64 v_tag(value v) {
    return v.raw & TAG_MASK;
}

// equality
inline bool v_same(value a, value b) {
    return a.raw == b.raw;
}
inline bool v_equal(value a, value b) {
    return a == b;
}

// truthiness (everything but null and false are truthy)
inline bool v_truthy(value a) {
    return !(v_same(a, V_FALSE) || v_same(a, V_NIL));
}

// (unsafe) value accessors:
inline void* vpointer(value v) {
    return reinterpret_cast<void*>(v.raw & (~TAG_MASK));
}

inline f64 vnumber(value v) {
    return v.num;
}

inline fn_string* vstring(value v) {
    return (fn_string*)vpointer(v);
}

inline symbol_id vsymbol(value v) {
    return (symbol_id)(v.raw >> TAG_WIDTH);
}

inline cons* vcons(value v) {
    return (cons*)vpointer(v);
}

inline fn_table* vtable(value v) {
    return (fn_table*)vpointer(v);
}

// returns true
inline bool vtruth(value v) {
    return V_NIL != v && V_FALSE != v;
}

inline function* vfunction(value v) {
    return (function*)vpointer(v);
}



// list functions

// these only work on conses, not on empty
value v_head(value x);
value v_tail(value x);

// table functions
forward_list<value> v_tab_get_keys(value obj);

bool v_tab_has_key(value obj, value key);

value v_tab_get(value obj, value key);
void v_tab_set(value obj, value key, value v);


/// value structures

// cons cells
struct alignas(32) cons {
    gc_header h;
    value head;
    value tail;

    cons(value head, value tail);
};

struct alignas(32) fn_string {
    gc_header h;
    u32 len;
    const char* data;

    // these constructors copy data
    fn_string(const string& src);
    // src must be null terminated
    fn_string(const char* src);
    fn_string(const fn_string& src);
    ~fn_string();

    string as_string();

    bool operator==(const fn_string& s) const;
};

struct alignas(32) fn_table {
    gc_header h;
    table<value,value> contents;

    fn_table();
};


// forward declarations for function_stub
struct code_chunk;
struct working_set;

// a stub describing a function. these go in the bytecode object
struct function_stub {
    vector<symbol_id> pos_params;  // positional params
    local_address req_args;        // # of required arguments
    optional<symbol_id> vl_param;  // variadic list parameter
    optional<symbol_id> vt_param;  // variadic table parameter

    code_chunk* chunk;             // chunk containing the function
    code_address addr;             // function address in its chunk

    local_address num_upvals;
    // upvalues are identified by addresses in the surrounding call frame
    vector<u8> upvals;
    vector<bool> upvals_direct;
    // if the corresponding entry of upvals_direct = false, then it means this
    // upvalue corresponds to an upvalue in the surrounding frame. Otherwise
    // it's a stack value.

    // get an upvalue address based on its stack location (offset relative to
    // the function base pointer). The upvalue is added if necessary
    local_address add_upvalue(u8 addr, bool direct);
};

// A location storing a captured variable. These are shared across functions.
struct upvalue_cell {
    u32 ref_count;       // number of functions using this upvalue
    bool closed;         // if false, the value is still on the stack
    stack_address pos;   // position on the stack while open
    value closed_value;  // holds the upvalue for closed cells
    // TODO: lock these :'(

    inline void reference() {
        ++ref_count;
    }
    inline void dereference() {
        --ref_count;
    }
    inline bool dead() {
        return ref_count == 0;
    }
    inline void close(value v) {
        closed_value = v;
        closed = true;
    }

    // create a new open cell with reference count 1
    upvalue_cell(stack_address pos)
        : ref_count{1}
        , closed{false}
        , pos{pos} {
    }
};

struct interpreter_handle;
struct alignas(32) function {
    gc_header h;
    function_stub* stub;
    // If foreign_func != nullptr, addr and upvalue fields are ignored, and the
    // function is evaluated by calling foreign_func instead of jumping
    value (*foreign_func)(interpreter_handle*,local_address,value*);
    upvalue_cell** upvals;
    value* init_vals;

    // The function stub must outlive associated function objects.

    // During construction, upvals is allocated according to the num_upvals
    // field in stub. However, it is the responsibility of the function creator
    // to allocate it.
    function(function_stub* stub);
    ~function();
};

struct virtual_machine;


// FIXME: Foreign functions need to be refactored. Right now the associated
// function pointer returns an optional value. Returning nothing activates
// special behavior that avoids all the usual safety measures. This is mainly
// for apply, which needs to be able to affect control flow.
//
// The right thing to do here is to offload the functionality provided by these
// functions into VM instructions. I will change the initialization code so that
// functions can be defined directly in terms of bytecode, and allowing all VM
// operations to be easily exposed to functions.

// symbols in fn are represented by a 32-bit unsigned ids
struct symtab_entry {
    symbol_id id;
    string name;
};

// the point of the symbol table is to have fast two-way lookup going from a symbol's name to its id
// and vice versa.
class symbol_table {
private:
    table<string,symtab_entry> by_name;
    vector<symtab_entry> by_id;
    // TODO: figure out why this can't be a full number :(
    symbol_id next_gensym = (symbol_id)(-1);

public:
    symbol_table() = default;

    symbol_id intern(const string& str);
    bool is_internal(const string& str) const;
    // if symbol_id does not name a valid symbol, returns the empty string
    string symbol_name(symbol_id sym) const;

    symbol_id gensym();
    bool is_gensym(symbol_id id) const;
    // not a true symbol name, but a useful symbolic name for a gensym
    string gensym_name(symbol_id sym) const;

    string operator[](symbol_id id) const {
        return symbol_name(id);
    }
};

/// as_value functions to create values
inline value as_value(f64 num) {
    value res = { .num=num };
    // make the first four bits 0
    res.raw &= (~TAG_MASK);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(bool b) {
    return b ? V_TRUE : V_FALSE;
}
inline value as_value(int num) {
    value res = { .num=(f64)num };
    // make the first four bits 0
    res.raw &= (~TAG_MASK);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(i64 num) {
    value res = { .num=(f64)num };
    // make the first four bits 0
    res.raw &= (~TAG_MASK);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(const fn_string* str) {
    u64 raw = reinterpret_cast<u64>(str);
    return { .raw = raw | TAG_STRING };
}
inline value as_value(cons* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_CONS };
}
inline value as_value(fn_table* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_TABLE };
}
inline value as_value(function* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FUNC };
}
inline value as_sym_value(symbol_id sym) {
    return { .raw = (((u64)sym) << TAG_WIDTH) | TAG_SYM };
}

string v_to_string(value v, const symbol_table* symbols);
}

#endif
