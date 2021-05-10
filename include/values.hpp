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
constexpr u64 TAG_NUM       = 0;
constexpr u64 TAG_CONS      = 1;
constexpr u64 TAG_STRING    = 2;
constexpr u64 TAG_TABLE     = 3;
constexpr u64 TAG_FUNC      = 4;
constexpr u64 TAG_FOREIGN   = 5;
constexpr u64 TAG_NAMESPACE = 6;
constexpr u64 TAG_EXT       = 7;
constexpr u64 TAG_NULL      = 8;
constexpr u64 TAG_TRUE      = 9;
constexpr u64 TAG_FALSE     = 10;
constexpr u64 TAG_EMPTY     = 11;
constexpr u64 TAG_SYM       = 12;

struct symbol;
struct cons;
struct fn_string;
struct fn_table;
struct function;
struct foreign_func;
struct fn_namespace;

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
        return (raw & 0xf) == TAG_NUM;
    }
    bool is_int() const;
    inline bool is_sym() const {
        return (raw & 0xf) == TAG_SYM;
    }

    inline bool is_null() const {
        return raw == TAG_NULL;
    }
    inline bool is_true() const {
        return raw == TAG_TRUE;
    }
    inline bool is_false() const {
        return raw == TAG_FALSE;
    }
    inline bool is_empty() const {
        return raw == TAG_EMPTY;
    }
    inline bool is_bool() const {
        return raw == TAG_TRUE || raw == TAG_FALSE;
    }
    inline bool is_string() const {
        return (raw & 0xf) == TAG_STRING;
    }
    inline bool is_cons() const {
        return (raw & 0xf) == TAG_CONS;
    }
    inline bool is_table() const {
        return (raw & 0xf) == TAG_TABLE;
    }
    inline bool is_func() const {
        return (raw & 0xf) == TAG_FUNC;
    }
    inline bool is_foreign() const {
        return (raw & 0xf) == TAG_FOREIGN;
    }
    inline bool is_namespace() const {
        return (raw & 0xf) == TAG_NAMESPACE;
    }

    // unsafe generic pointer accessor
    inline void* get_pointer() const {
        return reinterpret_cast<void*>(raw & (~0xf));
    }

    // unsafe accessors are prefixed with u. they don't check type tags or throw value errors.
    inline f64 unum() const {
        return num_val;
    }
    inline cons* ucons() const {
        return reinterpret_cast<cons*>(raw & (~0xf));
    }
    inline fn_string* ustring() const {
        return reinterpret_cast<fn_string*>(raw & (~0xf));
    }
    inline fn_table* utable() const {
        return reinterpret_cast<fn_table*>(raw & (~0xf));
    }
    inline function* ufunc() const {
        return reinterpret_cast<function*>(raw & (~0xf));
    }
    inline foreign_func* uforeign() const {
        return reinterpret_cast<foreign_func*>(raw & (~0xf));
    }
    inline fn_namespace* unamespace() const {
        return reinterpret_cast<fn_namespace*>(raw & (~0xf));
    }

    // safe accessors will check tags and throw value errors when appropriate
    f64 vnum() const;
    cons* vcons() const;
    fn_string* vstring() const;
    fn_table* vtable() const;
    function* vfunc() const;
    foreign_func* vforeign() const;
    fn_namespace* vnamespace() const;

    // all functions below are safe. foreign functions can call them without
    // first checking the types of the arguments provided, and an appropriate
    // value error will be generated and handled by the VM

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
    u32 string_len() const;

    // table functions
    value table_get(const value& key) const;
    void table_set(const value& key, const value& val) const;
    bool table_has_key(const value& key) const;
    forward_list<value> table_keys() const;

    // namespace functions
    optional<value> namespace_get(symbol_id key) const;
    void namespace_set(symbol_id key, const value& val) const;
    bool namespace_has_name(symbol_id key) const;
    forward_list<symbol_id> namespace_names() const;

    // generic get which works on namespaces and tables. If this value is not a
    // constant, this results in a value error.
    optional<value> get(const value& key) const;

    // used to get object header
    optional<obj_header*> header() const;

    void error(u64 expected) const;
};

// constant values
constexpr value V_NULL  = { .raw = TAG_NULL };
constexpr value V_FALSE = { .raw = TAG_FALSE };
constexpr value V_TRUE  = { .raw = TAG_TRUE };
constexpr value V_EMPTY = { .raw = TAG_EMPTY };

inline void* get_pointer(value v) {
    // mask out the three l_sb with 0's
    return (void*)(v.raw & (~0xf));
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

    // these constructors copy data
    fn_string(const string& src, bool gc=false);
    // src must be null terminated
    fn_string(const char* src, bool gc=false);
    fn_string(const fn_string& src, bool gc=false);
    ~fn_string();

    string as_string();

    bool operator==(const fn_string& s) const;
};

struct alignas(32) fn_table {
    obj_header h;
    table<value,value> contents;

    fn_table(bool gc=false);
};

// upvalue
struct upvalue {
    local_addr slot;
    bool direct;
};

// a stub describing a function. these go in the bytecode object
struct func_stub {
    // list of parameter names in the order in which they occur
    vector<symbol_id> positional;
    // whether this function accepts a variadic list (resp. table) argument
    bool var_list;
    bool var_table;

    // upvalues
    local_addr num_upvals;
    vector<upvalue> upvals;

    // the namespace in which this function was defined
    fn_namespace* ns;

    // bytecode address
    bc_addr addr;              // bytecode address of the function

    // get an upvalue and return its id. adds a new upvalue if one doesn't exist
    local_addr get_upvalue(local_addr id, bool direct);
};

// we use pointers to upvalue_slots to create two levels of indirection. in this
// way, upvalue_slot objects can be shared between function objects. meanwhile,
// upvalue_slot contains a pointer to a value, which is initially on the stack
// and migrates to the heap if the upvalue outlives its lexical scope.

// upvalue_slots are shared objects which track the location of an upvalue (i.e.
// a value cell containing a local_addr variable which was captured by a
// function).

// concretely, an upvalue_slot is a reference-counted pointer to a cell
// containing a value. upvalue_slots are initially expected to point to a
// location on the interpreter stack; corresponding upvalues are said to be
// "open". "closing" an upvalue means copying its value to the stack so that
// functions may access it even after the stack local_addr environment expires.
// upvalue_slots implement this behavior via the field open and the method
// close(). once closed, the upvalue_slot takes ownership of its own value cell,
// and it will be deleted when the reference count drops to 0.
struct upvalue_slot {
    // if true, val is a location on the interpreter stack
    bool* open;
    value** val;
    u32* ref_count;

    upvalue_slot()
        : open(nullptr)
        , val(nullptr)
        , ref_count(nullptr) {
    }
    upvalue_slot(value* place)
        : open(new bool)
        , val(new value*)
        , ref_count(new u32) {
        *open = true;
        *val = place;
        *ref_count = 1;
    }
    upvalue_slot(const upvalue_slot& u)
        : open(u.open)
        , val(u.val)
        , ref_count(u.ref_count) {
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
    // TODO: use refcount on upvalues
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

// symbols in fn are represented by a 32-bit unsigned ids
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

// key-value stores used to hold global variables and imports
struct alignas(32) fn_namespace {
    obj_header h;
    table<symbol_id,value> contents;

    fn_namespace(bool gc=false);

    optional<value> get(symbol_id name);
    void set(symbol_id name, const value& v);
};

/// as_value functions to create values
inline value as_value(f64 num) {
    value res = { .num_val=num };
    // make the first four bits 0
    res.raw &= (~0xf);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(bool b) {
    return b ? V_TRUE : V_FALSE;
}
inline value as_value(int num) {
    value res = { .num_val=(f64)num };
    // make the first four bits 0
    res.raw &= (~0xf);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(i64 num) {
    value res = { .num_val=(f64)num };
    // make the first four bits 0
    res.raw &= (~0xf);
    res.raw |= TAG_NUM;
    return res;
}
inline value as_value(symbol s) {
    return { .raw = (s.id << 4) | TAG_SYM };
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
inline value as_value(foreign_func* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_FOREIGN };
}
inline value as_value(fn_namespace* ptr) {
    u64 raw = reinterpret_cast<u64>(ptr);
    return { .raw = raw | TAG_NAMESPACE };
}
inline value as_sym_value(symbol_id sym) {
    return { .raw = (sym << 4) | TAG_SYM };
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


// naming convention: function names prefixed with a lowercase v are used to
// test/access properties of values.

// get the tag of a value
inline u64 v_tag(value v) {
    return v.raw & 0xf;
}

// these functions make the opposite conversion, going from fn values to c++
// values. none of them are safe (i.e. you should check the tags before doing
// any of these operations)

// all values corresponding to pointers are structured the same way
inline void* v_pointer(value v) {
    // mask out the three lsb with 0's
    return (void*)(v.raw & (~0xf));
}

inline f64 v_num(value v) {
    return v.unum();
}
inline bool v_bool(value v) {
    return v == V_TRUE;
}
// getting the entire symbol object requires a symbol table, so just get the
// associated symbol_id
inline u32 v_sym_id(value v) {
    return (u32) (v.raw >> 4);
}
inline fn_string* v_string(value v) {
    return (fn_string*) v_pointer(v);
}
inline cons* v_cons(value v) {
    return (cons*) v_pointer(v);
}
inline fn_table* v_table(value v) {
    return (fn_table*) v_pointer(v);
}
inline function* v_func(value v) {
    return (function*) v_pointer(v);
}
inline foreign_func* v_foreign(value v) {
    return (foreign_func*) v_pointer(v);
}
inline fn_namespace* v_namespace(value v) {
    return (fn_namespace*) v_pointer(v);
}

// check the 'truthiness' of a value. this returns true for all values but
// V_NULL and V_FALSE.
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
