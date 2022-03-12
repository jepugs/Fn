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
    bool discard;
    u32 pointer;
    u32 num_objs;
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
void* alloc_bytes(allocator* alloc, u64 size);
// gc_bytes are byte arrays used internally by some objects
gc_bytes* alloc_gc_bytes(allocator* alloc, u64 nbytes);
gc_bytes* realloc_gc_bytes(allocator* alloc, gc_bytes* src, u64 new_size);
void grow_gc_array(allocator* alloc, gc_bytes** arr, u64* cap, u64* size,
        u64 entry_size);
void init_gc_array(allocator* alloc, gc_bytes** arr, u64* cap, u64* size,
        u64 entry_size);
void alloc_string(istate* S, value* where, u32 size);
void alloc_string(istate* S, value* where, const string& str);
void alloc_cons(istate* S, u32 where, u32 hd, u32 tl);
void alloc_table(istate* S, value* where);
void alloc_sub_stub(istate* S, gc_handle* stub_handle);
void alloc_empty_fun(istate* S,
        value* where,
        symbol_id ns_id);
void alloc_foreign_fun(istate* S,
        value* where,
        void (*foreign)(istate*),
        u32 num_args,
        bool vari,
        u32 num_upvals);
void alloc_fun(istate* S, u32 where, u32 enclosing, constant_id fid);

void collect(istate* S);
void collect_now(istate* S);

}

#endif
