#ifndef __FN_GC_HPP
#define __FN_GC_HPP

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

// GC Methods

// Rather than try to figure out how inheritance works, here's how I've
// implemented type-based polymorphism in the garbage collector. Other than
// allocation, which happens as needed in custom functions, there are only two
// type-dependent behaviors in the garbage collector: setting up internal
// pointers, and scavenging external pointers. So, we make two tables. We call
// these two types of functions reinitializers and


// When an object is moved, first a bit-for-bit copy is made, and then the
// reinitializer is called to set up the internal pointers on the new instance.
using gc_reinitializer = void (*)(gc_header* obj);
// The scavenger is responsible for two things. (1) It has to update all
// pointers to other gc objects by either copying them or following the
// forwarding pointer. This is why we need a pointer to the allocator object.
// (2) It has to update the dirty bit on the gc_card on which obj lives.
using gc_scavenger = void (*)(gc_header* obj, istate* S);

// initializer/scavenger used for undefined types
extern gc_reinitializer undef_initializer;
extern gc_scavenger undef_scavenger;


// this is the number of entries in the GC method tables
constexpr u64 MAX_GC_TYPES = 16;
// method tables used to look up the appropriate initializer/scavenger based on
// the GC_TYPE.
extern gc_reinitializer gc_reinitializer_table[MAX_GC_TYPES];
extern gc_scavenger gc_scavenger_table[MAX_GC_TYPES];

// rounds size upward to a multiple of align. Align must be a power of 2.
inline constexpr u64 round_to_align(u64 size, u64 align = OBJ_ALIGN) {
    return align + ((size - 1) & ~(align - 1));
}


// GC Cards

// The allocator splits up memory into fixed-size segments (default 4 KiB) that
// we call GC cards. All Fn objects are allocated in GC cards, with objects
// larger than 2 KiB going in custom-sized cards by themselves. Allocation of
// smaller objects is very straightforward: we attempt to allocate the new
// object at the end of the current GC card. If there's no more space, we grab a
// new GC card and do it there instead. Getting a new card may trigger a garbage
// collection.

// GC cards are represented as arrays of bytes, but they always begin with a
// gc_card_header (see definition below).

// GC cards are aligned to their size, which is a power of 2. This means that
// given any object, we can mask out the lower few bits of its address to get
// the address of the gc_card_header.

constexpr u64 GC_CARD_SIZE = 1 << 12;

// objects of size strictly greater than this are considered large and get their
// own GC cards. Large objects are not moved during collection,
constexpr u64 LARGE_OBJECT_CUTOFF = GC_CARD_SIZE / 2;

struct gc_card_header {
    // This is normally a singley-linked list containing all gc cards in the
    // same generation
    gc_card_header* next;
    // a doubly-linked list is maintained for large cards so that they can
    // be moved between generations without copying
    gc_card_header* prev;
    u16 pointer;
    u8 gen;
    // cards containing live objects are marked during collection. This allows
    // us to identify and free dead cards in the tenured generation and large
    // object lists.
    bool mark;
    bool dirty;
    bool large;
};

// gc_cards begin with a header. Actual data begins at this address.
constexpr u16 GC_CARD_DATA_START = round_to_align(sizeof(gc_card_header));

// This struct specifies the alignment and size (in bytes) of a standard gc
// card. This information is taken into consideration by the object_pool
// instance that manages gc cards.
struct alignas(GC_CARD_SIZE) gc_card {
    u8 data[GC_CARD_SIZE];
};


// Generational GC info

// Generations. IMPORTANT NOTE: The numerical ordering here is used internally.
// Older generations must have higher values.
constexpr u8 GC_GEN_NURSERY    = 0;
constexpr u8 GC_GEN_SURVIVOR   = 1;
constexpr u8 GC_GEN_TENURED    = 2;

// number of collections an object must survive to be moved to the tenured
// generation
constexpr u8 GC_TENURE_AGE    = 16;

// number of GC cards the nursery is allowed to use before triggering an
// evacuation
constexpr u8 DEFAULT_NURSERY_SIZE    = 32;
// if the survivor space exceeds this number of gc cards, then it will be
// compacted/collected the next time the nursery is evacuated.
constexpr u8 DEFAULT_SURVIVOR_SIZE   = 128;

// a deck consists of two linked lists of gc cards, all in the same generation.
// There's a singley-linked list of normal gc cards and a doubley-linked list of
// large cards. This allows us to allocate objects of any size in the deck.

struct gc_deck {
    // number of cards being used total. This includes large object cards.
    u32 num_cards;
    u32 num_objs;
    u8 gen;
    // first gc_card of the generation. We guarantee this (and foot) are not
    // null.
    gc_card_header* head;
    // gc_cards are added to the back of the generation so we can quickly
    // iterate over objects in the order they were evacuated
    gc_card_header* foot;
    // oversize objects go in a separate linked list
    gc_card_header* large_obj_head;
    gc_card_header* large_obj_foot;
};

// gc_handle provides a way to protect distinguished values from being swept.
// The object represented by the handle may still be moved during a collection,
// but the handle's reference will be updated appropriately.

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

struct allocator {
    object_pool<gc_card> card_pool;
    gc_deck nursery;
    gc_deck survivor;
    gc_deck tenured;
    object_pool<gc_handle<gc_header>> handle_pool;
    gc_handle<gc_header>* handles;

    // used during collection; the maximum generation being copied
    u8 max_compact_gen;
    // used during collection; the maximum generation being scavenged
    u8 max_scavenge_gen;
};

// initialize a new allocator. This mainly involves setting up the decks
void init_allocator(allocator& alloc, istate* S);
// allocate a new nursery object. This will trigger a garbage collection if the
// nursery is full
gc_header* alloc_nursery_object(istate* S, u64 size);
// get the object at the specified address in the card. (Warning: addr is not
// validated)
gc_header* gc_card_object(gc_card_header* card_info, u16 addr);
// get the gc card of the specified object
gc_card_header* get_gc_card_header(gc_header* obj);
// whenever a new reference is written anywhere into a gc card, this function
// must be called to ensure that the dirty bit of the card is updated
// appropriately.
void write_guard(gc_card_header* card, gc_header* ref);

// The following two functions are intended to be called from within scavenger
// functions for every external pointer in the scavenged objects. These
// functions decide whether an object should be moved as part of the current
// garbage collection phase. If so, they copy the object and update its internal
// pointers.

// Conditionally copy a live object and return the updated location.
// - leaves behind a forwarding pointer in the old location
// - if object has already been copied this cycle, returns the forward pointer
// - generation to copy into is determined automatically
// - internal pointers are updated (but not external pointers)
// - objects in generations older than alloc.max_compact_gen
// - large objects are not copied
// - when obj is not copied, the original pointer is returned
// - dirty bits are not set
gc_header* copy_live_object(gc_header* obj, istate* S);
// If the value v is managed by the garbage collector, then this invokes
// copy_live_object() on the corresponding object and returns a new value
// pointing to it. Else simply returns v.
value copy_live_value(value v, istate* S);

// collection routines
template<typename T>
gc_handle<T>* get_handle(allocator* alloc, T* obj) {
    auto res = (gc_handle<T>*)alloc->handle_pool.new_object();
    res->obj = obj;
    res->alive = true;
    res->next = (gc_handle<T>*)alloc->handles;
    alloc->handles = (gc_handle<gc_header>*)res;
    return res;
}

template<typename T>
void release_handle(gc_handle<T>* handle) {
    handle->alive = false;
}

// perform an evacuation of the nursery into the survivor generation
void evacuate_nursery(istate* S);
// perform a compacting collection of the nursery and survivor generation.
void collect_survivor(istate* S);
// perform a "full" garbage collection. This will compact the nursery and
// survivor generations and additionally sweep any empty cards or unused large
// objects in the tenured generation. This does not compact the tenured
// generation.
void collect_full(istate* S);
// perform a full, compacting collection of all generations including the
// tenured generation.
void compact_full(istate* S);

// collect garbage now.
void collect_now(istate* S);

}


#endif
