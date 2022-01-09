// memory.hpp -- in-memory representations of Fn objects
#ifndef __FN_MEMORY_HPP
#define __FN_MEMORY_HPP

#include "base.hpp"
#include "table.hpp"

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
// alignment of objects on the heap. This value actually gives us an unused bit
// after the tag.
constexpr u8 OBJ_ALIGN      = 32;

constexpr u64 TAG_NUM       = 0x00;

// NOTE: I want these to line up with the GC_TYPE tags in base.hpp
constexpr u64 TAG_STRING    = 0x01;
constexpr u64 TAG_CONS      = 0x02;
constexpr u64 TAG_TABLE     = 0x03;
constexpr u64 TAG_FUNC      = 0x04;
constexpr u64 TAG_BIGNUM    = 0x05;

constexpr u64 TAG_SYM       = 0x06;
constexpr u64 TAG_NIL       = 0x07;
constexpr u64 TAG_TRUE      = 0x08;
constexpr u64 TAG_FALSE     = 0x09;
constexpr u64 TAG_EMPTY     = 0x0a;


// this is the main structure to represent a value
union value {
    u64 raw;
    void* ptr;
    f64 num;

    // implemented in values.cpp
    bool operator==(const value& v) const;
    bool operator!=(const value& v) const;
};

// bits used in the gc_header
constexpr u8 GC_GLOBAL_BIT      = 0x10;
constexpr u8 GC_TYPE_BITMASK    = 0x0f;

// GC Types
// NOTE: I want these five to line up with the type tags in values.hpp
constexpr u8 GC_TYPE_STRING     = 0x01;
constexpr u8 GC_TYPE_CONS       = 0x02;
constexpr u8 GC_TYPE_TABLE      = 0x03;
constexpr u8 GC_TYPE_FUNCTION   = 0x04;
constexpr u8 GC_TYPE_BIGNUM     = 0x05;
constexpr u8 GC_TYPE_CHUNK   = 0x0a;

// function stubs (hold code, etc)
constexpr u8 GC_TYPE_FUNC_STUB  = 0x06;
// upvalues
constexpr u8 GC_TYPE_UPVAL      = 0x07;
// vm_state objects
constexpr u8 GC_TYPE_VM_STATE   = 0x08;

// currently unused (intended for use by copying collector)
constexpr u8 GC_TYPE_FORWARD    = 0x0f;

// header contained at the beginning of every object
struct alignas (OBJ_ALIGN) gc_header {
    u8 mark;
    u8 bits;
    i8 pin_count;
    // used to include objects in a list
    gc_header* next;
};

// initialize a gc header in place
void init_gc_header(gc_header* dest, u8 gc_type);
// set a header to designate that its object has been moved to ptr
void set_gc_forward(gc_header* dest, gc_header* ptr);

// number of children per node in the table
constexpr u8 FN_TABLE_BREADTH = 32;

// a string of fixed size
struct alignas (OBJ_ALIGN) fn_string {
    gc_header h;
    u32 size;
    u8* data;
    bool operator==(const fn_string& other) const;
};

// a cell in a linked list
struct alignas (OBJ_ALIGN) fn_cons {
    gc_header h;
    value head;
    value tail;
};

// fn tables are represented by hash tries
struct alignas (OBJ_ALIGN) fn_table {
    gc_header h;
    // when mutated, the table is added to this GC list
    gc_header* updated_list;
    value metatable;
    table<value,value> contents;
};

// A location storing a captured variable. These are shared across functions.
struct upvalue_cell {
    gc_header* hd;
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

struct function_stub;
// represents a function value
struct alignas (OBJ_ALIGN) fn_function {
    gc_header h;
    // when an upvalue is mutated, the function is added to this GC list
    gc_header* updated_list;
    function_stub* stub;
    local_address num_upvals;
    upvalue_cell** upvals;
    value* init_vals;
};

// these are used to compute how much space should be allocated for an object
// for contiguous allocation
u32 string_size(u32 len);
u32 cons_size();
u32 function_size(function_stub stub);
// tables and chunks store additional data (i.e. table entries and code) on the
// normal C heap
u32 table_size();

fn_string* init_string(fn_string* bytes, u32 len);
fn_string* init_string(fn_string* bytes, const string& data);
fn_cons* init_cons(fn_cons* bytes, value hd, value tl);
fn_table* init_table(fn_table* bytes);

};
#endif
