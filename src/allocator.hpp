#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

// Defining this variable (which should be done via CMAKE) causes the garbage
// collector to run frequently and often emit output.
//#define GC_DEBUG

#include "base.hpp"
#include "bytes.hpp"
#include "namespace.hpp"
#include "obj.hpp"
#include "object_pool.hpp"


// FIXME: these macros should probably be in a header, but not this one (so as
// to not pollute the macro namespace)

// access parts of the gc_header
#define gc_mark(h) (((h).mark))
#define gc_global(h) (((h).bits & GC_GLOBAL_BIT) == GC_GLOBAL_BIT)
#define gc_old(h) (((h).bits & GC_OLD_BIT) == GC_OLD_BIT)
#define gc_type(h) (((h).bits & GC_TYPE_BITMASK))

#define gc_set_mark(h) (((h).mark = 1))
#define gc_unset_mark(h) (((h).mark = 0))

#define gc_set_global(h) ((h).bits = (h).bits | GC_GLOBAL_BIT)
#define gc_unset_global(h) (((h).bits &= ~GC_GLOBAL_BIT))

#define gc_set_old(h) ((h).bits = (h).bits | GC_OLD_BIT)
#define gc_unset_old(h) (((h).bits &= ~GC_OLD_BIT))


namespace fn {

class allocator {
public:
    u64 mem_usage;
    u64 count;
    // gc is invoked when mem_usage > collect_threshold. collect_threshold is
    // increased if mem_usage > 0.5*collect_threshold after a collection.
    u64 collect_threshold;

    object_pool<fn_cons> cons_pool;
    object_pool<fn_function> fun_pool;
    object_pool<fn_table> table_pool;

    istate* S;
    gc_header* objs_head;

    // deallocate an object, rendering all references to it meaningless
    void dealloc(gc_header* o);

    // helpers for mark
    //void mark_descend_value(value v);
    void add_mark_value(value v);
    // this marks an object and adds accessible nodes to the marking_list.
    void mark_descend(gc_header* o);
    // starting from roots and pins, set the mark on all accessible objects.
    void mark();

    // add all root objects to marking_list
    void add_roots_for_marking();

    // sweeping iterates over the list of objects doing the following:
    // - delete unmarked objects
    // - remove the mark from marked objects
    void sweep();

    allocator(istate* I);
    ~allocator();

    // invoke the gc if enough memory is used
    void collect();
    // invoke the gc no matter what
    void force_collect();

    void print_status();
};

void* alloc_bytes(allocator* alloc, u64 size);
void alloc_string(istate* S, value* where, u32 size);
void alloc_string(istate* S, value* where, const string& str);
void alloc_cons(istate* S, value* where, value hd, value tl);
void alloc_table(istate* S, value* where);
void alloc_sub_stub(istate* S, function_stub* where);
void alloc_empty_fun(istate* S,
        value* where,
        symbol_id ns_id);
void alloc_foreign_fun(istate* S,
        value* where,
        void (*foreign)(istate*),
        u32 num_args,
        bool var_arg,
        symbol_id ns_id);
void alloc_fun(istate* S, value* where, fn_function* enclosing,
        function_stub* stub);

void collect(istate* S);
void collect_now(istate* S);

}

#endif
