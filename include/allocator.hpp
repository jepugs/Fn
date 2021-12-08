#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

// Defining this variable (which should be done via CMAKE) causes the garbage
// collector to run frequently and often emit output.
// #define GC_DEBUG

#include "base.hpp"
#include "bytes.hpp"
#include "namespace.hpp"
#include "values.hpp"

#include <list>

namespace fn {

struct allocator;

// A root_stack is a stack of garbage collector root objects. These are used for
// the virtual machine stack.

// TODO: replace the vector here with a manually-managed array
struct root_stack {
    friend class allocator;
private:
    // only the allocator is allowed to construct these
    root_stack();
    u32 pointer;
    dyn_array<value> contents;
    dyn_array<function*> function_stack;
    // open upvalues in descending order by stack position
    std::list<upvalue_cell*> upvals;
    bool dead; // if true, stack will be deleted when garbage is collected
    value last_pop; // the last value popped off the stack

public:
    // no copying! These cannot be moved around in memory or else
    // mark_for_deletion will not work properly.
    root_stack(const root_stack& other) = delete;
    root_stack& operator=(const root_stack& other) = delete;
    root_stack(root_stack&& other) = delete;
    root_stack& operator=(root_stack&& other) = delete;

    ~root_stack();

    u32 get_pointer() const;

    // Declaring these inline didn't really do anything on GCC 11.1.0.
    value peek(u32 offset=0) const;
    value peek_bottom(u32 offset=0) const;
    // FIXME: this should actually pop to a working set if you're gonna use the
    // value
    value pop();
    value get_last_pop();
    void pop_times(u32 n);
    void push(value v);
    // set a value, indexed so 0 is the bottom
    void set(u32 offset, value v);

    // root_stack also maintains a stack of the functions in the call frame, in
    // order to make sure that they are visible.
    void push_function(function* callee);
    void pop_function();

    // set the upvalues of a function object with the given base pointer
    upvalue_cell* get_upvalue(stack_address loc);
    // close all upvalues with stack addresses >= base_addr. (Closing an upvalue
    // involves copying its value to the heap and removing it from the list of
    // upvalues). This does not change last_pop.
    void close(u32 base_addr);
    // like close(), but first saves the top of the stack and pushes it back
    // after doing the close (so the final stack size is base_addr+1). This sets
    // last_pop to the return value.
    void do_return(u32 base_addr);

    value& operator[](u32 offset) {
        return contents[offset];
    }

    void mark_for_deletion();
};


// A working_set is a unique object (read: move semantics only) used to store
// values newly created by the allocator before they are added to the list for
// garbage collection. This is done automatically on destruction.

// The GC may still access an object's header in this time, but only if it has
// been made accessible from a root object.
struct working_set {
private:
    // if true, this working_set no longer owns its objects (due to a move), so
    // cleanup won't be done in the destructor
    bool released;
    allocator* alloc; // weak reference

    // new objects allocated for this working set (not yet in the gc list)
    forward_list<gc_header*> new_objects;
    // pinned objects are guaranteed to live during this working_set's life
    forward_list<gc_header*> pinned_objects;

    // Gives newly created objects to the GC and releases pins. Does nothing
    // after the first call. This is called by the destructor.
    void add_to_gc();

public:
    working_set(const working_set& other) = delete;
    working_set& operator=(const working_set& other) = delete;

    working_set(allocator* use_alloc);
    working_set(working_set&& other) noexcept;
    ~working_set();

    working_set& operator=(working_set&& other) noexcept;

    value add_cons(value hd, value tl);
    value add_string(const string& s);
    value add_string(const fn_string& s);
    value add_string(u32 len);
    value add_table();
    // Create a function. The caller is responsible for correctly setting
    // upvalues and init values.
    value add_function(function_stub* stub);
    // add a chunk with name the specified namespace. (The namespace will be
    // created if it does not already exist).
    code_chunk* add_chunk(symbol_id id);

    // FIXME: pinning is not thread-safe

    // pin an existing value so it won't be collected for the lifetime of the
    // working_set. Returns v. The value is automagically unpinned if its
    // reference count hits 0.
    value pin_value(value v);
    void pin(gc_header* gc);
};

struct allocator {
    friend class working_set;
private:
    // note: we guarantee that every pointer in this array is actually memory
    // managed by this garbage collector
    std::list<gc_header*> objects;
    // this is the list of objects minus values accessible from global values.
    std::list<gc_header*> sweep_list;
    // list of global objects with mutable cells. This is to handle the case
    // where a globally accessible object ceases to be globally accessible.
    std::list<gc_header*> mutable_globals;

    // holds global variables
    global_env* globals;
    // flag used to determine garbage collector behavior. starts out false to
    // allow initialization
    bool gc_enabled;
    // if true, garbage collection will automatically run when next enabled
    bool to_collect;
    u64 mem_usage;
    // gc is invoked when mem_usage > collect_threshold. collect_threshold is
    // increased if mem_usage > 0.5*collect_threshold after a collection.
    u64 collect_threshold;
    // number of objects
    u32 count;

    // roots for the mark and sweep algorithm
    dyn_array<gc_header*> root_objects;
    // variable-size stacks of root objects. Used for vm stacks.
    std::list<root_stack*> root_stacks;
    // pins are temporary root objects which are added and managed by
    // working_sets. When an object's working_set reference count falls to 0, it
    // is removed automatically.
    std::list<gc_header*> pinned_objects;

    // IMPLNOTE: you may wonder why we have this roots/pins dichotomy. This way,
    // we can make root objects without any working_set, and also guarantee that
    // a working_set will never change the root objects list. So pins and roots
    // operate entirely independently.

    // deallocate an object, rendering all references to it meaningless
    void dealloc(gc_header* o);

    // get a list of objects accessible from the given value
    forward_list<gc_header*> accessible(gc_header* o);

    // helper for mark
    void mark_descend(gc_header* o);
    // starting from roots and pins, set the mark on all accessible objects.
    void mark();

    // sweeping iterates over the list of objects doing the following:
    // - remove globally accessible objects from the sweep list
    // - delete unmarked objects
    // - remove the mark from marked objects
    void sweep();

    // recursively mark an object as being global per the rules described in the
    // comment below for designate_global(). This may call mark_weakly_global.
    void mark_global(gc_header* o);
    void mark_weakly_global(gc_header* o);
    void marksweep_weak_globals(gc_header*);

    // pinned objects act as temporary roots
    void pin_object(gc_header* o);
    void unpin_object(gc_header* o);

public:
    allocator(global_env* use_globals);
    ~allocator();

    // TODO: implement in allocator.cpp
    u64 memory_used() const;
    u32 num_objects() const;

    bool gc_is_enabled() const;
    // enable/disable the garbage collector
    void enable_gc();
    void disable_gc();
    // invoke the gc if enough memory is used
    void collect();
    // invoke the gc no matter what
    void force_collect();

    // add a value to the list of root values so it will not be collected
    void add_root_object(gc_header* h);
    // create a root stack managed by this allocator
    root_stack* add_root_stack();
    working_set add_working_set();

    // Designate an object as a global object. This prevents the GC from marking
    // or sweeping this or any object it references. For safety, this should be
    // used only on values not at risk of collection. Behavior depends on value
    // type.
    // - Strings and chunks are marked global and no further action is taken.
    //   (The constant array of a chunk is *not* recursively marked. You have to
    //   do that separately).
    // - For lists, this means recursively marking referenced values as globals.
    // - Since tables and functions can reference mutable values, these mutable
    //   references are treated specially. The objects they refer to are
    //   considered to be "weakly global", and are used as additional root
    //   objects during collections.
    void designate_global(gc_header* o);

    void print_status();
};

}

#endif
