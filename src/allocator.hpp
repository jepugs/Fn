#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

#include "base.hpp"
#include "bytes.hpp"
#include "namespace.hpp"
#include "obj.hpp"
#include "object_pool.hpp"


// FIXME: these macros should probably be in a header, but not this one (so as
// to not pollute the macro namespace)

// access parts of the gc_header
#define gc_type(h) (((h).type))
#define handle_object(h) ((h->obj))

namespace fn {

// a handle provides a way to maintain references to Fn values while still
// allowing them to be moved by the allocator
struct gc_handle {
    // the object being held by this handle. The allocator will update this
    // pointer during collection, if necessary.
    gc_header* obj;
    // if false, this gc_handle will be cleaned up by the allocator
    bool alive;
    // linked list maintained by the allocator
    gc_handle* next;
};

// TODO: write a value_handle struct that does something similar

constexpr u64 GC_CARD_SIZE = 2 << 12;

struct gc_card_header {
    gc_card_header* next;
    u32 count;
    u16 pointer;

    // following fields are currently unused but will be supported in a future
    // version of the garbage collector

    // persistent cards are used to store large objects
    bool persistent;
    // mark is used to collect persistent cards
    // This isn't used in non-persistent gc cards. Maybe it shouldn't be here?
    bool mark;
    // Set to true when an object in this card is written to
    bool dirty;
    // generation.
    u8 gen;
};

struct alignas(GC_CARD_SIZE) gc_card {
    union {
        gc_card_header h;
        u8 data[GC_CARD_SIZE];
    } u;
};

class allocator {
public:
    u64 mem_usage;
    u64 count;
    // gc is invoked when mem_usage > collect_threshold. collect_threshold is
    // increased if mem_usage > 0.5*collect_threshold after a collection.
    u64 collect_threshold;

    object_pool<gc_card> card_pool;
    gc_card* active_card;

    istate* S;
    gc_handle* gc_handles;

    // deallocate an object, rendering all references to it meaningless
    void dealloc(gc_header* o);

    allocator(istate* I);
    ~allocator();

    // invoke the gc if enough memory is used
    void collect();
    // invoke the gc no matter what
    void force_collect();

    void print_status();
};

constexpr u64 INIT_GC_ARRAY_SIZE = 8;

gc_handle* get_handle(allocator* alloc, gc_header* obj);
void release_handle(gc_handle* handle);

// routine to get bytes from the allocator. Does not trigger collection.
void* get_bytes(allocator* alloc, u64 size);

// gc_bytes are byte arrays used internally by some objects. These routines will
// not trigger collection, since this could cause the object containing the
// array to be moved.
gc_bytes* alloc_gc_bytes(allocator* alloc, u64 nbytes);
gc_bytes* realloc_gc_bytes(allocator* alloc, gc_bytes* src, u64 new_size);

// object creation routines. Each of these triggers a collection at the
// beginning.
void alloc_string(istate* S, u32 where, u32 size);
void alloc_string(istate* S, u32 where, const string& str);
void alloc_cons(istate* S, u32 where, u32 hd, u32 tl);
void alloc_table(istate* S, u32 where);
void alloc_sub_stub(istate* S, gc_handle* stub_handle, const string& name);
void alloc_empty_fun(istate* S,
        u32 where,
        symbol_id ns_id);
void alloc_foreign_fun(istate* S,
        u32 where,
        void (*foreign)(istate*),
        u32 num_args,
        bool vari,
        u32 num_upvals,
        const string& name);
void alloc_fun(istate* S, u32 where, u32 enclosing, constant_id fid);

// functions for mutating function stubs. These are used by the compiler. These
// DO NOT trigger collection. (As such they could theoretically cause an out of
// memory error).
constant_id push_back_const(istate* S, gc_handle* stub_handle, value v);
void push_back_code(istate* S, gc_handle* stub_handle, u8 b);
void push_back_upval(istate* S, gc_handle* stub_handle, bool direct, u8 index);
void update_code_info(istate* S, function_stub* to, const source_loc& loc);
// get the location of an instruction based on the code_info array in the
// function stub. This doesn't perform mutation, but it's here because it needs
// the gc_array functions
code_info* instr_loc(function_stub* stub, u32 pc);

void collect(istate* S);
void collect_now(istate* S);

// gc_arrays wrap gc_bytes objects so that they can be conveniently used as
// dynamic arrays for objects of arbitrary size. These are the routines to
// manipulate them.
template<typename T>
void init_gc_array(istate* S, gc_array<T>* arr) {
    arr->cap = INIT_GC_ARRAY_SIZE;
    arr->size = 0;
    arr->data = alloc_gc_bytes(S->alloc, INIT_GC_ARRAY_SIZE*sizeof(T));
}

template<typename T>
void push_back_gc_array(istate* S, gc_array<T>* arr, const T& value) {
    if (arr->cap <= arr->size) {
        arr->cap *= 2;
        arr->data = realloc_gc_bytes(S->alloc, arr->data, arr->cap * sizeof(T));
    }
    ((T*)arr->data->data)[arr->size++] = value;
}

template<typename T>
T& gc_array_get(gc_array<T>* arr, u64 i) {
    return ((T*)arr->data->data)[i];
}

}

#endif
