#include "allocator.hpp"

#include <algorithm>
#include <iostream>

namespace fn {

static constexpr u32 COLLECT_TH = 4096;

root_stack::root_stack()
    : pointer{0}
    , dead{false} {
}

root_stack::~root_stack() {
    close(0);
}

u32 root_stack::get_pointer() const {
    return pointer;
}

value root_stack::peek(u32 offset) const {
    return contents[pointer - offset - 1];
}

value root_stack::peek_bottom(u32 offset) const {
    return contents[offset];
}

value root_stack::pop() {
    --pointer;
    auto res = contents.back();
    contents.pop_back();
    return res;
}

void root_stack::pop_times(u32 n) {
    pointer -= n;
    contents.resize(pointer);
}

void root_stack::push(value v) {
    ++pointer;
    contents.push_back(v);
}

void root_stack::set(u32 offset, value v) {
    contents[offset] = v;
}

upvalue_cell* root_stack::get_upvalue(stack_address pos) {
    // iterate over the open upvalues
    auto it = upvals.begin();
    while (it != upvals.end()) {
        if ((*it)->pos == pos) {
            // return an existing upvalue
            return *it;
        } else if ((*it)->pos < pos) {
            break;
        }
        ++it;
    }
    // create a new upvalue
    auto cell = new upvalue_cell{pos};
    upvals.insert(it, cell);
    return cell;
}

void root_stack::close(u32 base_addr) {
    for (auto it = upvals.begin(); it != upvals.end(); ) {
        if ((*it)->pos < base_addr) {
            break;
        }

        (*it)->close(contents[(*it)->pos]);
        (*it)->dereference();
        // IMPLNOTE: This if statement prevents a memory leak. If no live
        // functions reference this cell, the garbage collector won't see it.
        if ((*it)->dead()) {
            delete *it;
        }
        it = upvals.erase(it);
    }
    pointer = base_addr;
    contents.resize(base_addr);
}

void root_stack::mark_for_deletion() {
    dead = true;
}

void working_set::add_to_gc() {
    // add new objects to the allocator's list
    for (auto v : new_objects) {
        auto h = v.header();
        if (!h.has_value()) {
            continue;
        }
        alloc->objects.push_front(*h);
    }

    // decrease the reference count for pinned objects. The pinned_objects list
    // is automatically pruned by the allocator.
    for (auto h : pinned_objects) {
        --h->pin_count;
    }
}

working_set::working_set(allocator* use_alloc)
    : released{false}
    , alloc{use_alloc} {
}

working_set::working_set(working_set&& other) noexcept {
    *this = std::move(other);
}

working_set::~working_set() {
    if (!released) {
        add_to_gc();
    }
}

working_set& working_set::operator=(working_set&& other) noexcept {
    this->released = other.released;
    this->alloc = other.alloc;
    this->new_objects = std::move(other.new_objects);
    this->pinned_objects = std::move(other.pinned_objects);
    other.released = true;
    return *this;
}

value working_set::add_cons(value hd, value tl) {
    alloc->collect();
    alloc->mem_usage += sizeof(cons);
    ++alloc->count;
    auto v = as_value(new cons{hd,tl,true});
    new_objects.push_front(v);
    return v;
}

value working_set::add_string(const string& s) {
    alloc->collect();
    alloc->mem_usage += sizeof(fn_string) + s.length();
    ++alloc->count;
    auto v = as_value(new fn_string{s});
    new_objects.push_front(v);
    return v;
}

value working_set::add_table() {
    alloc->collect();
    alloc->mem_usage += sizeof(fn_table);
    ++alloc->count;
    auto v = as_value(new fn_table{});
    new_objects.push_front(v);
    return v;
}

value working_set::add_function(function_stub* stub) {
    alloc->collect();
    alloc->mem_usage += sizeof(function);
    ++alloc->count;
    auto f = new function{stub, true};
    auto v = as_value(f);
    new_objects.push_front(v);
    return v;
}

value working_set::pin_value(value v) {
    auto h = v.header();
    if(h.has_value()) {
        if ((*h)->pin_count++ == 0) {
            // first time this object is pinned
            alloc->pinned_objects.push_front(v);
        }
        pinned_objects.push_front(*h);
    }
    return v;
}

allocator::allocator(global_env* use_globals)
    : globals{use_globals}
    , gc_enabled{false}
    , to_collect{false}
    , mem_usage{0}
    , collect_threshold{COLLECT_TH}
    , count{0} {
}

allocator::~allocator() {
    for (auto o : objects) {
        dealloc(o->ptr);
    }
    for (auto s : root_stacks) {
        delete s;
    }
    auto keys = const_table.keys();
    for (auto k : keys) {
        dealloc(*const_table.get(k));
    }
    for (auto c : chunks) {
        delete c;
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
    } else if (v.is_function()) {
        mem_usage -= sizeof(function);
        auto f = v.ufunction();

        if (f->stub != nullptr) {
            // delete dead upvalues
            for (int i = 0; i < f->stub->num_upvals; ++i) {
                auto cell = f->upvals[i];
                cell->dereference();
                if (cell->dead()) {
                    delete cell;
                }
            }
        }

        delete f;
    }
    --count;
}

vector<value> allocator::accessible(value v) {
    vector<value> res;
    if (v.is_cons()) {
        res.push_back(v_uhead(v));
        res.push_back(v_utail(v));
    } else if (v.is_table()) {
        for (auto k : v_utab_get_keys(v)) {
            res.push_back(k);
            res.push_back(v_utab_get(v, k));
        }
        return res;
    } else if (v.is_function()) {
        auto f = v.ufunction();
        // add the upvalues
        auto m = f->stub->num_upvals;
        for (local_address i = 0; i < m; ++i) {
            auto cell = f->upvals[i];
            if (cell->closed) {
                res.push_back(cell->closed_value);
            }
        }
        auto num_opt = f->stub->pos_params.size() - f->stub->req_args;
        for (u32 i = 0; i < num_opt; ++i) {
            res.push_back(f->init_vals[i]);
        }
    }

    return res;
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
    for (auto v : root_objects) {
        auto h = v.header();
        if (h.has_value()) {
            mark_descend(*h);
        }
    }

    for (auto it = root_stacks.begin(); it != root_stacks.end();) {
        if ((*it)->dead) {
            delete *it;
            it = root_stacks.erase(it);
        } else {
            for (u32 i = 0; i < (*it)->get_pointer(); ++i) {
                auto h = (*it)->contents[i].header();
                if (h.has_value()) {
                    mark_descend(*h);
                }
            }
        }
    }

    for (auto it = pinned_objects.begin(); it != pinned_objects.end();) {
        auto h = it->header();
        if (!h.has_value() || (*h)->pin_count == 0) {
            // remove unreferenced pins and immediate values, (although a
            // non-immediate value should never be here)
            it = pinned_objects.erase(it);
        } else {
            mark_descend(*h);
            ++it;
        }
    }

    // global namespaces
    for (auto k : globals->ns_table.keys()) {
        auto ns = *globals->get_ns(k);
        // iterate over bindings
        for (auto k2 : ns->contents.keys()) {
            auto h = ns->get(k2)->header();
            mark_descend(*h);
        }
    }
}

void allocator::sweep() {
#ifdef GC_DEBUG
    auto orig_ct = count;
    auto orig_sz = mem_usage;
#endif
    // FIXME: this is a doubly-linked list

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

void allocator::add_root_object(value v) {
    root_objects.push_back(v);
}

root_stack* allocator::add_root_stack() {
    auto res = new root_stack{};
    root_stacks.push_front(res);
    return res;
}

working_set allocator::add_working_set() {
    return working_set{this};
}

code_chunk* allocator::add_chunk(symbol_id ns_name) {
    // ensure namespace exists
    auto x = globals->get_ns(ns_name);
    if (!x.has_value()) {
        globals->create_ns(ns_name);
    }
    auto res = new code_chunk{ns_name};
    chunks.push_back(res);
    return res;
}

void allocator::print_status() {
    std::cout << "allocator information\n";
    std::cout << "=====================\n";
    std::cout << "memory used (bytes): " << mem_usage << '\n';
    std::cout << "number of objects: " << count << '\n';
    // descend into values
}

}
