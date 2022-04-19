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

// Collection levels. Collecting at some level will also collect younger
// generations
constexpr u8 GC_LEVEL_EDEN = 0;
constexpr u8 GC_LEVEL_SURVIVOR = 1;
constexpr u8 GC_LEVEL_OLDGEN = 2;
// number of collections an object must survive before being moved to oldgen
constexpr u8 GC_RETIREMENT_AGE = 15;

// multipliers for how often to run major and minor gc
static constexpr u64 GC_MINOR_MULT = 5;
static constexpr u64 GC_MAJOR_MULT = 50;

// a handle provides a way to maintain references to Fn values while still
// allowing them to be moved by the allocator. This can be templated over any of
// the objects beginning with a gc_header
template <typename T>
struct gc_handle {
    // the object being held by this handle. The allocator will update this
    // pointer during collection, if necessary.
    T* obj;
    // if false, this gc_handle will be cleaned up by the allocator
    bool alive;
    // linked list maintained by the allocator
    gc_handle<T>* next;
};

// TODO: write a value_handle struct that does something similar

// this must be a power of two
constexpr u64 GC_CARD_SIZE = 2 << 12;

struct gc_card_header {
    gc_card_header* next;
    u16 count;
    u16 pointer;
    u8 level;   // the collection level (i.e. generation) of this card

    // lowest generation referenced by this card
    u8 lowest_ref;
};

struct alignas(GC_CARD_SIZE) gc_card {
    union {
        gc_card_header h;
        u8 data[GC_CARD_SIZE];
    } u;
};

gc_card* get_gc_card(gc_header* h);
void write_guard(gc_card* card, gc_header* ref);

class allocator {
public:
    u64 mem_usage;
    // gc is invoked when mem_usage > collect_threshold. collect_threshold is
    // increased if mem_usage > 0.5*collect_threshold after a collection.
    u64 collect_threshold;

    object_pool<gc_card> card_pool;
    gc_card* eden;
    gc_card* survivor;
    gc_card* oldgen;
    // counts number of GC cycles
    u64 cycles;

    istate* S;
    gc_handle<gc_header>* gc_handles;

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

template<typename T>
gc_handle<T>* get_handle(allocator* alloc, T* obj) {
    auto res = new gc_handle<T>;
    res->obj = obj;
    res->alive = true;
    res->next = (gc_handle<T>*)alloc->gc_handles;
    alloc->gc_handles = (gc_handle<gc_header>*)res;
    return res;
}

template<typename T>
void release_handle(gc_handle<T>* handle) {
    handle->alive = false;
}

// routines to get bytes from the allocator in the various generations
void* get_bytes_eden(allocator* alloc, u64 size);
void* get_bytes_survivor(allocator* alloc, u64 size);
void* get_bytes_oldgen(allocator* alloc, u64 size);

// gc_bytes are byte arrays used internally by some objects. These routines will
// not trigger collection, since this could cause the object containing the
// array to be moved.
gc_bytes* alloc_gc_bytes(allocator* alloc, u64 nbytes, u8 level=0);
gc_bytes* realloc_gc_bytes(allocator* alloc, gc_bytes* src, u64 new_size);

// object creation routines. Each of these may trigger a collection at the
// beginning. The "where" argument is a stack address that must be < sp;
// allocation routines without a where argument push their result to the top of
// the stack.
void alloc_string(istate* S, u32 stack_pos, u32 size);
void alloc_string(istate* S, u32 stack_pos, const string& str);
void alloc_cons(istate* S, u32 stack_pos, u32 hd, u32 tl);
void alloc_table(istate* S, u32 stack_pos, u32 init_cap=FN_TABLE_INIT_CAP);
// grow a table to a new minimum capacity. This will also 
void grow_table(istate* S, u32 stack_pos, u32 min_cap);

struct bc_compiler_output;
// create a toplevel function from bytecode compiler output
bool reify_function(istate* S, const scanner_string_table& sst,
        const bc_compiler_output& compiled);
// create a function
void alloc_fun(istate* S, u32 enclosing, constant_id fid);
// create a foreign function
void alloc_foreign_fun(istate* S,
        u32 stack_pos,
        void (*foreign)(istate*),
        u32 num_args,
        bool vari,
        u32 num_upvals,
        const string& name);

// allocate a function stub with a handle. Technically, we could avoid creating
// any handles here by attaching the stub directly to a function on the stack,
// but this is more straightforward, and stub creation DOES NOT need to be fast,
// since it's only invoked by the compiler.
gc_handle<function_stub>* gen_function_stub(istate* S,
        const scanner_string_table& sst, const bc_compiler_output& compiled);

// create the istate object
istate* alloc_istate(const string& filename, const string& wd);

void collect(istate* S);
void collect_now(istate* S);

}

#endif
