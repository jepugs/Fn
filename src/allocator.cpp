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
    if (n > 0) {
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

value root_stack::push_string(const string& str) {
    auto ptr = new fn_string{str};
    auto v = vbox_string(ptr);
    push(v);
    alloc->add_string(ptr);
    return v;
}

value root_stack::make_string(stack_address place, const string& str) {
    auto ptr = new fn_string{str};
    auto v = vbox_string(ptr);
    set(place, v);
    alloc->add_string(ptr);
    return v;
}

value root_stack::push_cons(value hd, value tl) {
    auto ptr = new cons{hd, tl};
    auto v = vbox_cons(ptr);
    push(v);
    alloc->add_cons(ptr);
    return v;
}

value root_stack::make_cons(stack_address place, value hd, value tl) {
    auto ptr = new cons{hd, tl};
    auto v = vbox_cons(ptr);
    set(place, v);
    alloc->add_cons(ptr);
    return v;
}

void root_stack::top_to_list(u32 n) {
    if (n == 0) {
        push(V_EMPTY);
        return;
    }

    auto ptr = new cons{contents[pointer - 1], V_EMPTY};
    contents[pointer - 1] = vbox_cons(ptr);
    alloc->add_cons(ptr);

    for (u32 i = 1; i < n; ++i) {
        ptr = new cons{
            contents[pointer - i - 1],
            contents[pointer - i]};
        contents[pointer - i - 1] = vbox_cons(ptr);
        alloc->add_cons(ptr);
    }

    pointer = pointer - n + 1;
    contents.resize(pointer);
}

value root_stack::push_table() {
    auto ptr = new fn_table{};
    auto v = vbox_table(ptr);
    push(v);
    alloc->add_table(ptr);
    return v;
}

value root_stack::make_table(stack_address place) {
    auto ptr = new fn_table{};
    auto v = vbox_table(ptr);
    set(place, v);
    alloc->add_table(ptr);
    return v;
}

value root_stack::create_function(function_stub* stub, stack_address bp) {
    auto f = new function{stub};

    // set up initial values
    auto len = stub->pos_params.size - stub->req_args;
    for (u32 i = 0; i < len; ++i) {
        f->init_vals[i] = peek(i);
    }
    pop_times(len);

    // set upvalues
    for (auto i = 0; i < stub->num_upvals; ++i) {
        auto pos = stub->upvals[i];
        if (stub->upvals_direct[i]) {
            auto u = get_upvalue(bp + pos);
            u->reference();
            f->upvals[i] = u;
        } else {
            auto u = peek_callee()->upvals[pos];
            u->reference();
            f->upvals[i] = u;
        }
    }

    auto v = vbox_function(f);
    push(v);
    alloc->add_function(f);
    return v;
}

void root_stack::push_callee(function* callee) {
    callee_stack.push_back(callee);
}

void root_stack::pop_callee() {
    callee_stack.resize(callee_stack.size-1);
}

function* root_stack::peek_callee() {
    return callee_stack[callee_stack.size-1];
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
    // FIXME: these should really come from a resource pool in the allocator
    auto cell = new upvalue_cell{pos};
    upvals.insert(it, cell);
    return cell;
}

void root_stack::close_upvalues(u32 base_addr) {
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
}

void root_stack::close(u32 base_addr) {
    close_upvalues(base_addr);

    pointer = base_addr;
    contents.resize(base_addr);
}

void root_stack::close_for_tc(u32 n, u32 base_addr) {
    auto old_ptr = pointer;

    close_upvalues(base_addr);

    auto arg_offset = (old_ptr - n) - (base_addr);
    auto end = n + base_addr;
    for (u32 i = base_addr; i < end; ++i) {
        contents[i] = contents[i + arg_offset];
    }

    pointer = base_addr + n;
    contents.resize(pointer);
}


void root_stack::do_return(u32 ret_pos) {
    close_upvalues(ret_pos);
    contents[ret_pos] = contents[pointer - 1];
    pointer = ret_pos + 1;
    contents.resize(pointer);
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

// void allocator::mark_descend_value(value v) {
//     if (vhas_header(v)) {
//         mark_descend(vheader(v));
//     }
// }

void allocator::add_mark_value(value v) {
    if (vhas_header(v)) {
        marking_list.push_front(vheader(v));
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
        for (auto v : ((code_chunk*)o)->constant_arr) {
            add_mark_value(v);
        }
        break;
    case GC_TYPE_CONS:
        add_mark_value(((cons*)o)->head);
        add_mark_value(((cons*)o)->tail);
        break;
    case GC_TYPE_TABLE:
        {
            auto x = ((fn_table*)o)->contents;
            for (u32 i = 0; i < x.cap; ++i) {
                if (x.array[i] != nullptr) {
                    add_mark_value(x.array[i]->key);
                    add_mark_value(x.array[i]->val);
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
                    add_mark_value(cell->closed_value);
                }
            }
            auto num_opt = f->stub->pos_params.size - f->stub->req_args;
            for (u32 i = 0; i < num_opt; ++i) {
                add_mark_value(f->init_vals[i]);
            }
            marking_list.push_front(&f->stub->chunk->h);
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
                add_mark_value((*it)->contents[i]);
            }
            for (auto x : (*it)->callee_stack) {
                mark_descend((gc_header*)x);
            }
            add_mark_value((*it)->last_pop);
            ++it;
        }
    }

    while (true) {
        if (marking_list.empty()) {
            break;
        }
        auto x = marking_list.front();
        marking_list.pop_front();
        mark_descend(x);
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

void allocator::add_cons(cons* ptr) {
    objects.push_back((gc_header*)ptr);
    collect();
    mem_usage += sizeof(cons);
    ++count;
}

void allocator::add_string(fn_string* ptr) {
    objects.push_back((gc_header*)ptr);
    collect();
    // FIXME: count string size here
    mem_usage += sizeof(fn_string);
    ++count;
}

void allocator::add_table(fn_table* ptr) {
    objects.push_back((gc_header*)ptr);
    collect();
    // FIXME: count table size here
    mem_usage += sizeof(fn_table);
    ++count;
}

void allocator::add_function(function* ptr) {
    objects.push_back((gc_header*)ptr);
    collect();
    // FIXME: count function size here
    mem_usage += sizeof(fn_table);
    ++count;
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
    res->alloc = this;
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
