#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

// Defining this variable (which should be done via CMAKE) causes the garbage
// collector to run frequently and often emit output.
//#define GC_DEBUG

#include "base.hpp"
#include "bytes.hpp"
#include "namespace.hpp"
#include "values.hpp"


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
};

// NOTE: we require that T occupies at least as much space as void*. This is not
// a problem for our purposes. Constructors/destructors are not invoked. You
// gotta use placement new for construction and then manually call the
// destructors.
template<typename T>
class object_pool {
private:
    u32 block_size = 256;
    // the beginning of the block holds a pointer to the next block, so e.g. a
    // block size of 256 has total size 257*sizeof(obj)
    T* first_block;

    // pointer to the next free object location. By casting between T* and void*
    // we can actually embed the free list into the block directly. (This is why
    // we require that sizeof(T) >= sizeof(void*)
    T* first_free;

    // Allocate another block for the pool. This works perfectly fine, but it's
    // absolutely vile.
    T* new_block() {
        auto res = (T*)malloc((1 + block_size)*sizeof(T));
        ((void**)res)[0] = nullptr;

        auto objs = &res[1];
        for (u32 i = 0; i < block_size-1; ++i) {
            // store the pointer to objs[i+1] in objs[i]
            *(void**)(&objs[i]) = &objs[i + 1];
        }
        // look at those casts. C++ is screaming at me not to do this
        *(void**)(&objs[block_size - 1]) = nullptr;
        return res;
    }

public:
    object_pool()
        : first_free{nullptr} {
        static_assert(sizeof(T) >= sizeof(void*));
        first_block = new_block();
        first_free = &first_block[1];
    }
    ~object_pool() {
        while (first_block != nullptr) {
            auto next = ((T**)first_block)[0];
            free(first_block);
            first_block = next;
        }
    }

    // get a new object. THIS DOES NOT INVOKE new; you must use placement new on
    // the returned pointer.
    T* new_object() {
        if (first_free == nullptr) {
            auto tmp = first_block;
            first_block = new_block();
            ((T**)first_block)[0] = tmp;
            first_free = &first_block[1];
        }
        auto res = first_free;
        // *first_free is actually a pointer to the next free position
        first_free = *((T**)first_free);
        return res;
    }
    // free an object within the pool. THIS DOES NOT INVOKE THE DESTRUCTOR. You
    // must do that yourself.
    void free_object(T* obj) {
        auto tmp = first_free;
        first_free = obj;
        *(T**)obj = tmp;
    }
};

// A root_stack is a stack of values treated as gc roots. These are used for the
// virtual machine stack. This does not inherit from gc_root because it started
// out this way and I'm worried about decreased performance due to virtual
// method dispatch and the fact that the descend() call can't be inlined.
class root_stack {
    friend class allocator;
private:
    // only the allocator is allowed to construct these
    root_stack();
    allocator* alloc;
    u32 pointer;
    dyn_array<value> contents;
    dyn_array<function*> callee_stack;
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
    void set(stack_address offset, value v);
    // set a value, indexed counting backward from the top of the stack
    void set_from_top(stack_address offset, value v);


    // Value creation on the stack

    // These functions are used to create values directly on the stack so they
    // are visible to the GC. The push_ functions extend the stack one position,
    // whereas the set_ functions replace an existing stack value, indexed so 0
    // is the bottom.

    // strings
    value push_string(const string& str);
    value make_string(stack_address place, const string& str);

    // lists
    value push_cons(value hd, value tl);
    value make_cons(stack_address place, value hd, value tl);
    // replace the top element of the stack with a list consisting of the top n
    // elements ordered from bottom to top. n must be greater than 0 and less
    // than pointer.
    void top_to_list(u32 n);

    // tables
    value push_table();
    value make_table(stack_address place);

    // Create a function on top of the stack. Uses the given function stub and
    // base pointer (needed to initialize the function). If the function
    // requires any initial values, they will be popped right off the stack, so
    // make sure they're there, ok? This also sets up the upvalues using the top
    // of the callee stack. We do all of this in one step inside the root stack
    // class so that the object will become visible to the GC as soon as it's
    // fully initialized, and no sooner.
    value create_function(function_stub* func, stack_address bp);

    // root_stack also maintains a record of the functions in the call stack.
    // This is so that the function can safely be popped off of the main call
    // stack. The function on top of the stack is also used for setting upvalues
    // of newly created functions.
    void push_callee(function* callee);
    void pop_callee();
    function* peek_callee();

    // set the upvalues of a function object with the given base pointer
    upvalue_cell* get_upvalue(stack_address loc);
    // close all upvalues with stack addresses >= base_addr. (Closing an upvalue
    // involves copying its value to the heap and removing it from the list of
    // upvalues). This does not change last_pop.
    void close_upvalues(u32 base_addr);
    // like close_upvalues, but also rolls pointer back to base_addr
    void close(u32 base_addr);
    // close for tail call. This is like close, but the top n elements of the
    // stack (which must all reside above the base pointer) are pushed back so
    // they start at base_addr (preserving their order on the stack)
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
    friend class root_stack;
    friend class code_chunk;
private:
    gc_header* first_obj=nullptr;
    // list of objects to mark next. This is populated based on root objects.
    // FIXME: this could be replaced by a local variable in mark()
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

    // pool allocators. These seem to be slightly faster than using malloc/new
    // for everything, but I'm not really ok with how they work.
    object_pool<cons> cons_allocator;
    object_pool<function> function_allocator;
    object_pool<fn_table> table_allocator;

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

    // allocate objects using the GC's internal facilities. You have to run
    // placement new on these to construct them.
    cons* alloc_new_cons();
    function* alloc_new_function();
    fn_table* alloc_new_table();

    // add (already allocated) objects to the GC list. These must be already be
    // accessible to the GC, or they will be swept
    void add_string(fn_string* ptr);
    void add_cons(cons* ptr);
    void add_table(fn_table* ptr);
    void add_function(function* ptr);
    void add_chunk(code_chunk* ptr);

    void add_to_obj_list(gc_header* h);

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

    // pinning an object increments its pin count, which starts at 0. When
    // positive, the object is used as a root for the GC mark-and-sweep
    // algorithm.
    void pin_object(gc_header* o);
    // decrement an object's pin count
    void unpin_object(gc_header* o);

    void print_status();
};

}

#endif
