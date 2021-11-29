// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "array.hpp"
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
};


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
    char* data;

    // these constructors copy data
    explicit fn_string(const string& src);
    // src must be null terminated
    explicit fn_string(const char* src);
    // this create a string of specified length with uninitialized data
    explicit fn_string(u32 len);
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
struct fn_handle;

// a stub describing a function. these go in the bytecode object
struct function_stub {
    dyn_array<symbol_id> pos_params;  // positional params
    local_address req_args;        // # of required arguments
    optional<symbol_id> vl_param;  // variadic list parameter
    optional<symbol_id> vt_param;  // variadic table parameter

    // if foreign != nullptr, then all following fields are ignored, and calling
    // this function will be deferred to this
    value (*foreign)(fn_handle*,value*);

    code_chunk* chunk;             // chunk containing the function
    string name;                   // optional name for debugging info
    code_address addr;             // function address in its chunk

    local_address num_upvals;
    // upvalues are identified by addresses in the surrounding call frame
    dyn_array<u8> upvals;
    dyn_array<bool> upvals_direct;
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

struct alignas(32) function {
    gc_header h;
    function_stub* stub;
    local_address num_upvals;
    // it is important that these pointers get set to null for foreign
    // functions. Since the function stub is not necessarily available at the
    // time when the function is deleted, this acts as a flag as to whether
    // these fields need to be deleted.
    upvalue_cell** upvals;
    value* init_vals;

    // The function stub must outlive associated function objects.

    // During construction, upvals is allocated according to the num_upvals
    // field in stub. However, it is the responsibility of the function creator
    // to allocate it.
    function(function_stub* stub);
    ~function();
};


// symbols in fn are represented by a 32-bit unsigned ids
struct symtab_entry {
    symbol_id id;
    // pointer here b/c string itself is not trivially copyable, hence not
    // appropriate for a dynamic array.
    string* name;
};

// the point of the symbol table is to have fast two-way lookup going from a symbol's name to its id
// and vice versa.
class symbol_table {
private:
    table<string,symtab_entry> by_name;
    dyn_array<symtab_entry> by_id;
    symbol_id next_gensym = -1;

public:
    symbol_table() = default;
    ~symbol_table();

    symbol_id intern(const string& str);
    bool is_internal(const string& str) const;
    // if symbol_id does not name a valid symbol, returns the empty string
    string symbol_name(symbol_id sym) const;

    symbol_id gensym();
    bool is_gensym(symbol_id id) const;
    // not a true symbol name, but a useful symbolic name for a gensym
    string gensym_name(symbol_id sym) const;

    // acts like gensym_name for gensyms, symbol_name otherwise
    string nice_name(symbol_id sym) const;

    string operator[](symbol_id id) const {
        return symbol_name(id);
    }
};

// constant values
constexpr value V_NIL  = { .raw = TAG_NIL };
constexpr value V_FALSE = { .raw = TAG_FALSE };
constexpr value V_TRUE  = { .raw = TAG_TRUE };
constexpr value V_EMPTY = { .raw = TAG_EMPTY };

// convert a value to a string
string v_to_string(value v,
        const symbol_table* symbols,
        bool code_format=false);


// type information
inline u64 vtag(value v) {
    return v.raw & TAG_MASK;
}
inline u64 vis_number(value v) {
    return vtag(v) == TAG_NUM;
}
inline u64 vis_string(value v) {
    return vtag(v) == TAG_STRING;
}
inline u64 vis_cons(value v) {
    return vtag(v) == TAG_CONS;
}
inline u64 vis_table(value v) {
    return vtag(v) == TAG_TABLE;
}
inline u64 vis_function(value v) {
    return vtag(v) == TAG_FUNC;
}
inline u64 vis_symbol(value v) {
    return vtag(v) == TAG_SYM;
}
inline u64 vis_nil(value v) {
    return v.raw == V_NIL.raw;
}
inline u64 vis_bool(value v) {
    return v == V_TRUE || v == V_FALSE;
}
inline u64 vis_emptyl(value v) {
    return v == V_EMPTY;
}

inline bool vhas_header(value v) {
    auto t = vtag(v);
    return t == TAG_STRING
        || t == TAG_CONS
        || t == TAG_TABLE
        || t == TAG_FUNC;
}

inline gc_header* vheader(value v) {
    return (gc_header*)(v.raw & ~TAG_MASK);
}

// creating values
inline value vbox_number(f64 v) {
    value res = { .num = v };
    res.raw = (res.raw & ~TAG_MASK) | TAG_NUM;
    return res;
}
inline value vbox_symbol(symbol_id v) {
    return { .raw = (((u64)v) << TAG_WIDTH | TAG_SYM)};
}
inline value vbox_bool(bool v) {
    if (v) {
        return V_TRUE;
    } else {
        return V_FALSE;
    }
}
inline value vbox_ptr(void* p, u64 tag) {
    value res = { .ptr = p };
    res.raw = (res.raw & ~TAG_MASK) | tag;
    return res;
}
inline value vbox_string(fn_string* p) {
    return vbox_ptr(p, TAG_STRING);
}
inline value vbox_cons(cons* p) {
    return vbox_ptr(p, TAG_CONS);
}
inline value vbox_table(fn_table* p) {
    return vbox_ptr(p, TAG_TABLE);
}
inline value vbox_function(function* p) {
    return vbox_ptr(p, TAG_FUNC);
}

// accessing objects from a value
inline f64 vnumber(value v) {
    return v.num;
}
inline fn_string* vstring(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_string*)v.ptr;
    // return (fn_string*)(v.ptr & ~TAG_MASK);
}
inline cons* vcons(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (cons*)v.ptr;
}
inline fn_table* vtable(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_table*)v.ptr;
}
inline function* vfunction(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (function*)v.ptr;
}
inline symbol_id vsymbol(value v) {
    return v.raw >> TAG_WIDTH;
}

inline bool vtruth(value v) {
    return !(v == V_NIL || v == V_FALSE);
}

inline u32 vstrlen(value v) {
    return vstring(v)->len;
}

// cons/list
// undefined behavior on V_EMPTY
inline value vhead(value v) {
    return vcons(v)->head;
}
// works on V_EMPTY
inline value vtail(value v) {
    return vcons(v)->tail;
}
inline u32 vlength(value v) {
    u32 ct = 0;
    for (auto it = v; it != V_EMPTY; it=vtail(it)) {
        ++ct;
    }
    return ct;
}
inline value drop(u32 n, value v) {
    auto res = v;
    for (u32 i = 0; i < n; ++i) {
        v = vtail(v);
    }
    return res;
}

// tables
inline u32 vnum_keys(value v) {
    return vtable(v)->contents.get_size();
}
inline forward_list<value> vgetkeys(value v) {
    return vtable(v)->contents.keys();
}
inline bool vhaskey(value v, value key, value* result) {
    auto x = vtable(v)->contents.get(key);
    if (x.has_value()) {
        if (result != nullptr) {
            *result = *x;
        }
        return true;
    } else {
        return false;
    }
}
// returns V_NIL if no such value is found
inline value vget(value v, value key) {
    value res;
    if (vhaskey(v, key, &res)) {
        return res;
    } else {
        return V_NIL;
    }
}
inline void vset(value v, value key, value new_val) {
    vtable(v)->contents.insert(key, new_val);
}



inline void* get_pointer(value v) {
    // mask out the three l_sb with 0's
    return (void*)(v.raw & (~TAG_MASK));
};

// value type/tag checking

inline u64 v_tag(value v) {
    return v.raw & TAG_MASK;
}

// equality
inline bool vsame(value a, value b) {
    return a.raw == b.raw;
}
inline bool vequal(value a, value b) {
    return a == b;
}

}

#endif
