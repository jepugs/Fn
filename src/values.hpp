// values.hpp -- utility functions for working with fn values

#ifndef __FN_VALUES_HPP
#define __FN_VALUES_HPP

#include "array.hpp"
#include "base.hpp"
#include "obj.hpp"
#include "table.hpp"

#include <functional>
#include <cstring>

namespace fn {

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
inline u64 vis_list(value v) {
    return v == V_EMPTY || vtag(v) == TAG_CONS;
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
    return v.raw == V_YES.raw || v.raw == V_NO.raw;
}
inline u64 vis_emptyl(value v) {
    return v.raw == V_EMPTY.raw;
}
inline u64 vis_unin(value v) {
    return v.raw == V_UNIN.raw;
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
        return V_YES;
    } else {
        return V_NO;
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
inline value vbox_cons(fn_cons* p) {
    return vbox_ptr(p, TAG_CONS);
}
inline value vbox_table(fn_table* p) {
    return vbox_ptr(p, TAG_TABLE);
}
inline value vbox_function(fn_function* p) {
    return vbox_ptr(p, TAG_FUNC);
}
inline value vbox_header(gc_header* h) {
    switch (h->type) {
    case GC_TYPE_STRING:
        return vbox_string((fn_string*)h);
    case GC_TYPE_CONS:
        return vbox_cons((fn_cons*)h);
    case GC_TYPE_TABLE:
        return vbox_table((fn_table*)h);
    case GC_TYPE_FUNCTION:
        return vbox_function((fn_function*)h);
    default:
        return V_NIL;
    }
}

// accessing objects from a value
inline f64 vnumber(value v) {
    return v.num;
}
inline void* vpointer(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return v.ptr;
}
inline fn_string* vstring(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_string*)v.ptr;
    // return (fn_string*)(v.ptr & ~TAG_MASK);
}
inline fn_cons* vcons(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_cons*)v.ptr;
}
inline fn_table* vtable(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_table*)v.ptr;
}
inline fn_function* vfunction(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_function*)v.ptr;
}
inline symbol_id vsymbol(value v) {
    return v.raw >> TAG_WIDTH;
}

inline bool vtruth(value v) {
    return !(v.raw == V_NIL.raw || v.raw == V_NO.raw);
}

inline u32 vstrlen(value v) {
    return vstring(v)->size;
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
    return vtable(v)->size;
}

inline void* get_pointer(value v) {
    // mask out the three l_sb with 0's
    return (void*)(v.raw & (~TAG_MASK));
};


// equality
inline bool vsame(value a, value b) {
    return a.raw == b.raw;
}
inline bool vequal(value a, value b) {
    return a == b;
}

// returns an array of two values, key followed by value, which should not be
// freed
value* table_get(istate* S, fn_table* tab, value k);
void table_set(istate* S, fn_table* tab, value k, value v);

value get_metatable(istate* S, value obj);
string type_string(value v);

}

#endif
