#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

// Defining this variable (which should be done via CMAKE) causes the garbage
// collector to run frequently and often emit output.
// #define GC_DEBUG

#include "base.hpp"
#include "parse.hpp"
#include "values.hpp"

#include <functional>
#include <list>

namespace fn {


class allocator {
private:
    // note: we guarantee that every pointer in this array is actually memory managed by this
    // garbage collector
    std::list<obj_header*> objects;
    // separate list of constant objects
    table<value,value> const_table;
    // flag used to determine garbage collector behavior. starts out false to allow initialization
    bool gc_enabled;
    // if true, garbage collection will automatically run when next enabled
    bool to_collect;
    u64 mem_usage;
    // gc is invoked when mem_usage > collect_threshold. collect_threshold is increased if mem_usage >
    // 0.5*collect_threshold after a collection.
    u64 collect_threshold;
    // number of objects
    u32 count;

    std::function<vector<value>()> get_roots;

    void dealloc(value v);

    // get a list of objects accessible from the given value
    //forward_list<obj_header*> accessible(value v);
    vector<value> accessible(value v);

    void mark_descend(obj_header* o);

    void mark();
    void sweep();

public:
    allocator();
    allocator(std::function<vector<value>()> get_roots);
    allocator(vector<value> (*get_roots)());
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

    value add_cons(value hd, value tl);
    value add_string(const string& s);
    value add_string(const char* s);
    value add_table();
    value add_func(func_stub* stub,
                   const std::function<void (upvalue_slot*,value*)>& populate);
    value add_foreign(local_addr min_args,
                      bool var_args,
                      value (*func)(local_addr, value*, virtual_machine*));
    value add_namespace();

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
