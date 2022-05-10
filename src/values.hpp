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
inline u64 vext_tag(value v) {
    return v.raw & EXT_TAG_MASK;
}
inline bool vis_int(value v) {
    return vtag(v) == TAG_INT;
}
inline bool vis_float(value v) {
    return vtag(v) == TAG_FLOAT;
}
inline bool vis_number(value v) {
    return vtag(v) == TAG_FLOAT
        || vtag(v) == TAG_INT;
}
inline bool vis_string(value v) {
    return vtag(v) == TAG_STRING;
}
inline bool vis_cons(value v) {
    return vtag(v) == TAG_CONS;
}
inline bool vis_list(value v) {
    return v == V_EMPTY || vtag(v) == TAG_CONS;
}
inline bool vis_vec(value v) {
    return vtag(v) == TAG_VECTOR;
}
inline bool vis_table(value v) {
    return vtag(v) == TAG_TABLE;
}
inline bool vis_function(value v) {
    return vtag(v) == TAG_FUNC;
}
inline bool vis_symbol(value v) {
    return vext_tag(v) == TAG_SYM;
}
inline bool vis_nil(value v) {
    return v.raw == V_NIL.raw;
}
inline bool vis_bool(value v) {
    return v.raw == V_YES.raw || v.raw == V_NO.raw;
}
inline bool vis_emptyl(value v) {
    return v.raw == V_EMPTY.raw;
}
inline bool vis_unin(value v) {
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
inline value vbox_int(i32 v) {
    union {
        u64 raw;
        i32 i;
    } u;
    u.i = v;
    value res = { .raw = u.raw << 5 };
    res.raw = (res.raw & ~TAG_MASK) | TAG_INT;
    return res;
}
inline value vbox_float(f64 v) {
    value res = { .f = v };
    res.raw = (res.raw & ~TAG_MASK) | TAG_FLOAT;
    return res;
}
inline value vbox_symbol(symbol_id v) {
    return { .raw = (((u64)v) << EXT_TAG_WIDTH | TAG_SYM)};
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
inline value vbox_string(fn_str* p) {
    return vbox_ptr(p, TAG_STRING);
}
inline value vbox_cons(fn_cons* p) {
    return vbox_ptr(p, TAG_CONS);
}
inline value vbox_vec(fn_vec* p) {
    return vbox_ptr(p, TAG_VECTOR);
}
inline value vbox_table(fn_table* p) {
    return vbox_ptr(p, TAG_TABLE);
}
inline value vbox_function(fn_function* p) {
    return vbox_ptr(p, TAG_FUNC);
}
inline value vbox_header(gc_header* h) {
    switch (h->type) {
    case GC_TYPE_STR:
        return vbox_string((fn_str*)h);
    case GC_TYPE_CONS:
        return vbox_cons((fn_cons*)h);
    case GC_TYPE_VEC:
        return vbox_vec((fn_vec*)h);
    case GC_TYPE_TABLE:
        return vbox_table((fn_table*)h);
    case GC_TYPE_FUN:
        return vbox_function((fn_function*)h);
    default:
        return V_NIL;
    }
}

// accessing objects from a value
inline i32 vint(value v) {
    union {
        i32 i;
        u64 raw;
    } u;
    u.raw = v.raw >> 5;
    return u.i;
}
inline f64 vfloat(value v) {
    return value{.raw = (v.raw ^ TAG_FLOAT)}.f;
}
inline f64 vcast_float(value v) {
    if (vis_int(v)) {
        return vint(v);
    } else {
        return vfloat(v);
    }
}
inline void* vpointer(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return v.ptr;
}
inline fn_str* vstr(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_str*)v.ptr;
    // return (fn_str*)(v.ptr & ~TAG_MASK);
}
inline fn_cons* vcons(value v) {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_cons*)v.ptr;
}
inline fn_vec* vvec(value v)  {
    v.raw = v.raw & ~TAG_MASK;
    return (fn_vec*)v.ptr;
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
    return v.raw >> EXT_TAG_WIDTH;
}

inline bool vtruth(value v) {
    return !(v.raw == V_NIL.raw || v.raw == V_NO.raw);
}

inline u32 vstrlen(value v) {
    return vstr(v)->size;
}

// cons/list
// undefined behavior on V_EMPTY
inline value vhead(value v) {
    return vcons(v)->head;
}
// works on V_EMPTY
inline value vtail(value v) {
    if (vis_emptyl(v)) {
        return V_EMPTY;
    } else {
        return vcons(v)->tail;
    }
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
value* table_get(fn_table* tab, value k);
// get an element by doing linear probing. This is faster for small tables
value* table_get_linear(fn_table* tab, value k);
void table_insert(istate* S, u32 table_pos, u32 key_pos, u32 val_pos);

value get_metatable(istate* S, value obj);
string type_string(value v);

}

#endif
