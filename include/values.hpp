// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "base.hpp"
#include "table.hpp"

#include <functional>
#include <cstring>

namespace fn {

/// value representation

// all values are 64-bits wide. either 3 or 8 bits are used to hold the tag. the reason for the
// variable tag size is that 3-bit tags can be associated with an 8-byte-aligned pointer (so we only
// need 61 bits to hold the pointer, since the last 3 bits are 0). this requires that none of the
// 3-bit tags occur as prefixes to the 8-bit tags.

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

class cons;
class fn_string;
class object;
class function;
class foreign_func;
struct symbol;

struct obj_header;

// rather than providing a constructor for value, use the (lowercase 'v') value() functions defined
// below
union value {
    u64 raw;
    void* ptr;
    f64 num_val;
    
    // implemented in values.cpp
    bool operator==(const value& v) const;
    bool operator!=(const value& v) const;

    // assignment operators
    value& operator+=(const value& v);

    // functions to check for a tag
    inline bool is_num() const {
        return (raw & 0x7) == TAG_NUM;
    }
    bool is_int() const;
    inline bool is_cons() const {
        return (raw & 0x7) == TAG_CONS;
    }
    inline bool is_str() const {
        return (raw & 0x7) == TAG_STR;
    }
    inline bool is_bool() const {
        return (raw & 0xff) == TAG_BOOL;
    }
    inline bool is_object() const {
        return (raw & 0x7) == TAG_OBJ;
    }
    inline bool is_func() const {
        return (raw & 0x7) == TAG_FUNC;
    }
    inline bool is_foreign() const {
        return (raw & 0x7) == TAG_FOREIGN;
    }
    inline bool is_sym() const {
        return (raw & 0xff) == TAG_SYM;
    }

    inline bool is_null() const {
        return raw == TAG_NULL;
    }
    inline bool is_true() const {
        return raw == ((1 << 8) | TAG_BOOL);
    }
    inline bool is_false() const {
        return raw == TAG_BOOL;
    }
    inline bool is_empty() const {
        return raw == TAG_EMPTY;
    }

    // unsafe generic pointer accessor
    inline void* get_pointer() const {
        return reinterpret_cast<void*>(raw & (~7));
    }

    // unsafe accessors are prefixed with u. they don't check type tags or throw value errors.
    inline f64 unum() const {
        return num_val;
    }
    inline cons* ucons() const {
        return reinterpret_cast<cons*>(raw & (~7));
    }
    inline fn_string* ustr() const {
        return reinterpret_cast<fn_string*>(raw & (~7));
    }
    inline object* uobj() const {
        return reinterpret_cast<object*>(raw & (~7));
    }
    inline function* ufunc() const {
        return reinterpret_cast<function*>(raw & (~7));
    }
    inline foreign_func* uforeign() const {
        return reinterpret_cast<foreign_func*>(raw & (~7));
    }

    // safe accessors will check tags and throw value errors when appropriate
    f64 vnum() const;
    cons* vcons() const;
    fn_string* vstr() const;
    object* vobj() const;
    function* vfunc() const;
    foreign_func* vforeign() const;

    // all functions below are safe. foreign functions can call them without first checking the
    // types of the arguments provided, and an appropriate value error will be generated and handled
    // by the v_m.

    // num functions
    // arithmetic operators are only work on numbers
    value operator+(const value& v) const;
    value operator-(const value& v) const;
    value operator*(const value& v) const;
    value operator/(const value& v) const;

    value pow(const value& expt) const;

    // cons functions
    // these only work on cons, not empty
    value& rhead() const;
    value& rtail() const;

    // list functions. work on cons and empty
    value head() const;
    value tail() const;

    // str functions
    u32 str_len() const;

    // obj functions
    value& get(const value& key) const;
    void set(const value& key, const value& val) const;
    bool has_key(const value& key) const;
    forward_list<value> obj_keys() const;

    // used to get object header
    optional<obj_header*> header() const;

    void error(u64 expected) const;
};

// constant values
constexpr value V_NULL  = { .raw = TAG_NULL };
constexpr value V_FALSE = { .raw = TAG_BOOL };
constexpr value V_TRUE  = { .raw = (1 << 8) | TAG_BOOL };
constexpr value V_EMPTY = { .raw = TAG_EMPTY };

inline void* get_pointer(value v) {
    // mask out the three l_sb with 0's
    return (void*)(v.raw & (~7));
};

// this error is generated when a value is expected to have a certain tag and has a different one.
class value_error : public std::exception {
public:
    // expected tag
    u64 expected;
    value actual;

    value_error(u64 expected, value actual) : expected(expected), actual(actual) { }
};


/// value structures

// common header object for all objects requiring additional memory management
struct alignas(32) obj_header {
    // a value pointing to this. (also encodes the type via the tag)
    value ptr;
    // does the gc manage this object?
    bool gc;
    // gc mark bit (indicates reachable, starts false)
    bool mark;

    obj_header(value ptr, bool gc);
};

// cons cells
struct alignas(32) cons {
    obj_header h;
    value head;
    value tail;

    cons(value head, value tail, bool gc=false);
};

struct alignas(32) fn_string {
    obj_header h;
    const u32 len;
    const char* data;

    fn_string(const string& src, bool gc=false);
    fn_string(const char* src, bool gc=false);
    ~fn_string();

    bool operator==(const fn_string& s) const;
};

struct alignas(32) object {
    obj_header h;
    table<value,value> contents;

    object(bool gc=false);
};

// upvalue
struct upvalue {
    local_addr slot;
    bool direct;
};

// a stub describing a function. these go in the bytecode object
struct func_stub {
    // arity information
    local_addr positional;   // number of positional arguments, including optional & keyword arguments
    local_addr required;     // number of required positional arguments (minimum arity)
    bool varargs;          // whether this function has a variadic argument

    // upvalues
    local_addr num_upvals;
    vector<upvalue> upvals;

    // module i_d as a list
    value mod_id;

    // bytecode address
    bc_addr addr;              // bytecode address of the function

    // get an upvalue and return its id. adds a new upvalue if one doesn't exist
    local_addr get_upvalue(local_addr id, bool direct);
};

// we will use pointers to upvalue_slots to create two levels of indirection. in this way,
// upvalue_slot objects can be shared between function objects. meanwhile, upvalue_slot contains a
// pointer to a value, which is initially on the stack and migrates to the heap if the upvalue
// outlives its lexical scope.

// upvalue_slots are shared objects which track the location of an upvalue (i.e. a value cell
// containing a local_addr variable which was captured by a function). 

// concretely, an upvalue_slot is a reference-counted pointer to a cell containing a value.
// upvalue_slots are initially expected to point to a location on the interpreter stack;
// corresponding upvalues are said to be "open". "closing" an upvalue means copying its value to the
// stack so that functions may access it even after the stack local_addr environment expires.
// upvalue_slots implement this behavior via the field open and the method close(). once closed, the
// upvalue_slot takes ownership of its own value cell, and it will be deleted when the reference count
// drops to 0.
struct upvalue_slot {
    // if true, val is a location on the interpreter stack
    bool* open;
    value** val;
    u32* ref_count;

    upvalue_slot()
        : open(nullptr)
        , val(nullptr)
        , ref_count(nullptr)
    { }
    upvalue_slot(value* place)
        : open(new bool)
        , val(new value*)
        , ref_count(new u32)
    {
        *open = true;
        *val = place;
        *ref_count = 1;
    }
    upvalue_slot(const upvalue_slot& u)
        : open(u.open)
        , val(u.val)
        , ref_count(u.ref_count)
    {
        ++*ref_count;
    }
    ~upvalue_slot() {
        if (ref_count == nullptr) {
            return;
        }

        --*ref_count;
        if (*ref_count == 0) {
            if (!*open) {
                // closed upvals need to have their data deleted
                delete *val;
            }
            delete open;
            delete val;
            delete ref_count;
        }
    }

    upvalue_slot& operator=(const upvalue_slot& u) {
        this->open = u.open;
        this->val = u.val;
        this->ref_count = u.ref_count;
        ++*ref_count;
        return *this;
    }

    // copies this upvalue's value cell to the heap. the new value cell will be cleared when the
    // last reference to this upvalue_slot is deleted.
    void close() {
        *open = false;
        auto v = **val;
        *val = new value;
        **val = v;
    }
};

struct alignas(32) function {
    obj_header h;
    func_stub* stub;
    upvalue_slot* upvals;

    // warning: you must manually set up the upvalues
    function(func_stub* stub, const std::function<void (upvalue_slot*)>& populate, bool gc=false);
    // t_od_o: use refcount on upvalues
    ~function();
};

struct virtual_machine;

// foreign functions
struct alignas(32) foreign_func {
    obj_header h;
    local_addr min_args;
    bool var_args;
    value (*func)(local_addr, value*, virtual_machine*);

    foreign_func(local_addr min_args, bool var_args, value (*func)(local_addr, value*, virtual_machine*), bool gc=false);
};

// symbols in fn are represented by a 32-bit unsigned i_d
struct symbol {
    symbol_id id;
    string name;
};

// the point of the symbol table is to have fast two-way lookup going from a symbol's name to its id
// and vice versa.
class symbol_table {
private:
    table<string,symbol> by_name;
    vector<symbol> by_id;

public:
    symbol_table();

    const symbol* intern(const string& str);
    bool is_internal(const string& str) const;

    optional<const symbol*> find(const string& str) const;

    const symbol& operator[](symbol_id id) const {
        return by_id[id];
    }
};

/// as_value functions to create values
inline value as_value(f64 num) {
    value res = { .num_val=num };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(bool b) {
    return { .raw=(b << 8) | TAG_BOOL};
}
inline value as_value(int num) {
    value res = { .num_val=(f64)num };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(i64 num) {
    value res = { .num_val=(f64)num };
    // make the first three bits 0
    res.raw &= (~7);
    res.raw |= TAG_NUM;
    return res;
}
// f_ix_me: we probably shouldn't have this function allocate memory for the string; rather, once the
// g_c is up and running, we should pass in a pointer which we already know is 8-byte aligned
inline value as_value(const fn_string* str) {
    u64 raw = reinterpret_cast<u64>(str);
    return { .raw = raw | TAG_STR };
}
// n_ot_e: not sure whether it's a good idea to have this one
// inline value as_value(bool b) {
//     return { .raw =  static_cast<u64>(b) | TAG_b_oo_l };
// }
inline value as_value(symbol s) {
    return { .raw = (s.id << 8) | TAG_SYM };
}
inline value as_value(function* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FUNC };
}
inline value as_value(foreign_func* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FOREIGN };
}
inline value as_value(cons* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_CONS };
}
inline value as_value(object* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_OBJ };
}


/// utility functions

// check if two values are the same in memory
inline bool v_same(const value& v1, const value& v2) {
    return v1.raw == v2.raw;
}
// hash an arbitrary value. v_equal(v1,v2) implies the hashes are also equal.
template<> u32 hash<value>(const value& v);
// convert a value to a string. symbols can be nullptr here only if we're really careful to know
// that there are no symbols contained in v.
string v_to_string(value v, const symbol_table* symbols);

// these value(type) functions go from c++ values to fn values


// naming convention: function names prefixed with a lowercase v are used to test/access properties
// of values.

// get the first 3 bits of the value
inline u64 v_short_tag(value v) {
    return v.raw & 7;
}
// get the entire tag of a value. the return value of this expression is intended to be used in a
// switch statement for handling different types of value.
inline u64 v_tag(value v) {
    auto t = v_short_tag(v);
    if (t != TAG_EXT) {
        return t;
    }
    return v.raw & 255;
}

// these functions make the opposite conversion, going from fn values to c++ values. none of them
// are safe (i.e. you should check the tags before doing any of these operations)

// all values corresponding to pointers are structured the same way
inline void* v_pointer(value v) {
    // mask out the three l_sb with 0's
    return (void*)(v.raw & (~7));
}

inline f64 v_num(value v) {
    return v.unum();
}
inline fn_string* v_str(value v) {
    return (fn_string*) v_pointer(v);
}
inline function* v_func(value v) {
    return (function*) v_pointer(v);
}
inline foreign_func* v_foreign(value v) {
    return (foreign_func*) v_pointer(v);
}
inline cons* v_cons(value v) {
    return (cons*) v_pointer(v);
}
inline object* v_obj(value v) {
    return (object*) v_pointer(v);
}
inline bool v_bool(value v) {
    return static_cast<bool>(v.raw >> 8);
}


// since getting the entire symbol object requires access to the symbol table, we provide v_sym_id to
// access the symbol's id directly (rather than v_sym, which would require a symbol_table argument).
inline u32 v_sym_id(value v) {
    return (u32) (v.raw >> 8);
}

// check the 'truthiness' of a value. this returns true for all values but V_NULL and V_FALSE.
inline bool v_truthy(value v) {
    return !v_same(v,V_FALSE) && !v_same(v,V_NULL);
}

// list accessors
inline value v_head(value v) {
    return v_cons(v)->head;
}
inline value v_tail(value v) {
    return v_cons(v)->tail;
}

}

#endif
