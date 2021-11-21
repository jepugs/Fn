#include "allocator.hpp"

#include <algorithm>
#include <iostream>

// access parts of the gc_header
#define gc_mark(h) (((h).bits & GC_MARK_BIT) == GC_MARK_BIT)
#define gc_ignore(h) (((h).bits & GC_IGNORE_BIT) == GC_IGNORE_BIT)
#define gc_global(h) (((h).bits & GC_GLOBAL_BIT) == GC_GLOBAL_BIT)
#define gc_type(h) (((h).bits & GC_TYPE_BITMASK))

#define gc_set_mark(h) (((h).bits |= GC_MARK_BIT))
#define gc_unset_mark(h) (((h).bits &= ~GC_MARK_BIT))

#define gc_set_ignore(h) (((h).bits |= GC_IGNORE_BIT))
#define gc_unset_ignore(h) (((h).bits &= ~GC_IGNORE_BIT))

#define gc_set_global(h) (((h).bits |= GC_GLOBAL_BIT))
#define gc_unset_global(h) (((h).bits &= ~GC_GLOBAL_BIT))


namespace fn {

static constexpr u32 COLLECT_TH = 4096;

root_stack::root_stack()
    : pointer{0}
    , dead{false}
    , last_pop{V_NIL} {
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
    last_pop = contents.back();
    contents.pop_back();
    return last_pop;
}

void root_stack::pop_times(u32 n) {
    if (n >= 0) {
        pointer -= n;
        last_pop = contents[pointer];
        contents.resize(pointer);
    }
}

value root_stack::get_last_pop() {
    return last_pop;
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
    auto v = as_value(new cons{hd,tl});
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

value working_set::add_string(const fn_string& s) {
    alloc->collect();
    alloc->mem_usage += sizeof(fn_string) + s.len;
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
    auto f = new function{stub};
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
        dealloc(o);
    }
    for (auto s : root_stacks) {
        delete s;
    }
}

void allocator::dealloc(gc_header* o) {
    switch (gc_type(*o)) {
    case GC_TYPE_CHUNK:
        // TODO: maybe track chunk mem_usage
        free_code_chunk((code_chunk*) o);
        break;
    case GC_TYPE_STRING:
        mem_usage -= ((fn_string*)o)->len;
        mem_usage -= sizeof(fn_string);
        delete (fn_string*)o;
        break;
    case GC_TYPE_CONS:
        mem_usage -= sizeof(cons);
        delete (cons*)o;
        break;
    case GC_TYPE_TABLE:
        mem_usage -= sizeof(fn_table);
        delete (fn_table*)o;
        break;
    case GC_TYPE_FUNCTION:
        mem_usage -= sizeof(function);
        {
            auto f = (function*)o;

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
        break;
    }
    --count;
}

// add a value's header to dest if it has one
static void add_value_header(value v, forward_list<gc_header*>& dest) {
    auto x = v.header();
    if (x.has_value()) {
        dest.push_front(*x);
    }
}

forward_list<gc_header*> allocator::accessible(gc_header* o) {
    forward_list<gc_header*> res;
    switch (gc_type(*o)) {
    case GC_TYPE_CHUNK:
        {
            auto x = (code_chunk*)o;
            for (auto i = 0; i < x->num_constants; ++i) {
                add_value_header(x->constant_table[i], res);
            }
        }
    case GC_TYPE_CONS:
        add_value_header(((cons*)o)->head, res);
        add_value_header(((cons*)o)->tail, res);
        break;
    case GC_TYPE_TABLE:
        {
            auto x = (fn_table*)o;
            for (auto k : x->contents.keys()) {
                add_value_header(k, res);
                add_value_header(*x->contents.get(k), res);
            }
        }
        break;
    case GC_TYPE_FUNCTION:
        {
            auto f = (function*)o;
            auto m = f->stub->num_upvals;
            for (local_address i = 0; i < m; ++i) {
                auto cell = f->upvals[i];
                if (cell->closed) {
                    add_value_header(cell->closed_value, res);
                }
            }
            auto num_opt = f->stub->pos_params.size() - f->stub->req_args;
            for (u32 i = 0; i < num_opt; ++i) {
                add_value_header(f->init_vals[i], res);
            }
        }
        break;
    }

    return res;
}

void allocator::mark_descend(gc_header* o) {
    if (gc_mark(*o)) {
        // already been here or not managed
        return;
    }
    gc_set_mark(*o);
    for (auto h : accessible(o)) {
        mark_descend(h);
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
            auto h = (*it)->last_pop.header();
            if (h.has_value()) {
                mark_descend(*h);
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
        for (auto k2 : ns->names()) {
            auto h = ns->get(k2)->header();
            mark_descend(*h);
        }
        for (auto k2 : ns->macro_names()) {
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
    std::list<gc_header*> more_objs;
    for (auto h : objects) {
        if (gc_mark(*h)) {
            more_objs.push_back(h);
        } else {
            dealloc(h);
        }
    }
    objects.swap(more_objs);
    // unmark remaining objects
    for (auto h : objects) {
        gc_unset_mark(*h) = false;
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

code_chunk* allocator::add_chunk(symbol_id ns_id) {
    // ensure namespace exists
    auto x = globals->get_ns(ns_id);
    if (!x.has_value()) {
        globals->create_ns(ns_id);
    }
    auto res = mk_code_chunk(ns_id);
    objects.push_back((gc_header*)res);
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
