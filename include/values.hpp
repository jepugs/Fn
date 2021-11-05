// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "base.hpp"
#include "table.hpp"
#include "vm_handle.hpp"

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
    f64 num_val;
    
    // implemented in values.cpp
    bool operator==(const value& v) const;
    bool operator!=(const value& v) const;

    // functions to check for a tag
    inline u64 tag() const {
        return raw & 0xf;
    }
    inline bool is_num() const {
        return (raw & 0xf) == TAG_NUM;
    }
    bool is_int() const;
    inline bool is_symbol() const {
        return (raw & 0xf) == TAG_SYM;
    }
    inline bool is_string() const {
        return (raw & 0xf) == TAG_STRING;
    }
    inline bool is_null() const {
        return raw == TAG_NULL;
    }
    inline bool is_bool() const {
        return raw == TAG_TRUE || raw == TAG_FALSE;
    }
    inline bool is_empty() const {
        return raw == TAG_EMPTY;
    }
    inline bool is_cons() const {
        return (raw & 0xf) == TAG_CONS;
    }
    inline bool is_table() const {
        return (raw & 0xf) == TAG_TABLE;
    }
    inline bool is_function() const {
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
    inline symbol_id usym_id() const {
        return static_cast<symbol_id>((raw & (~0xf)) >> 4);
    }
    inline fn_string* ustring() const {
        return reinterpret_cast<fn_string*>(raw & (~0xf));
    }
    inline bool ubool() const {
        return this->tag() == TAG_TRUE;
    }
    inline cons* ucons() const {
        return reinterpret_cast<cons*>(raw & (~0xf));
    }
    inline fn_table* utable() const {
        return reinterpret_cast<fn_table*>(raw & (~0xf));
    }
    inline function* ufunction() const {
        return reinterpret_cast<function*>(raw & (~0xf));
    }
    inline foreign_func* uforeign() const {
        return reinterpret_cast<foreign_func*>(raw & (~0xf));
    }
    inline fn_namespace* unamespace() const {
        return reinterpret_cast<fn_namespace*>(raw & (~0xf));
    }

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
    optional<obj_header*> header() const;
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

// value type/tag checking

inline u64 v_tag(value v) {
    return v.raw & 0xf;
}

// bool v_is_num(value v);
// bool v_is_int(value v);
// bool v_is_sym(value v);
// bool v_is_string(value v);
// bool v_is_bool(value v);
// bool v_is_null(value v);
// bool v_is_empty(value v);
// bool v_is_cons(value v);
// bool v_is_list(value v);
// bool v_is_table(value v);
// bool v_is_namespace(value v);
// bool v_is_function(value v);

// equality
inline bool v_same(value a, value b) {
    return a.raw == b.raw;
}
inline bool v_equal(value a, value b) {
    return a == b;
}

// truthiness (everything but null and false are truthy)
inline bool v_truthy(value a) {
    return !(v_same(a, V_FALSE) || v_same(a, V_NULL));
}

// Safe value accessors. These check tags and generate errors if necessary.
f64 v_num(vm_handle vm, value v);
bool v_bool(vm_handle vm, value v);
fn_string* v_string(vm_handle vm, value v);
cons* v_cons(vm_handle vm, value v);
fn_table* v_table(vm_handle vm, value v);
function* v_function(vm_handle vm, value v);
foreign_func* v_foreign(vm_handle vm, value v);
fn_namespace* v_namespace(vm_handle vm, value v);

// functions to create memory managed objects
value alloc_string(vm_handle vm, const string& str);
value alloc_string(vm_handle vm, const char* str);
value alloc_cons(vm_handle vm, value head, value tail);
value alloc_table(vm_handle vm);

// generate a symbol with a unique ID
value v_gensym(vm_handle vm);

// safe arithmetic operations
value v_plus(vm_handle vm, value a, value b);
value v_minus(vm_handle vm, value a, value b);
value v_times(vm_handle vm, value a, value b);
value v_div(vm_handle vm, value a, value b);
value v_pow(vm_handle vm, value a, value b);
// note: the modulus is floor(b) if b is not an integer
value v_mod(vm_handle vm, value a, value b);

// these overloads allow us to operate on values with f64's
value v_plus(vm_handle vm, value a, f64 b);
value v_minus(vm_handle vm, value a, f64 b);
value v_times(vm_handle vm, value a, f64 b);
value v_div(vm_handle vm, value a, f64 b);
value v_pow(vm_handle vm, value a, f64 b);
value v_mod(vm_handle vm, value a, f64 b);

// absolute value
value v_uabs(value a);
value v_abs(vm_handle vm, value a);

// natural log
value v_ulog(value a);
value v_log(vm_handle vm, value a);

// floor and ceiling functions
value v_ufloor(value a);
value v_uceil(value a);

value v_floor(vm_handle vm, value a);
value v_ceil(vm_handle vm, value a);

// ordering
bool v_ult(value a, value b);
bool v_ugt(value a, value b);
bool v_ule(value a, value b);
bool v_uge(value a, value b);

bool v_lt(vm_handle vm, value a, value b);
bool v_gt(vm_handle vm, value a, value b);
bool v_le(vm_handle vm, value a, value b);
bool v_ge(vm_handle vm, value a, value b);

// string functions
value v_ustrlen(value str);
value v_strlen(vm_handle vm, value str);

// symbol functions
const symbol* v_lookup_symbol(vm_handle vm, value sym);
value v_intern(vm_handle vm, value name);
value v_intern(vm_handle vm, const string& name);
value v_intern(vm_handle vm, const char* name);

string v_usym_name(vm_handle vm, value sym);
symbol_id v_usym_id(value sym);

string v_sym_name(vm_handle vm, value sym);
symbol_id v_sym_id(vm_handle vm, value sym);

// list functions

// these only work on conses, not on empty
value v_uhead(value x);
value v_utail(value x);

// generates error on cons
value v_head(vm_handle vm, value x);
// this works on empty
value v_tail(vm_handle vm, value x);

// table/namespace functions
forward_list<value> v_utab_get_keys(value obj);
forward_list<value> v_uns_get_keys(value obj);
forward_list<value> v_tab_get_keys(vm_handle vm, value obj);
forward_list<value> v_ns_get_keys(vm_handle vm, value obj);

bool v_utab_has_key(value obj, value key);
bool v_uns_has_key(value obj, value key);
bool v_tab_has_key(vm_handle vm, value obj, value key);
bool v_ns_has_key(vm_handle vm, value obj, value key);

value v_utab_get(value obj, value key);
void v_utab_set(value obj, value key, value v);
value v_uns_get(value obj, value key);
void v_uns_set(value obj, value key, value v);

value v_tab_get(vm_handle vm, value obj, value key);
void v_tab_set(vm_handle vm, value obj, value key, value v);
value v_ns_get(vm_handle vm, value obj, value key);
void v_ns_set(vm_handle vm, value obj, value key, value v);

forward_list<value> v_get_keys(vm_handle vm, value obj);
value v_get(vm_handle vm, value obj, value key);
void v_set(vm_handle vm, value obj, value key, value v);


/// value structures

// common header object for all objects requiring additional memory management
struct alignas(32) obj_header {
    // a value pointing to this. (also encodes the type via the tag)
    value ptr;

    // gc flags
    // if false, the gc acts as if this object doesn't exist
    bool gc;
    // FIXME: pinning is not thread-safe
    // reference count equal to number of working sets that have this pinned
    i16 pin_count;
    // gc mark bit (true indicates reachable, starts false)
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


// forward declarations for function_stub
struct code_chunk;
struct working_set;

// a stub describing a function. these go in the bytecode object
struct function_stub {
    vector<symbol_id> pos_params;  // positional params
    local_addr req_args;           // # of required arguments
    optional<symbol_id> vl_param;  // variadic list parameter
    optional<symbol_id> vt_param;  // variadic table parameter

    // If foreign_func != nullptr, addr and upvalue fields are ignored, and the
    // function is evaluated by calling foreign_func instead of jumping
    value (*foreign_func)(working_set*, value*);
    code_chunk* chunk;             // chunk containing the function
    bc_addr addr;                  // function address in its chunk

    local_addr num_upvals;
    // upvalues are identified by their offset relative to the base pointer.
    // Upvalues defined in a containing function are then identified
    vector<i32> upvals;

    // get an upvalue address based on its stack location (offset relative to
    // the function base pointer). The upvalue is added if necessary
    local_addr get_upvalue(i32 offset);
};

// A location storing a captured variable. These are shared across functions.
struct upvalue_cell {
    bool closed;         // if false, the value is still on the stack
    u32 ref_count;       // number of functions using this upvalue
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
    upvalue_cell()
        : closed{false}
        , ref_count{1} {
    }
};

struct alignas(32) function {
    obj_header h;
    function_stub* stub;
    upvalue_cell** upvals;
    value* init_vals;

    // The function stub must outlive associated function objects.

    // During construction, upvals is allocated according to the num_upvals
    // field in stub. However, it is the responsibility of the function creator
    // to allocate it.
    function(function_stub* stub, bool gc=false);
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

// foreign functions
struct alignas(32) foreign_func {
    obj_header h;
    local_addr min_args;
    bool var_args;
    optional<value> (*func)(local_addr, value*, virtual_machine*);
    // TODO: new foreign func signature
    //value (*func)(working_set*, value*);

    foreign_func(local_addr min_args,
                 bool var_args,
                 optional<value> (*func)(local_addr, value*, virtual_machine*), bool gc=false);
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

string v_to_string(value v, const symbol_table* symbols);
}

#endif
