#include "allocator.hpp"

#include <algorithm>
#include <iostream>

//#define GC_DEBUG
//#define GC_VERBOSE

namespace fn {

// 64KiB for first collect. Actual usage will be a somewhat higher because
// tables/functions/chunks create some extra data depending on
// entries/upvalues/code
static constexpr u32 FIRST_COLLECT = 64 * 1024;
static constexpr f64 COLLECT_SCALE_FACTOR = 2;
// This essentially says no more than this proportion of available memory may be
// devoted to persistent objects
static constexpr f64 RESCALE_TH = 0.7;


allocator::allocator(istate* S)
    : mem_usage{0}
    , count{0}
    , collect_threshold{FIRST_COLLECT}
    , S{S} {
}

allocator::~allocator() {
    for (auto h = objs_head; h != nullptr;) {
        auto tmp = h->next;
        dealloc(h);
        h = tmp;
    }
}

void allocator::dealloc(gc_header* o) {
    switch (gc_type(*o)) {
    case GC_TYPE_STRING:
        mem_usage -= ((fn_string*)o)->size;
        mem_usage -= sizeof(fn_string);
        free(((fn_string*)o)->data);
        delete (fn_string*)o;
        break;
    case GC_TYPE_CONS:
        mem_usage -= sizeof(fn_cons);
        free(o);
        break;
    case GC_TYPE_TABLE:
        mem_usage -= sizeof(fn_table);
        delete (fn_table*)o;
        break;
    case GC_TYPE_FUNCTION: {
        mem_usage -= sizeof(fn_function);
        auto f = (fn_function*)o;

        delete[] f->init_vals;
        delete[] f->upvals;
        delete f;
    }
        break;
    case GC_TYPE_UPVALUE: {
        delete (upvalue_cell*)o;
    }
    }
    --count;
}


static void add_value_header(value v,
        std::forward_list<gc_header*>* to_list) {
    if (vhas_header(v)) {
        to_list->push_front(vheader(v));
    }
}

void add_accessible(gc_header* o, std::forward_list<gc_header*>* to_list) {
    switch (gc_type(*o)) {
    case GC_TYPE_CONS:
        add_value_header(((fn_cons*)o)->head, to_list);
        add_value_header(((fn_cons*)o)->tail, to_list);
        break;
    case GC_TYPE_TABLE: {
        add_value_header(((fn_table*)o)->metatable, to_list);
        for (auto entry : ((fn_table*)o)->contents) {
            add_value_header(entry->key, to_list);
            add_value_header(entry->val, to_list);
        }
    }
        break;
    case GC_TYPE_FUNCTION: {
        auto f = (fn_function*)o;
        // upvalues
        for (local_address i = 0; i < f->num_upvals; ++i) {
            auto cell = f->upvals[i];
            to_list->push_front(&cell->h);
        }
        for (u32 i = 0; i < f->stub->num_opt; ++i) {
            add_value_header(f->init_vals[i], to_list);
        }
        to_list->push_front(&f->stub->h);
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto s = (function_stub*)o;
        for (auto x : s->sub_funs) {
            to_list->push_front(&x->h);
        }
        for (auto x : s->const_arr) {
            add_value_header(x, to_list);
        }
    }
        break;
    case GC_TYPE_UPVALUE: {
        auto u = (upvalue_cell*)o;
        if(u->closed) {
            add_value_header(u->datum.val, to_list);
            // open upvalues should be visible from the stack
        }
    }
        break;
    }
}

void allocator::sweep() {
#ifdef GC_LOG
    auto orig_ct = count;
    auto orig_sz = mem_usage;
#endif

    auto prev_ptr = &objs_head;
    for (auto h = objs_head; h != nullptr;) {
        if (gc_mark(*h)) {
            gc_unset_mark(*h);
            prev_ptr = &h->next;
            h = h->next;
        } else {
            *prev_ptr = h->next;
            dealloc(h);
            h = *prev_ptr;
        }
    }

#ifdef GC_LOG
    auto ct = orig_ct - count;
    auto sz = orig_sz - mem_usage;
    std::cout << "swept " << ct << " objects ( " << sz << " bytes)\n";
#endif
}

// fn_string* allocator::alloc_string(u32 n) {
//     auto res = new fn_string;
//     init_gc_header(&res->h, GC_TYPE_STRING);
//     res->size = n;
//     res->data = (u8*)malloc(n + 1);
//     return res;
// }

// fn_string* allocator::alloc_string(const string& str) {
//     auto res = alloc_string(str.size());
//     memcpy(res->data, str.c_str(), str.size() + 1);
//     return res;
// }

// fn_cons* allocator::alloc_cons(value hd, value tl) {
//     auto res = (fn_cons*) malloc(sizeof(fn_cons));
//     init_gc_header(&res->h, GC_TYPE_CONS);
//     res->head = hd;
//     res->tail = tl;
//     return res;
// }

// fn_function* allocator::alloc_function(function_stub* stub) {
//     auto res = new fn_function;
//     init_gc_header(&res->h, GC_TYPE_FUNCTION);
//     res->stub = stub;
//     auto num_opt = stub->pos_params.size - stub->req_args;
//     res->init_vals = new value[num_opt];
//     res->num_upvals = stub->num_upvals;
//     res->upvals = new upvalue_cell*[stub->num_upvals];
//     return res;
// }

// fn_table* allocator::alloc_table() {
//     auto res = new fn_table;
//     init_gc_header(&res->h, GC_TYPE_TABLE);
//     res->metatable = V_NIL;
//     return res;
// }


// void allocator::add_cons(fn_cons* ptr) {
//     add_to_obj_list((gc_header*)ptr);
//     collect();
//     mem_usage += sizeof(fn_cons);
//     ++count;
// }

// void allocator::add_string(fn_string* ptr) {
//     add_to_obj_list((gc_header*)ptr);
//     collect();
//     // FIXME: count string size here
//     mem_usage += sizeof(fn_string);
//     ++count;
// }

// void allocator::add_table(fn_table* ptr) {
//     add_to_obj_list((gc_header*)ptr);
//     collect();
//     // FIXME: count table size here
//     mem_usage += sizeof(fn_table);
//     ++count;
// }

// void allocator::add_function(fn_function* ptr) {
//     add_to_obj_list((gc_header*)ptr);
//     collect();
//     // FIXME: count function size here
//     mem_usage += sizeof(fn_function);
//     ++count;
// }

// void allocator::add_to_obj_list(gc_header* h) {
//     h->next = objs_head;
//     objs_head = h;
// }

void allocator::collect() {
#ifdef GC_DEBUG
    force_collect();
#else
    if (mem_usage >= collect_threshold) {
        force_collect();
        if (mem_usage >= RESCALE_TH * collect_threshold) {
            collect_threshold *= COLLECT_SCALE_FACTOR;
        }
    }
#endif
}

void allocator::force_collect() {
    // note: assume that objects begin unmarked
#ifdef GC_VERBOSE
    std::cout << "garbage collection beginning (mem_usage = "
              << mem_usage
              << ", num_objects() = "
              << count
              << " ):\n";
#endif

    mark();
    sweep();
#ifdef GC_VERBOSE
    std::cout << "Post collection (mem_usage ="
              << mem_usage
              << ", num_objects() = "
              << count
              << " ):\n";
#endif
}

void allocator::print_status() {
    std::cout << "allocator information\n";
    std::cout << "=====================\n";
    std::cout << "memory used (bytes): " << mem_usage << '\n';
    std::cout << "number of objects: " << count << '\n';
    // descend into values
}

}
