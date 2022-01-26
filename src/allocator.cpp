#include "allocator.hpp"

#include "istate.hpp"
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
    , S{S}
    , objs_head{nullptr} {
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
        free(o);
        break;
    case GC_TYPE_CONS:
        mem_usage -= sizeof(fn_cons);
        free(o);
        break;
    case GC_TYPE_TABLE:
        mem_usage -= sizeof(fn_table);
        ((fn_table*)o)->contents.~table();
        free(o);
        break;
    case GC_TYPE_FUNCTION: {
        // FIXME: count the size of the upvals array
        mem_usage -= sizeof(fn_function);
        free(o);
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto x = (function_stub*)o;
        x->code.~dyn_array();
        x->const_arr.~dyn_array();
        x->sub_funs.~dyn_array();
        x->upvals.~dyn_array();
        x->upvals_direct.~dyn_array();
        for (auto c = x->ci_head; c != nullptr;) {
            auto tmp = c;
            c = c->prev;
            delete tmp;
        }
        free(o);
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
        for (local_address i = 0; i < f->stub->num_upvals; ++i) {
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

    // mark();
    // sweep();
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

static void add_obj(allocator* alloc, gc_header* obj) {
    obj->next = alloc->objs_head;
    alloc->objs_head = obj;
}

void* alloc_bytes(allocator* alloc, u64 size) {
    return malloc(size);
}

void alloc_string(allocator* alloc, value* where, u32 len) {
    alloc->collect();
    auto sz = sizeof(fn_string) + len + 1;
    auto res = (fn_string*)alloc_bytes(alloc, sz);
    init_gc_header(&res->h, GC_TYPE_STRING);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    res->data[len] = 0;
    *where = vbox_string(res);
    alloc->mem_usage += sz;
    ++alloc->count;
    add_obj(alloc, &res->h);
}
void alloc_string(allocator* alloc, value* where, const string& str) {
    alloc->collect();
    auto len = str.size();
    auto sz = sizeof(fn_string) + len + 1;
    auto res = (fn_string*)alloc_bytes(alloc, sz);
    res->size = len;
    init_gc_header(&res->h, GC_TYPE_STRING);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    memcpy(res->data, str.c_str(), len);
    res->data[len] = 0;
    *where = vbox_string(res);
    alloc->mem_usage += sz;
    ++alloc->count;
    add_obj(alloc, &res->h);
}

void alloc_cons(allocator* alloc, value* where, value hd, value tl) {
    alloc->collect();
    auto res = (fn_cons*)alloc_bytes(alloc, sizeof(fn_cons));
    init_gc_header(&res->h, GC_TYPE_CONS);
    res->head = hd;
    res->tail = tl;
    *where = vbox_cons(res);
    alloc->mem_usage += sizeof(fn_cons);
    ++alloc->count;
    add_obj(alloc, &res->h);
}

void alloc_table(allocator* alloc, value* where) {
    alloc->collect();
    auto res = (fn_table*)alloc_bytes(alloc, sizeof(fn_table));
    init_gc_header(&res->h, GC_TYPE_TABLE);
    new (&res->contents) table<value, value>;
    res->metatable = V_NIL;
    *where = vbox_table(res);
    alloc->mem_usage += sizeof(fn_table);
    ++alloc->count;
    add_obj(alloc, &res->h);
}

static function_stub* mk_func_stub(allocator* alloc, symbol_id ns_id,
        fn_namespace* ns) {
    auto res = (function_stub*)alloc_bytes(alloc, (sizeof(function_stub)));
    init_gc_header(&res->h, GC_TYPE_FUN_STUB);
    new (&res->code) dyn_array<u8>;
    new (&res->const_arr) dyn_array<value>;
    new (&res->sub_funs) dyn_array<function_stub*>; 
    new (&res->upvals) dyn_array<u8>;
    new (&res->upvals_direct) dyn_array<bool>;
    res->num_params = 0;
    res->num_opt = 0;
    res->num_upvals = 0;
    res->vari = false;
    res->foreign = nullptr;
    res->ns_id = ns_id;
    res->ns = ns;
    // we can't actually set up the ns info here, so we leave it uninitialized
    res->name = nullptr;
    res->filename = nullptr;
    res->ci_head = nullptr;
    return res;
}

void alloc_sub_stub(allocator* alloc, function_stub* where) {
    alloc->collect();
    auto res = mk_func_stub(alloc, where->ns_id, where->ns);
    where->sub_funs.push_back(res);
    alloc->mem_usage += sizeof(function_stub);
    ++alloc->count;
    add_obj(alloc, &res->h);
}

void alloc_empty_fun(allocator* alloc,
        value* where,
        symbol_id ns_id,
        fn_namespace* ns) {
    alloc->collect();
    auto res = (fn_function*)alloc_bytes(alloc, sizeof(fn_function));
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->init_vals = nullptr;
    res->upvals = nullptr;
    res->stub = mk_func_stub(alloc, ns_id, ns);
    *where = vbox_function(res);
    alloc->mem_usage += sizeof(function_stub) + sizeof(fn_function);
    alloc->count += 2;
    add_obj(alloc, &res->h);
    add_obj(alloc, &res->stub->h);
}

void alloc_foreign_fun(allocator* alloc,
        value* where,
        void (*foreign)(istate*),
        u32 num_params,
        bool vari,
        symbol_id ns_id,
        fn_namespace* ns) {
    alloc->collect();
    auto res = (fn_function*)alloc_bytes(alloc, sizeof(fn_function));
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->init_vals = nullptr;
    res->upvals = nullptr;
    res->stub = mk_func_stub(alloc, ns_id, ns);
    res->stub->foreign = foreign;
    res->stub->num_params = num_params;
    res->stub->num_opt = 0;
    res->stub->vari = vari;
    *where = vbox_function(res);
    alloc->mem_usage += sizeof(function_stub) + sizeof(fn_function);
    alloc->count += 2;
    add_obj(alloc, &res->h);
    add_obj(alloc, &res->stub->h);
}

static void alloc_upval(allocator* alloc, upvalue_cell** where, u32 pos) {
    alloc->collect();
    auto res = new upvalue_cell;
    init_gc_header(&res->h, GC_TYPE_UPVALUE);
    res->closed = false;
    res->datum.pos = pos;
    *where = res;
    alloc->mem_usage+= sizeof(upvalue_cell);
    alloc->count += 1;
    add_obj(alloc, &res->h);
}

static upvalue_cell* open_upval(istate* S, u32 pos) {
    for (u32 i = 0; i < S->open_upvals.size; ++i) {
        if (S->open_upvals[i]->datum.pos == pos) {
            return S->open_upvals[i];
        } else if (S->open_upvals[i]->datum.pos > pos) {
            // insert a new upvalue cell
            // FIXME: maybe not safe to have nullptr here
            S->open_upvals.push_back(nullptr);
            for (u32 j = S->open_upvals.size - 1; j > i; --j) {
                S->open_upvals[j] = S->open_upvals[j - 1];
            }
            alloc_upval(S->alloc, &S->open_upvals[i], pos);
            return S->open_upvals[i];
        }
    }
    // FIXME: maybe not safe to have nullptr here
    // add to the end of the list
    S->open_upvals.push_back(nullptr);
    alloc_upval(S->alloc, &S->open_upvals[S->open_upvals.size - 1], pos);
    return S->open_upvals[S->open_upvals.size - 1];
}

void alloc_fun(istate* S, value* where, fn_function* enclosing,
        function_stub* stub) {
    S->alloc->collect();
    auto sz = sizeof(fn_function) + stub->num_opt*sizeof(value)
        + stub->num_upvals*sizeof(upvalue_cell);
    auto res = (fn_function*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->init_vals = (value*)(sizeof(fn_function) + (u8*)res);
    for (u32 i = 0; i < stub->num_opt; ++i) {
        res->init_vals[i] = V_NIL;
    }
    res->upvals = (upvalue_cell**)(sizeof(fn_function)
            + stub->num_opt*sizeof(value) + (u8*)res);
    res->stub = stub;

    // set up upvalues
    for (u32 i = 0; i < stub->num_upvals; ++i) {
        if (stub->upvals_direct[i]) {
            res->upvals[i] = open_upval(S, S->bp + stub->upvals[i]);
        } else {
            res->upvals[i] = enclosing->upvals[stub->upvals[i]];
        }
    }

    *where = vbox_function(res);
    S->alloc->mem_usage += sizeof(fn_function);
    S->alloc->count += 1;
    add_obj(S->alloc, &res->h);
}

}
