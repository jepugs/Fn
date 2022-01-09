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


root_stack::root_stack()
    : pointer{0}
    , contents{512}
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

void root_stack::push_string(const string& str) {
    auto ptr = alloc->alloc_string(str);
    auto v = vbox_string(ptr);
    push(v);
    alloc->add_string(ptr);
}

void root_stack::set_string(stack_address place, const string& str) {
    auto ptr = alloc->alloc_string(str);
    auto v = vbox_string(ptr);
    set(place, v);
    alloc->add_string(ptr);
}

void root_stack::push_cons(value hd, value tl) {
    auto ptr = alloc->alloc_cons(hd, tl);
    auto v = vbox_cons(ptr);
    push(v);
    alloc->add_cons(ptr);
}

void root_stack::set_cons(stack_address place, value hd, value tl) {
    auto ptr = alloc->alloc_cons(hd, tl);
    auto v = vbox_cons(ptr);
    set(place, v);
    alloc->add_cons(ptr);
}

void root_stack::top_to_list(u32 n) {
    if (n == 0) {
        push(V_EMPTY);
        return;
    }

    auto ptr = alloc->alloc_cons(contents[pointer - 1], V_EMPTY);
    contents[pointer - 1] = vbox_cons(ptr);
    alloc->add_cons(ptr);

    for (u32 i = 1; i < n; ++i) {
        ptr = alloc->alloc_cons(contents[pointer-i-1], contents[pointer-i]);
        contents[pointer - i - 1] = vbox_cons(ptr);
        alloc->add_cons(ptr);
    }

    pointer = pointer - n + 1;
    contents.resize(pointer);
}

void root_stack::push_table() {
    auto ptr = alloc->alloc_table();
    auto v = vbox_table(ptr);
    push(v);
    alloc->add_table(ptr);
}

void root_stack::set_table(stack_address place) {
    auto ptr = alloc->alloc_table();
    auto v = vbox_table(ptr);
    set(place, v);
    alloc->add_table(ptr);
}

value root_stack::create_function(function_stub* stub, stack_address bp) {
    auto f = alloc->alloc_function(stub);

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

void root_stack::push_callee(fn_function* callee) {
    callee_stack.push_back(callee);
}

void root_stack::pop_callee() {
    callee_stack.resize(callee_stack.size-1);
}

fn_function* root_stack::peek_callee() {
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
    auto ptr = alloc->alloc_cons(hd, tl);
    auto v = vbox_cons(ptr);
    pin(&ptr->h);
    alloc->add_cons(ptr);
    return v;
}

value working_set::add_string(const string& s) {
    auto ptr = alloc->alloc_string(s);
    pin(&ptr->h);
    alloc->add_string(ptr);
    return vbox_string(ptr);
}

value working_set::add_table() {
    auto ptr = alloc->alloc_table();
    auto v = vbox_table(ptr);
    pin(&ptr->h);
    alloc->add_table(ptr);
    return v;
}

value working_set::add_function(function_stub* stub) {
    // stub better not have any init values
    auto ptr = alloc->alloc_function(stub);
    auto v = vbox_function(ptr);
    pin(&ptr->h);
    alloc->add_function(ptr);
    return v;
}

code_chunk* working_set::add_chunk(symbol_id ns_id) {
    // ensure namespace exists
    auto x = alloc->globals->get_ns(ns_id);
    if (!x.has_value()) {
        alloc->globals->create_ns(ns_id);
    }

    auto res = mk_code_chunk(alloc, ns_id);

    auto h = (gc_header*)res;
    pin(h);
    alloc->add_chunk(res);
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
    for (auto h = nursery_head; h != nullptr;) {
        auto tmp = h->next;
        dealloc(h);
        h = tmp;
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
    case GC_TYPE_FUNCTION:
        mem_usage -= sizeof(fn_function);
        {
            auto f = (fn_function*)o;

            for (int i = 0; i < f->num_upvals; ++i) {
                auto cell = f->upvals[i];
                cell->dereference();
                if (cell->dead()) {
                    delete cell;
                }
            }

            delete[] f->init_vals;
            delete[] f->upvals;
            delete f;
        }
        break;
    }
    --count;
}

void allocator::add_mark_value(value v) {
    if (vhas_header(v)) {
        marking_list.push_front(vheader(v));
    }
}

static void add_value_header(value v,
        std::forward_list<gc_header*>* to_list) {
    if (vhas_header(v)) {
        to_list->push_front(vheader(v));
    }
}

void add_accessible(gc_header* o, std::forward_list<gc_header*>* to_list) {
    switch (gc_type(*o)) {
    case GC_TYPE_CHUNK:
        for (auto v : ((code_chunk*)o)->constant_arr) {
            add_value_header(v, to_list);
        }
        break;
    case GC_TYPE_CONS:
        add_value_header(((fn_cons*)o)->head, to_list);
        add_value_header(((fn_cons*)o)->tail, to_list);
        break;
    case GC_TYPE_TABLE:
        {
            add_value_header(((fn_table*)o)->metatable, to_list);
            for (auto entry : ((fn_table*)o)->contents) {
                add_value_header(entry->key, to_list);
                add_value_header(entry->val, to_list);
            }
        }
        break;
    case GC_TYPE_FUNCTION:
        {
            auto f = (fn_function*)o;
            // upvalues
            for (local_address i = 0; i < f->num_upvals; ++i) {
                auto cell = f->upvals[i];
                if (cell->closed) {
                    add_value_header(cell->closed_value, to_list);
                }
            }
            auto num_opt = f->stub->pos_params.size - f->stub->req_args;
            for (u32 i = 0; i < num_opt; ++i) {
                add_value_header(f->init_vals[i], to_list);
            }
            to_list->push_front(&f->stub->chunk->h);
        }
        break;
    }
}

void allocator::mark_descend(gc_header* o) {
    if (gc_mark(*o)) {
        // already been here or not managed
        return;
    }
    gc_set_mark(*o);

    add_accessible(o, &marking_list);
}

void allocator::add_roots_for_marking() {
    // roots
    for (auto it = roots.begin(); it != roots.end();) {
        if ((*it)->pin_count > 0) {
            marking_list.push_front(*it);
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
                add_value_header((*it)->contents[i], &marking_list);
            }
            for (auto x : (*it)->callee_stack) {
                marking_list.push_front((gc_header*)x);
            }
            add_value_header((*it)->last_pop, &marking_list);
            ++it;
        }
    }
}

void allocator::mark() {
    // roots
    add_roots_for_marking();

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

    auto prev_ptr = &nursery_head;
    for (auto h = nursery_head; h != nullptr;) {
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

fn_string* allocator::alloc_string(u32 n) {
    auto res = new fn_string;
    init_gc_header(&res->h, GC_TYPE_STRING);
    res->size = n;
    res->data = (u8*)malloc(n + 1);
    return res;
}

fn_string* allocator::alloc_string(const string& str) {
    auto res = alloc_string(str.size());
    memcpy(res->data, str.c_str(), str.size() + 1);
    return res;
}

fn_cons* allocator::alloc_cons(value hd, value tl) {
    auto res = (fn_cons*) malloc(sizeof(fn_cons));
    init_gc_header(&res->h, GC_TYPE_CONS);
    res->head = hd;
    res->tail = tl;
    return res;
}

fn_function* allocator::alloc_function(function_stub* stub) {
    auto res = new fn_function;
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->stub = stub;
    auto num_opt = stub->pos_params.size - stub->req_args;
    res->init_vals = new value[num_opt];
    res->num_upvals = stub->num_upvals;
    res->upvals = new upvalue_cell*[stub->num_upvals];
    return res;
}

fn_table* allocator::alloc_table() {
    auto res = new fn_table;
    init_gc_header(&res->h, GC_TYPE_TABLE);
    res->metatable = V_NIL;
    return res;
}


void allocator::add_cons(fn_cons* ptr) {
    add_to_obj_list((gc_header*)ptr);
    collect();
    mem_usage += sizeof(fn_cons);
    ++count;
}

void allocator::add_string(fn_string* ptr) {
    add_to_obj_list((gc_header*)ptr);
    collect();
    // FIXME: count string size here
    mem_usage += sizeof(fn_string);
    ++count;
}

void allocator::add_table(fn_table* ptr) {
    add_to_obj_list((gc_header*)ptr);
    collect();
    // FIXME: count table size here
    mem_usage += sizeof(fn_table);
    ++count;
}

void allocator::add_function(fn_function* ptr) {
    add_to_obj_list((gc_header*)ptr);
    collect();
    // FIXME: count function size here
    mem_usage += sizeof(fn_function);
    ++count;
}

void allocator::add_chunk(code_chunk* ptr) {
    add_to_obj_list((gc_header*)ptr);
    collect();
    // FIXME: count chunk size here
    mem_usage += sizeof(code_chunk);
    ++count;
}

void allocator::add_to_obj_list(gc_header* h) {
    h->next = nursery_head;
    nursery_head = h;
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

void allocator::pin_object(gc_header* o) {
    if (o->pin_count == 0) {
        add_gc_root(o);
    }
    ++o->pin_count;
}

void allocator::unpin_object(gc_header* o) {
    --o->pin_count;
}

void allocator::print_status() {
    std::cout << "allocator information\n";
    std::cout << "=====================\n";
    std::cout << "memory used (bytes): " << mem_usage << '\n';
    std::cout << "number of objects: " << count << '\n';
    // descend into values
}

}
