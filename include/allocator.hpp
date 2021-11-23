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

    // set the upvalues of a function object with the given base pointer
    upvalue_cell* get_upvalue(stack_address loc);
    // close all upvalues with stack addresses >= base_addr. (Closing an upvalue
    // involves copying its value to the heap and removing it from the list of
    // upvalues).
    void close(u32 base_addr);

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

    // new_objs are values allocated for this working set (not yet in the gc
    // list)
    forward_list<value> new_objects;
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
    value add_table();
    // Create a function. The caller is responsible for correctly setting
    // upvalues and init values.
    value add_function(function_stub* stub);
    value add_foreign(local_address min_args,
            bool var_args,
            optional<value> (*func)(local_address, value*, virtual_machine*));

    // FIXME: pinning is not thread-safe

    // pin an existing value so it won't be collected for the lifetime of the
    // working_set. Returns v. The value is automagically unpinned if its
    // reference count hits 0.
    value pin_value(value v);
};

struct allocator {
    friend class working_set;
private:
    // note: we guarantee that every pointer in this array is actually memory
    // managed by this garbage collector
    std::list<gc_header*> objects;
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
    dyn_array<value> root_objects;
    // variable-size stacks of root objects. Used for vm stacks.
    std::list<root_stack*> root_stacks;
    // pins are temporary root objects which are added and managed by
    // working_sets. When an object's working_set reference count falls to 0, it
    // is removed automatically.
    std::list<value> pinned_objects;

    // IMPLNOTE: you may wonder why we have this roots/pins dichotomy. This way,
    // we can make root objects without any working_set, and also guarantee that
    // a working_set will never change the root objects list. So pins and roots
    // operate entirely independently.

    // deallocate an object, rendering all references to it meaningless
    void dealloc(gc_header* o);

    // get a list of objects accessible from the given value
    forward_list<gc_header*> accessible(gc_header* o);

    void mark_descend(gc_header* o);

    void mark();
    void sweep();

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
    void add_root_object(value v);
    // create a root stack managed by this allocator
    root_stack* add_root_stack();
    working_set add_working_set();

    // add a chunk with name the specified namespace. (The namespace will be
    // created if it does not already exist).
    code_chunk* add_chunk(symbol_id ns_name);
    // note: this namespace must be in the global environment of this allocator
    // instance.
    code_chunk* add_chunk(fn_namespace* ns);

    void print_status();
};

}

#endif
