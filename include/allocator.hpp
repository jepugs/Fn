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

// FIXME: these macros should probably be in a header, but not this one (so as
// to not pollute the macro namespace)

// access parts of the gc_header
#define gc_mark(h) (((h).bits & GC_MARK_BIT) == GC_MARK_BIT)
#define gc_global(h) (((h).bits & GC_GLOBAL_BIT) == GC_GLOBAL_BIT)
#define gc_type(h) (((h).bits & GC_TYPE_BITMASK))

#define gc_set_mark(h) \
    ((h).bits = ((h).bits | GC_MARK_BIT))
#define gc_unset_mark(h) (((h).bits = (h).bits & ~GC_MARK_BIT))

#define gc_set_global(h) ((h).bits = (h).bits | GC_GLOBAL_BIT)
#define gc_unset_global(h) (((h).bits &= ~GC_GLOBAL_BIT))


namespace fn {

struct allocator;

// These are reference counted roots. When the reference count hits 0, they are
// removed from the root objects list.
struct pinned_object {
    // If this is set to false, this root object will be destroyed and removed
    // on the next collection
    bool alive = true;

    gc_header* obj;

    // note that this will not increment the pin count on its own
    pinned_object(gc_header* obj);
    ~pinned_object();
    // void mark(std::function<void(gc_header*)> descend);
};


// A root_stack is a stack of values treated as gc roots. These are used for the
// virtual machine stack. This does not inherit from gc_root because it started
// out this way and I'm worried about decreased performance due to virtual
// method dispatch and the fact that the descend() call can't be inlined.
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
    // mark_for_deletion() will not work properly.
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
    // close for tail call. This is like close, but the top n elements of the
    // stack (which must all reside above the base pointer) are pushed back to
    // the new call frame.
    void close_for_tc(stack_address n, stack_address base_addr);
    // like close(), but first saves the top of the stack and pushes it back
    // after doing the close (so the final stack size is base_addr+1). This sets
    // last_pop to the return value.
    void do_return(u32 base_addr);

    value& operator[](u32 offset) {
        return contents[offset];
    }

    void kill();
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

    // pins are temporary root objects which are added and managed by
    // working_sets. When an object's working_set reference count falls to 0, it
    // is removed automatically.
    void pin(gc_header* gc);
    // pin an existing value so it won't be collected for the lifetime of the
    // working_set. Returns v. The value is automagically unpinned if its
    // reference count hits 0.
    value pin_value(value v);
};

class allocator {
    friend class working_set;
    friend class code_chunk;
private:
    // note: we guarantee that every pointer in this array is actually memory
    // managed by this garbage collector
    std::list<gc_header*> objects;
    // globals are removed from the objects array
    std::list<gc_header*> global_objects;
    // list of global objects with mutable values. These mutable cells are
    // used as gc roots
    std::list<gc_header*> mutable_globals;
    // list of objects to mark next. This is populate based on root objects.
    std::forward_list<gc_header*> marking_list;

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
    std::list<gc_header*> roots;
    // variable-size stacks of root objects. Used for vm stacks.
    std::list<root_stack*> root_stacks;

    // deallocate an object, rendering all references to it meaningless
    void dealloc(gc_header* o);

    // get a list of objects accessible from the given value
    forward_list<gc_header*> accessible(gc_header* o);

    // helpers for mark
    //void mark_descend_value(value v);
    void add_mark_value(value v);
    // this marks an object and adds accessible nodes to the marking_list.
    void mark_descend(gc_header* o);
    // starting from roots and pins, set the mark on all accessible objects.
    void mark();

    // sweeping iterates over the list of objects doing the following:
    // - remove globally accessible objects from the sweep list
    // - delete unmarked objects
    // - remove the mark from marked objects
    void sweep();

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
    void add_gc_root(gc_header* r);
    // create a root stack managed by this allocator
    root_stack* add_root_stack();
    working_set add_working_set();

    // Designate an object as global by setting the global bit and pinning it.
    // Right now this does little other than guarantee the object's safety. In
    // the future I'm planning to add a feature to freeze globally accessible
    // values.
    void designate_global(gc_header* o);
    // Once implemented, this will allow globals to be 
    void unset_global(gc_header* o);

    void print_status();
};

}

#endif
