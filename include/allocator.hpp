#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

// Defining this variable (which should be done via CMAKE) causes the garbage
// collector to run frequently and often emit output.
// #define GC_DEBUG

#include "base.hpp"
#include "parse.hpp"
#include "values.hpp"

#include <list>

namespace fn {

struct allocator;

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
    forward_list<obj_header*> pinned_objects;

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
    value add_table();
    value add_function(function_stub* stub,
                       const std::function<void (upvalue_slot*,value*)>& populate);
    value add_foreign(local_addr min_args,
                      bool var_args,
                      optional<value> (*func)(local_addr, value*, virtual_machine*));
    value add_namespace();

    // FIXME: pinning is not thread-safe

    // pin an existing value so it won't be collected for the lifetime of the
    // working_set. Returns v. The value is automagically unpinned if its
    // reference count hits 0.
    value pin_value(value v);
};


struct root_stack {
    friend class allocator;
private:
    // only the allocator is allowed to make these
    root_stack(u32 size);
    u32 size;
    u16 pointer;
    vector<value> contents;
    bool dead; // if true, stack will be deleted when garbage is collected

public:
    // no copying! These cannot be moved around in memory or else
    // mark_for_deletion will not work properly.
    root_stack(const root_stack& other) = delete;
    root_stack& operator=(const root_stack& other) = delete;
    root_stack(root_stack&& other) = delete;
    root_stack& operator=(root_stack&& other) = delete;

    u16 get_pointer() const;

    value peek(stack_addr offset=0) const;
    value peek_bottom(stack_addr offset=0) const;
    value pop();
    void push(value v);
    void clear();

    void mark_for_deletion();
};


struct allocator {
    friend class working_set;
private:
    // note: we guarantee that every pointer in this array is actually memory
    // managed by this garbage collector
    std::list<obj_header*> objects;
    // separate list of constant objects
    table<value,value> const_table;
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
    vector<value> root_objects;
    // variable-size stacks of root objects. Used for vm stacks.
    std::list<root_stack*> root_stacks;
    // pins are temporary root objects which are added and managed by
    // working_sets. When an object's working_set reference count falls to 0 it
    // is removed automatically.
    std::list<value> pinned_objects;

    // IMPLNOTE: you may wonder why we have this roots/pins dichotomy. This way,
    // we can make root objects without any working_set, and also guarantee that
    // a working_set will never change the root objects list. So pins and roots
    // operate entirely independently.

    // deallocate an object, rendering all references to it meaningless
    void dealloc(value v);

    // get a list of objects accessible from the given value
    //forward_list<obj_header*> accessible(value v);
    vector<value> accessible(value v);

    void mark_descend(obj_header* o);

    void mark();
    void sweep();

    value add_cons(value hd, value tl);
    value add_string(const string& s);
    value add_string(const char* s);
    value add_table();
    value add_function(function_stub* stub,
                       const std::function<void (upvalue_slot*,value*)>& populate);
    value add_foreign(local_addr min_args,
                      bool var_args,
                      optional<value> (*func)(local_addr, value*, virtual_machine*));
    value add_namespace();

public:
    allocator();
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
    root_stack* add_root_stack(u32 size);
    working_set add_working_set();

    // FIXME: remove these and use working_set with appropriate lifetime instead
    // Note: values passed to const_cons must be constant or they will be
    // deallocated prematurely (bad)
    value const_cons(value hd, value tl);
    value const_string(const char* s);
    value const_string(const string& s);
    value const_string(const fn_string& s);

    // convert an ast_node object to a quoted form
    value const_quote(const fn_parse::ast_node* node);

    void print_status();
};

}

#endif
