#include "allocator.hpp"

#include <algorithm>
#include <iostream>

namespace fn {

static constexpr u32 COLLECT_TH = 4096;

allocator::allocator()
    : objects()
    , gc_enabled(false)
    , to_collect(false)
    , mem_usage(0)
    , collect_threshold(COLLECT_TH)
    , count(0)
    , get_roots([] { return generator<value>(); })
{ }

allocator::allocator(std::function<generator<value>()> get_roots)
    : objects()
    , gc_enabled(false)
    , to_collect(false)
    , mem_usage(0)
    , collect_threshold(COLLECT_TH)
    , count(0)
    , get_roots(get_roots)
{ }

allocator::allocator(generator<value> (*get_roots)())
    : objects()
    , gc_enabled(false)
    , to_collect(false)
    , mem_usage(0)
    , collect_threshold(COLLECT_TH)
    , count(0)
    , get_roots([get_roots] { return get_roots(); })
{ }

allocator::~allocator() {
    for (auto o : objects) {
        dealloc(o->ptr);
    }
}

void allocator::dealloc(value v) {
    if (v.is_cons()) {
        mem_usage -= sizeof(cons);
        delete v.ucons();
    } else if (v.is_str()) {
        auto s = v.ustr();
        mem_usage -= s->len;
        mem_usage -= sizeof(fn_string);
        delete s;
    } else if (v.is_object()) {
        mem_usage -= sizeof(object);
        delete v.uobj();
    } else if (v.is_func()) {
        mem_usage -= sizeof(function);
        delete v.ufunc();
    } else if (v.is_foreign()) {
        mem_usage -= sizeof(foreign_func);
        delete v.uforeign();
    }
    --count;
}

generator<value> allocator::accessible(value v) {
    if (v.is_cons()) {
        return generate1(v.rhead()) + generate1(v.rtail());
    } else if (v.is_object()) {
        generator<value> res;
        for (auto k : v.obj_keys()) {
            res += generate1(k);
            res += generate1(v.get(k));
        }
        return res;
    } else if (v.is_func()) {
        generator<value> res;
        auto f = v.ufunc();
        // just need to add the upvalues
        auto m = f->stub->num_upvals;
        for (local_addr i = 0; i < m; ++i) {
            if (f->upvals[i].ref_count == nullptr) continue;
            res += generate1(**f->upvals[i].val);
        }
        return res;
    }

    return generator<value>();
}

void allocator::mark_descend(obj_header* o) {
    if (o->mark || !o->gc)
        // already been here or not managed
        return;
    o->mark = true;
    for (auto q : accessible(o->ptr)) {
        auto h = q.header();
        if (h.has_value()) {
            mark_descend(*h);
        }
    }
}

void allocator::mark() {
    for (auto v : get_roots()) {
        auto h = v.header();
        if (h.has_value()) {
            mark_descend(*h);
        }
    }
}

void allocator::sweep() {
#ifdef g_c_d_eb_ug
    auto orig_ct = count;
    auto orig_sz = mem_usage;
#endif
    // delete unmarked objects
    std::list<obj_header*> more_objs;
    for (auto h : objects) {
        if (h->mark) {
            more_objs.push_back(h);
        } else {
            dealloc(h->ptr);
        }
    }
    objects.swap(more_objs);
    // unmark remaining objects
    for (auto h : objects) {
        h->mark = false;
    }
#ifdef g_c_d_eb_ug
    auto ct = orig_ct - count;
    auto sz = orig_sz - mem_usage;
    std::cout << "swept " << ct << " objects ( " << sz << " bytes)\n";
#endif
}

bool allocator::gc_is_enabled() const {
    return gc_enabled;
}

void allocator::enable_gc() {
    gc_enabled = true;
    if (to_collect) {
        force_collect();
    }
}

void allocator::disable_gc() {
    gc_enabled = false;
}

void allocator::collect() {
#ifdef g_c_d_eb_ug
    if (gc_enabled) {
        force_collect();
    } else {
        to_collect = true;
    }
#else
    if (mem_usage >= collect_threshold) {
        if (gc_enabled) {
            force_collect();
            // increase the collection threshold in order to guarantee that it's more than twice the
            // current usage. (this is meant to allow the program to grow by spacing out
            // collections). 
            while (2*mem_usage >= collect_threshold) {
                collect_threshold <<= 1;
            }
        } else {
            to_collect = true;
        }
    }
#endif
}

void allocator::force_collect() {
    // note: assume that objects begin unmarked
#ifdef g_c_d_eb_ug
    std::cout << "garbage collection beginning (mem_usage = "
              << mem_usage
              << ", num_objects() = "
              << count
              << " ):\n";
#endif

    mark();
    sweep();
    to_collect = false;
}

value allocator::add_cons(value hd, value tl) {
    collect();
    mem_usage += sizeof(cons);
    ++count;

    auto v = new cons(hd,tl,true);
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_str(const string& s) {
    collect();
    mem_usage += sizeof(fn_string);
    // also add size of the string's payload
    mem_usage += s.size();
    ++count;

    auto v = new fn_string(s, true);
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_str(const char* s) {
    return add_str(string(s));
}

value allocator::add_obj() {
    collect();
    mem_usage += sizeof(object);
    ++count;

    auto v = new object(true);
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_func(func_stub* stub, const std::function<void (upvalue_slot*)>& populate) {
    collect();
    mem_usage += sizeof(function);
    ++count;

    auto v = new function(stub, populate, true);
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_foreign(local_addr min_args, bool var_args, value (*func)(local_addr, value*, virtual_machine*)) {
    collect();
    mem_usage += sizeof(foreign_func);
    ++count;

    auto v = new foreign_func(min_args, var_args, func, true);
    objects.push_front(&v->h);
    return as_value(v);
}

void allocator::print_status() {
    std::cout << "allocator information\n";
    std::cout << "=====================\n";
    std::cout << "memory used (bytes): " << mem_usage << '\n';
    std::cout << "number of objects: " << count << '\n';
    // descend into values
}


}
