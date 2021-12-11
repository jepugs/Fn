#include "allocator.hpp"

#include <algorithm>
#include <iostream>

// #define GC_DEBUG

namespace fn {

// 64KiB for first collect. Actual usage will be a somewhat higher because
// tables/functions/chunks create some extra data depending on
// entries/upvalues/code
static constexpr u32 FIRST_COLLECT = 64 * 1024;
static constexpr f64 COLLECT_SCALE_FACTOR = 2;
// This essentially says no more than this proportion of available memory may be
// devoted to persistent objects
static constexpr f64 RESCALE_TH = 0.7;


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
    last_pop = contents[contents.size-1];
    contents.resize(contents.size-1);
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

void root_stack::push_function(function* callee) {
    function_stack.push_back(callee);
}

void root_stack::pop_function() {
    function_stack.resize(function_stack.size-1);
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
    // Warning: don't do any stack operations here so last_pop won't be
    // affected. (Otherwise do_return will break).
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

void root_stack::do_return(u32 base_addr) {
    pop();
    // NOTE! No stack operations can happen here, or last pop will get messed
    // up.
    close(base_addr);
    push(last_pop);
    pop_function();
}

void root_stack::kill() {
    dead = true;
}

void working_set::add_to_gc() {
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
    auto tmpr = released;
    released = other.released;
    other.released = tmpr;

    auto tmpa = other.alloc;
    alloc = other.alloc;
    other.alloc = tmpa;

    pinned_objects = std::move(other.pinned_objects);

    return *this;
}

value working_set::add_cons(value hd, value tl) {
    alloc->collect();
    alloc->mem_usage += sizeof(cons);
    ++alloc->count;
    auto ptr = new cons{hd,tl};
    auto v = vbox_cons(ptr);
    pin(&ptr->h);
    alloc->objects.push_back((gc_header*)ptr);
    return v;
}

value working_set::add_string(const string& s) {
    alloc->collect();
    // +1 for null terminator
    alloc->mem_usage += sizeof(fn_string) + s.length() + 1;
    ++alloc->count;
    auto ptr = new fn_string{s};
    pin(&ptr->h);
    alloc->objects.push_back((gc_header*)ptr);
    return vbox_string(ptr);
}

value working_set::add_string(const fn_string& s) {
    alloc->collect();
    alloc->mem_usage += sizeof(fn_string) + s.len + 1;
    ++alloc->count;
    auto ptr = new fn_string{s};
    auto v = vbox_string(ptr);
    pin(&ptr->h);
    alloc->objects.push_back((gc_header*)ptr);
    return v;
}

value working_set::add_string(u32 len) {
    alloc->collect();
    alloc->mem_usage += sizeof(fn_string) + len + 1;
    ++alloc->count;
    auto ptr = new fn_string{len};
    auto v = vbox_string(ptr);
    pin(&ptr->h);
    alloc->objects.push_back((gc_header*)ptr);
    return v;
}

value working_set::add_table() {
    alloc->collect();
    alloc->mem_usage += sizeof(fn_table);
    ++alloc->count;
    auto ptr = new fn_table{};
    auto v = vbox_table(ptr);
    pin(&ptr->h);
    alloc->objects.push_back((gc_header*)ptr);
    return v;
}

value working_set::add_function(function_stub* stub) {
    alloc->collect();
    alloc->mem_usage += sizeof(function)
        + stub->num_upvals*sizeof(upvalue_cell)
        + (stub->pos_params.size - stub->req_args)*sizeof(value);
    ++alloc->count;
    auto ptr = new function{stub};
    auto v = vbox_function(ptr);
    pin(&ptr->h);
    alloc->objects.push_back((gc_header*)ptr);
    return v;
}

code_chunk* working_set::add_chunk(symbol_id ns_id) {
    alloc->collect();
    alloc->mem_usage += sizeof(code_chunk);
    ++alloc->count;

    // ensure namespace exists
    auto x = alloc->globals->get_ns(ns_id);
    if (!x.has_value()) {
        alloc->globals->create_ns(ns_id);
    }

    auto res = mk_code_chunk(alloc, ns_id);

    auto h = (gc_header*)res;
    pin(h);
    alloc->objects.push_front(h);
    return res;
}

value working_set::pin_value(value v) {
    if(vhas_header(v)) {
        pin(vheader(v));
    }
    return v;
}

void working_set::pin(gc_header* h) {
    ++h->pin_count;
    if (h->pin_count == 1) {
        // first time this object is pinned
        alloc->add_gc_root(h);
    }
    pinned_objects.push_front(h);
}

allocator::allocator(global_env* use_globals)
    : globals{use_globals}
    , gc_enabled{true}
    , to_collect{false}
    , mem_usage{0}
    , collect_threshold{FIRST_COLLECT}
    , count{0} {
}

allocator::~allocator() {
    for (auto s : root_stacks) {
        delete s;
    }
    for (auto o : objects) {
        dealloc(o);
    }
}

void allocator::dealloc(gc_header* o) {
    switch (gc_type(*o)) {
    case GC_TYPE_CHUNK:
        // FIXME: this doesn't count the constants array
        mem_usage -= sizeof(code_chunk);
        mem_usage -= ((code_chunk*)o)->constant_arr.size * sizeof(constant_id);
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

            for (int i = 0; i < f->num_upvals; ++i) {
                auto cell = f->upvals[i];
                cell->dereference();
                if (cell->dead()) {
                    delete cell;
                }
            }

            delete f;
        }
        break;
    }
    --count;
}

void allocator::mark_descend_value(value v) {
    if (vhas_header(v)) {
        mark_descend(vheader(v));
    }
}

void allocator::mark_descend(gc_header* o) {
    if (gc_mark(*o)) {
        // already been here or not managed
        return;
    }
    gc_set_mark(*o);

    switch (gc_type(*o)) {
    case GC_TYPE_CHUNK:
        {
            auto x = (code_chunk*)o;
            for (auto v : x->constant_arr) {
                mark_descend_value(v);
            }
        }
        break;
    case GC_TYPE_CONS:
        mark_descend_value(((cons*)o)->head);
        mark_descend_value(((cons*)o)->tail);
        break;
    case GC_TYPE_TABLE:
        {
            auto x = ((fn_table*)o)->contents;
            for (u32 i = 0; i < x.cap; ++i) {
                if (x.array[i] != nullptr) {
                    mark_descend_value(x.array[i]->key);
                    mark_descend_value(x.array[i]->val);
                }
            }
        }
        break;
    case GC_TYPE_FUNCTION:
        {
            auto f = (function*)o;
            auto m = f->num_upvals;
            // upvalues
            for (local_address i = 0; i < m; ++i) {
                auto cell = f->upvals[i];
                if (cell->closed) {
                    mark_descend_value(cell->closed_value);
                }
            }
            auto num_opt = f->stub->pos_params.size - f->stub->req_args;
            for (u32 i = 0; i < num_opt; ++i) {
                mark_descend_value(f->init_vals[i]);
            }
            mark_descend(&f->stub->chunk->h);
        }
        break;
    }
}

void allocator::mark() {
    // roots
    for (auto it = roots.begin(); it != roots.end();) {
        if ((*it)->pin_count > 0) {
            mark_descend(*it);
            ++it;
        } else {
            it = roots.erase(it);
        }
    }

    // stacks
    for (auto it = root_stacks.begin(); it != root_stacks.end();) {
        if ((*it)->dead) {
            delete *it;
            it = root_stacks.erase(it);
        } else {
            for (u32 i = 0; i < (*it)->get_pointer(); ++i) {
                mark_descend_value((*it)->contents[i]);
            }
            for (auto x : (*it)->function_stack) {
                mark_descend((gc_header*)x);
            }
            mark_descend_value((*it)->last_pop);
            ++it;
        }
    }

}

void allocator::sweep() {
#ifdef GC_LOG
    auto orig_ct = count;
    auto orig_sz = mem_usage;
#endif

    for (auto it = objects.begin(); it != objects.end();) {
        if (gc_mark(**it)) {
            gc_unset_mark(**it);
            ++it;
        } else {
            dealloc(*it);
            it = objects.erase(it);
        }
    }

#ifdef GC_LOG
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
            if (mem_usage >= RESCALE_TH * collect_threshold) {
                collect_threshold *= COLLECT_SCALE_FACTOR;
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
    // std::cout << "garbage collection beginning (mem_usage = "
    //           << mem_usage
    //           << ", num_objects() = "
    //           << count
    //           << " ):\n";
#endif

    mark();
    sweep();
    to_collect = false;
}

void allocator::add_gc_root(gc_header* r) {
    roots.push_back(r);
}

root_stack* allocator::add_root_stack() {
    auto res = new root_stack{};
    root_stacks.push_front(res);
    return res;
}

working_set allocator::add_working_set() {
    return working_set{this};
}

void allocator::designate_global(gc_header* o) {
    if (!gc_global(*o)) {
        gc_set_global(*o);
        if (o->pin_count == 0) {
            add_gc_root(o);
        }
        ++o->pin_count;
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
