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
    , get_roots([] { return generator<value>(); }) {
}

allocator::allocator(std::function<generator<value>()> get_roots)
    : objects()
    , gc_enabled(false)
    , to_collect(false)
    , mem_usage(0)
    , collect_threshold(COLLECT_TH)
    , count(0)
    , get_roots(get_roots) {
}

allocator::allocator(generator<value> (*get_roots)())
    : objects()
    , gc_enabled(false)
    , to_collect(false)
    , mem_usage(0)
    , collect_threshold(COLLECT_TH)
    , count(0)
    , get_roots([get_roots] { return get_roots(); }) {
}

allocator::~allocator() {
    for (auto o : objects) {
        dealloc(o->ptr);
    }
    auto keys = const_table.keys();
    for (auto k : keys) {
        dealloc(**const_table.get(*k));
    }
}

void allocator::dealloc(value v) {
    if (v.is_cons()) {
        mem_usage -= sizeof(cons);
        delete v.ucons();
    } else if (v.is_string()) {
        auto s = v.ustring();
        mem_usage -= s->len;
        mem_usage -= sizeof(fn_string);
        delete s;
    } else if (v.is_table()) {
        mem_usage -= sizeof(fn_table);
        delete v.utable();
    } else if (v.is_func()) {
        mem_usage -= sizeof(function);
        delete v.ufunc();
    } else if (v.is_foreign()) {
        mem_usage -= sizeof(foreign_func);
        delete v.uforeign();
    } else if (v.is_namespace()) {
        mem_usage -= sizeof(fn_namespace);
        delete v.unamespace();
    }
    --count;
}

generator<value> allocator::accessible(value v) {
    if (v.is_cons()) {
        return generate1(v.rhead()) + generate1(v.rtail());
    } else if (v.is_table()) {
        generator<value> res;
        for (auto k : v.table_keys()) {
            res += generate1(k);
            res += generate1(v.table_get(k));
        }
        return res;
    } else if (v.is_func()) {
        generator<value> res;
        auto f = v.ufunc();
        // add the upvalues
        auto m = f->stub->num_upvals;
        for (local_addr i = 0; i < m; ++i) {
            if (f->upvals[i].ref_count == nullptr) continue;
            res += generate1(**f->upvals[i].val);
        }
        auto num_opt = f->stub->positional.size() - f->stub->optional_index;
        for (u32 i = 0; i < num_opt; ++i) {
            res += generate1(f->init_vals[i]);
        }

        return res;
    } else if (v.is_namespace()) {
        generator<value> res;
        int ct = 0;
        for (auto sym : v.namespace_names()) {
            res += generate1(*(v.namespace_get(sym)));
            ++ct;
        }
        return res;
    }

    return generator<value>();
}

void allocator::mark_descend(obj_header* o) {
    if (o->mark || !o->gc) {
        // already been here or not managed
        return;
    }
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
#ifdef GC_DEBUG
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
#ifdef GC_DEBUG
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
#ifdef GC_DEBUG
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
#ifdef GC_DEBUG
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

    auto v = new cons{hd,tl,true};
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_string(const string& s) {
    collect();
    mem_usage += sizeof(fn_string);
    // also add size of the string's payload
    mem_usage += s.size();
    ++count;

    auto v = new fn_string{s, true};
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_string(const char* s) {
    return add_string(string{s});
}

value allocator::add_table() {
    collect();
    mem_usage += sizeof(fn_table);
    ++count;

    auto v = new fn_table{true};
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_func(func_stub* stub,
                          const std::function<void (upvalue_slot*,value*)>& populate) {
    collect();
    mem_usage += sizeof(function);
    ++count;

    auto v = new function{stub, populate, true};
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_foreign(local_addr min_args,
                             bool var_args,
                             value (*func)(local_addr, value*, virtual_machine*)) {
    collect();
    mem_usage += sizeof(foreign_func);
    ++count;

    auto v = new foreign_func{min_args, var_args, func, true};
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::add_namespace() {
    collect();
    mem_usage += sizeof(fn_namespace);
    ++count;

    auto v = new fn_namespace{true};
    objects.push_front(&v->h);
    return as_value(v);
}

value allocator::const_cons(value hd, value tl) {
    mem_usage += sizeof(cons);
    ++count;

    auto v = as_value(new cons{hd,tl,false});
    auto x = const_table.get(v);
    if (x.has_value()) {
        dealloc(v);
        return **x;
    } else {
        const_table.insert(v, v);
        return v;
    }
}

value allocator::const_string(const string& s) {
    mem_usage += sizeof(fn_string);
    // also add size of the string's payload
    mem_usage += s.size();
    ++count;

    auto v = as_value(new fn_string{s, false});
    auto x = const_table.get(v);
    if (x.has_value()) {
        dealloc(v);
        return **x;
    } else {
        const_table.insert(v, v);
        return v;
    }
}

value allocator::const_string(const char* s) {
    return const_string(string{s});
}

value allocator::const_string(const fn_string& s) {
    return const_string(s.data);
}

value allocator::const_quote(const fn_parse::ast_node* node) {
    if (node->kind == fn_parse::ak_atom) {
        auto& atom = *node->datum.atom;
        switch (atom.type) {
        case fn_parse::at_number:
            return as_value(atom.datum.num);
        case fn_parse::at_symbol:
            return as_sym_value(atom.datum.sym);
        case fn_parse::at_string:
            return const_string(*atom.datum.str);
        default:
            // FIXME: maybe should raise an exception?
            return V_NULL;
        }
    } else if (node->kind == fn_parse::ak_list) {
        auto tl = V_EMPTY;
        for (i32 i = node->datum.list->size() - 1; i >= 0; --i) {
            auto hd = const_quote(node->datum.list->at(i));
            tl = const_cons(hd, tl);
        }
        return tl;
    } else {
        // FIXME: probably should be an error
        return V_NULL;
    }
}


void allocator::print_status() {
    std::cout << "allocator information\n";
    std::cout << "=====================\n";
    std::cout << "memory used (bytes): " << mem_usage << '\n';
    std::cout << "number of objects: " << count << '\n';
    // descend into values
}


}
