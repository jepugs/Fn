#include "allocator.hpp"

#include "istate.hpp"
#include <iostream>

//#define GC_STRESS
//#define GC_VERBOSE

namespace fn {

// 64KiB for first collect. Actual usage will be a somewhat higher because
// tables/functions/chunks create some extra data depending on
// entries/upvalues/code
static constexpr u32 FIRST_COLLECT = 64 * 1024;
static constexpr f64 COLLECT_SCALE_FACTOR = 2;
// This essentially says no more than this proportion of available memory may be
// devoted to persistent objects
static constexpr f64 RESCALE_TH = 0.8;


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
        cons_pool.free_object((fn_cons*)o);
        break;
    case GC_TYPE_TABLE:
        mem_usage -= sizeof(fn_table);
        ((fn_table*)o)->contents.~table();
        table_pool.free_object((fn_table*)o);
        break;
    case GC_TYPE_FUNCTION: {
        // FIXME: count the size of the upvals array
        auto f = (fn_function*)o;
        mem_usage -= sizeof(fn_function) + sizeof(value)*f->stub->num_opt
            + sizeof(upvalue_cell*)*f->stub->num_upvals;
        free(((fn_function*)o)->init_vals);
        fun_pool.free_object((fn_function*)o);
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto x = (function_stub*)o;
        mem_usage -= sizeof(function_stub);
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
    alloc->mem_usage += size;
    return malloc(size);
}

void alloc_string(istate* S, value* where, u32 len) {
    collect(S);
    auto sz = sizeof(fn_string) + len + 1;
    auto res = (fn_string*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_STRING);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    res->data[len] = 0;
    *where = vbox_string(res);
    ++S->alloc->count;
    add_obj(S->alloc, &res->h);
}

void alloc_string(istate* S, value* where, const string& str) {
    collect(S);
    auto len = str.size();
    auto sz = sizeof(fn_string) + len + 1;
    auto res = (fn_string*)alloc_bytes(S->alloc, sz);
    res->size = len;
    init_gc_header(&res->h, GC_TYPE_STRING);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    memcpy(res->data, str.c_str(), len);
    res->data[len] = 0;
    *where = vbox_string(res);
    ++S->alloc->count;
    add_obj(S->alloc, &res->h);
}

void alloc_cons(istate* S, value* where, value hd, value tl) {
    collect(S);
    //auto res = (fn_cons*)alloc_bytes(S->alloc, sizeof(fn_cons));
    auto res = S->alloc->cons_pool.new_object();
    S->alloc->mem_usage += sizeof(fn_cons);
    init_gc_header(&res->h, GC_TYPE_CONS);
    res->head = hd;
    res->tail = tl;
    *where = vbox_cons(res);
    ++S->alloc->count;
    add_obj(S->alloc, &res->h);
}

void alloc_table(istate* S, value* where) {
    collect(S);
    auto res = S->alloc->table_pool.new_object();
    S->alloc->mem_usage += sizeof(fn_table);
    // auto res = (fn_table*)alloc_bytes(S->alloc, sizeof(fn_table));
    init_gc_header(&res->h, GC_TYPE_TABLE);
    new (&res->contents) table<value, value>;
    res->metatable = V_NIL;
    *where = vbox_table(res);
    ++S->alloc->count;
    add_obj(S->alloc, &res->h);
}

static function_stub* mk_func_stub(allocator* alloc, symbol_id ns_id) {
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
    res->space = 1;
    res->vari = false;
    res->foreign = nullptr;
    res->ns_id = ns_id;
    // we can't actually set up the ns info here, so we leave it uninitialized
    res->name = nullptr;
    res->filename = nullptr;
    res->ci_head = nullptr;
    return res;
}

void alloc_sub_stub(istate* S, function_stub* where) {
    collect(S);
    auto res = mk_func_stub(S->alloc, where->ns_id);
    where->sub_funs.push_back(res);
    ++S->alloc->count;
    add_obj(S->alloc, &res->h);
}

void alloc_empty_fun(istate* S,
        value* where,
        symbol_id ns_id) {
    collect(S);
    auto res = (fn_function*)alloc_bytes(S->alloc, sizeof(fn_function));
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->init_vals = nullptr;
    res->upvals = nullptr;
    res->stub = mk_func_stub(S->alloc, ns_id);
    *where = vbox_function(res);
    S->alloc->count += 2;
    add_obj(S->alloc, &res->h);
    add_obj(S->alloc, &res->stub->h);
}

void alloc_foreign_fun(istate* S,
        value* where,
        void (*foreign)(istate*),
        u32 num_params,
        bool vari,
        symbol_id ns_id) {
    collect(S);
    auto res = (fn_function*)alloc_bytes(S->alloc, sizeof(fn_function));
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->init_vals = nullptr;
    res->upvals = nullptr;
    res->stub = mk_func_stub(S->alloc, ns_id);
    res->stub->foreign = foreign;
    res->stub->num_params = num_params;
    res->stub->num_opt = 0;
    res->stub->vari = vari;
    *where = vbox_function(res);
    S->alloc->count += 2;
    add_obj(S->alloc, &res->h);
    add_obj(S->alloc, &res->stub->h);
}

static void alloc_upval(istate* S, upvalue_cell** where, u32 pos) {
    collect(S);
    auto res = new upvalue_cell;
    init_gc_header(&res->h, GC_TYPE_UPVALUE);
    res->closed = false;
    res->datum.pos = pos;
    *where = res;
    S->alloc->count += 1;
    add_obj(S->alloc, &res->h);
}

static upvalue_cell* open_upval(istate* S, u32 pos) {
    for (u32 i = 0; i < S->open_upvals.size; ++i) {
        if (S->open_upvals[i]->datum.pos == pos) {
            return S->open_upvals[i];
        } else if (S->open_upvals[i]->datum.pos > pos) {
            // insert a new upvalue cell
            // FIXME: maybe not safe to have nullptr here
            S->open_upvals.push_back(S->open_upvals[S->open_upvals.size - 1]);
            for (u32 j = S->open_upvals.size - 2; j > i; --j) {
                S->open_upvals[j] = S->open_upvals[j - 1];
            }
            alloc_upval(S, &S->open_upvals[i], pos);
            return S->open_upvals[i];
        }
    }
    // FIXME: maybe not safe to have nullptr here
    // add to the end of the list
    upvalue_cell* v;
    alloc_upval(S, &v, pos);
    S->open_upvals.push_back(v);
    return S->open_upvals[S->open_upvals.size - 1];
}

void alloc_fun(istate* S, value* where, fn_function* enclosing,
        function_stub* stub) {
    collect(S);
    auto res = S->alloc->fun_pool.new_object();
    S->alloc->mem_usage += sizeof(fn_function);
    // size of upvals + initvals arrays
    auto sz = stub->num_opt*sizeof(value)
        + stub->num_upvals*sizeof(upvalue_cell);
    init_gc_header(&res->h, GC_TYPE_FUNCTION);
    res->init_vals = (value*)alloc_bytes(S->alloc, sz);
    for (u32 i = 0; i < stub->num_opt; ++i) {
        res->init_vals[i] = V_NIL;
    }
    res->upvals = (upvalue_cell**)(stub->num_opt*sizeof(value) + (u8*)res->init_vals);
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
    S->alloc->count += 1;
    add_obj(S->alloc, &res->h);
}

static void add_value_header(value v, dyn_array<gc_header*>* to_list) {
    if (vhas_header(v)) {
        to_list->push_back(vheader(v));
    }
}

static void add_accessible(gc_header* o, dyn_array<gc_header*>* to_list) {
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
            to_list->push_back(&cell->h);
        }
        for (u32 i = 0; i < f->stub->num_opt; ++i) {
            add_value_header(f->init_vals[i], to_list);
        }
        to_list->push_back(&f->stub->h);
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto s = (function_stub*)o;
        for (auto x : s->sub_funs) {
            to_list->push_back(&x->h);
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

static void mark_descend(gc_header* h, dyn_array<gc_header*>* to_list) {
    if (!gc_mark(*h)) {
        gc_set_mark(*h);
        add_accessible(h, to_list);
    }
}

static void mark(istate* S) {
    dyn_array<gc_header*> marking;
    // stack
    for (u32 i = 0; i < S->sp; ++i) {
        add_value_header(S->stack[i], &marking);
    }
    // open upvalues
    for (auto u : S->open_upvals) {
        marking.push_back(&u->h);
    }
    // globals
    for (auto e : S->G->def_tab) {
        add_value_header(e->val, &marking);
    }
    for (auto e : S->G->macro_tab) {
        marking.push_back(&e->val->h);
    }

    while(marking.size > 0) {
        auto h = marking[--marking.size];
        mark_descend(h, &marking);
    }
}

static void sweep(istate* S) {
    auto prev_ptr = &S->alloc->objs_head;
    while (*prev_ptr != nullptr) {
        if (gc_mark(**prev_ptr)) {
            gc_unset_mark(**prev_ptr);
            prev_ptr = &(*prev_ptr)->next;
        } else {
            auto tmp = *prev_ptr;
            *prev_ptr = tmp->next;
            S->alloc->dealloc(tmp);
        }
    }
}

void collect(istate* S) {
    auto alloc = S->alloc;
#ifndef GC_STRESS
    if (alloc->mem_usage >= alloc->collect_threshold) {
#endif
        collect_now(S);
        if (alloc->mem_usage >= RESCALE_TH * alloc->collect_threshold) {
            alloc->collect_threshold *= COLLECT_SCALE_FACTOR;
        }
#ifndef GC_STRESS
    }
#endif
}

void collect_now(istate* S) {
#ifdef GC_VERBOSE
    std::cout << ">>GC START: " << S->alloc->count << " objects "
              << "(" << S->alloc->mem_usage << " bytes)" << '\n';
#endif
    mark(S);
    sweep(S);
#ifdef GC_VERBOSE
    std::cout << "<<GC END: " << S->alloc->count << " objects "
              << "(" << S->alloc->mem_usage << " bytes)" << '\n';
#endif
}


}
