#include "allocator.hpp"

#include "istate.hpp"
#include <iostream>

// uncomment to disable the GC. This will lead to very high memory consumption.
// It is used to develop new data structures without fully implementing
// their allocation/collection functions first.
//#define GC_DISABLE

// uncomment to run the GC before every allocation. This will obviously tank
// performance and is only used to locate GC bugs
#define GC_STRESS

// uncomment to have the GC print information to STDOUT on every collection
//#define GC_VERBOSE

namespace fn {

// 64KiB for first collect. Actual usage will be a somewhat higher because
// tables/functions/chunks create some extra data depending on
// entries/upvalues/code
static constexpr u64 FIRST_COLLECT = 64 * 1024;
static constexpr u64 COLLECT_SCALE_FACTOR = 2;
// This essentially says no more than this percentage of available memory may be
// devoted to persistent objects. (It must be less than 100)
static constexpr u64 RESCALE_TH = 80;

static inline constexpr u64 round_to_align(u64 size) {
    return OBJ_ALIGN + ((size - 1) & ~(OBJ_ALIGN - 1));
}

static void add_gc_card(allocator* alloc) {
    auto new_card = alloc->card_pool.new_object();
    new_card->u.h.next = &alloc->active_card->u.h;
    new_card->u.h.discard = false;
    new_card->u.h.pointer = round_to_align(sizeof(gc_card_header));
    alloc->active_card = new_card;
}

gc_card* get_gc_card(gc_header* h) {
    return (gc_card*) ((u64)h & ~(GC_CARD_SIZE-1));
}

allocator::allocator(istate* S)
    : mem_usage{0}
    , count{0}
    , collect_threshold{FIRST_COLLECT}
    , active_card{nullptr}
    , S{S}
    , gc_handles{nullptr} {
    add_gc_card(this);
}

allocator::~allocator() {
}

void allocator::print_status() {
    std::cout << "allocator information\n";
    std::cout << "=====================\n";
    std::cout << "memory used (bytes): " << mem_usage << '\n';
    std::cout << "number of objects: " << count << '\n';
    // descend into values
}

gc_handle* get_handle(allocator* alloc, gc_header* obj) {
    auto res = new gc_handle{
        .obj = obj,
        .alive = true,
        .next = alloc->gc_handles
    };
    alloc->gc_handles = res;
    return res;
}

void release_handle(gc_handle* handle) {
    handle->alive = false;
}

void* alloc_bytes(allocator* alloc, u64 size) {
    alloc->mem_usage += size;
    if (size + alloc->active_card->u.h.pointer > GC_CARD_SIZE) {
        // TODO: account for very large objects
        add_gc_card(alloc);
    }
    auto res = (void*)&alloc->active_card->u.data[alloc->active_card->u.h.pointer];
    alloc->active_card->u.h.pointer += size;
    return res;
}

void alloc_string(istate* S, value* where, u32 len) {
    collect(S);
    auto sz = round_to_align(sizeof(fn_string) + len + 1);
    auto res = (fn_string*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_STRING, sz);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    res->data[len] = 0;
    *where = vbox_string(res);
    ++S->alloc->count;
}

void alloc_string(istate* S, value* where, const string& str) {
    collect(S);
    auto len = str.size();
    auto sz = round_to_align(sizeof(fn_string) + len + 1);
    auto res = (fn_string*)alloc_bytes(S->alloc, sz);
    res->size = len;
    init_gc_header(&res->h, GC_TYPE_STRING, sz);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    memcpy(res->data, str.c_str(), len);
    res->data[len] = 0;
    *where = vbox_string(res);
    ++S->alloc->count;
}

void alloc_cons(istate* S, u32 where, u32 hd, u32 tl) {
    collect(S);
    auto sz = round_to_align(sizeof(fn_cons));
    auto res = (fn_cons*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_CONS, sz);
    res->head = S->stack[hd];
    res->tail = S->stack[tl];
    S->stack[where] = vbox_cons(res);
    ++S->alloc->count;
}

void alloc_table(istate* S, value* where) {
    collect(S);
    auto sz =  round_to_align(sizeof(fn_table));
    auto res = (fn_table*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_TABLE, sz);
    res->size = 0;
    res->cap = FN_TABLE_INIT_CAP;
    res->rehash = 3 * FN_TABLE_INIT_CAP / 4;
    res->contents = (value*)malloc(2 * FN_TABLE_INIT_CAP * sizeof(value));
    for (u32 i = 0; i < 2*FN_TABLE_INIT_CAP; i += 2) {
        res->contents[i] = V_UNIN;
    }
    res->metatable = V_NIL;
    *where = vbox_table(res);
    ++S->alloc->count;
}

static function_stub* mk_fun_stub(allocator* alloc, symbol_id ns_id) {
    auto sz = sizeof(function_stub);
    auto res = (function_stub*)alloc_bytes(alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUN_STUB, sz);
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

void alloc_sub_stub(istate* S, gc_handle* stub_handle) {
    collect(S);
    auto stub = (function_stub*)stub_handle->obj;
    auto res = mk_fun_stub(S->alloc, stub->ns_id);
    stub->sub_funs.push_back(res);
    ++S->alloc->count;
}

void alloc_empty_fun(istate* S,
        value* where,
        symbol_id ns_id) {
    collect(S);
    auto sz = sizeof(fn_function);
    auto res = (fn_function*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->init_vals = nullptr;
    res->upvals = nullptr;
    res->stub = mk_fun_stub(S->alloc, ns_id);
    *where = vbox_function(res);
    S->alloc->count += 2;
}

static upvalue_cell* alloc_open_upval(istate* S, u32 pos) {
    auto sz = round_to_align(sizeof(upvalue_cell));
    auto res = (upvalue_cell*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_UPVALUE, sz);
    res->closed = false;
    res->datum.pos = pos;
    S->alloc->count += 1;
    return res;
}

static upvalue_cell* alloc_closed_upval(istate* S) {
    auto sz = sizeof(upvalue_cell);
    auto res = (upvalue_cell*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_UPVALUE, sz);
    res->closed = true;
    res->datum.val = V_NIL;
    S->alloc->count += 1;
    return res;
}

void alloc_foreign_fun(istate* S,
        value* where,
        void (*foreign)(istate*),
        u32 num_params,
        bool vari,
        u32 num_upvals) {
    collect(S);
    auto sz = round_to_align(sizeof(fn_function)
            + num_upvals * sizeof(upvalue_cell*));
    auto res = (fn_function*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->init_vals = nullptr;
    res->upvals = (upvalue_cell**)((u8*)res + sizeof(fn_function));
    for (u32 i = 0; i < num_upvals; ++i) {
        res->upvals[i] = alloc_closed_upval(S);
        res->upvals[i]->closed = true;
    }
    res->stub = mk_fun_stub(S->alloc, S->ns_id);
    res->stub->foreign = foreign;
    res->stub->num_params = num_params;
    res->stub->num_upvals = num_upvals;
    res->stub->vari = vari;
    *where = vbox_function(res);
    S->alloc->count += 2;
}

static upvalue_cell* open_upval(istate* S, u32 pos) {
    for (u32 i = 0; i < S->open_upvals.size; ++i) {
        if (S->open_upvals[i]->datum.pos == pos) {
            return S->open_upvals[i];
        } else if (S->open_upvals[i]->datum.pos > pos) {
            // insert a new upvalue cell
            S->open_upvals.push_back(S->open_upvals[S->open_upvals.size - 1]);
            for (u32 j = S->open_upvals.size - 2; j > i; --j) {
                S->open_upvals[j] = S->open_upvals[j - 1];
            }
            S->open_upvals[i] = alloc_open_upval(S, pos);
            return S->open_upvals[i];
        }
    }
    S->open_upvals.push_back(alloc_open_upval(S, pos));
    return S->open_upvals[S->open_upvals.size - 1];
}

void alloc_fun(istate* S, u32 where, u32 enclosing, constant_id fid) {
    collect(S);
    auto enc_fun = vfunction(S->stack[enclosing]);
    auto stub = enc_fun->stub->sub_funs[fid];
    // size of upvals + initvals arrays
    auto sz = round_to_align(sizeof(fn_function)
            + stub->num_opt*sizeof(value)
            + stub->num_upvals*sizeof(upvalue_cell*));
    auto res = (fn_function*)alloc_bytes(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->init_vals = (value*)((u8*)res + sizeof(fn_function));
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
            res->upvals[i] = enc_fun->upvals[stub->upvals[i]];
        }
    }

    S->stack[where] = vbox_function(res);
    S->alloc->count += 1;
}



static void update_or_q_value(value* place, dyn_array<value*>* q) {
    if (!vhas_header(*place)) {
        return;
    }
    auto ptr = vheader(*place);
    if (ptr->type == GC_TYPE_FORWARD) {
        *place = vbox_header(ptr->forward);
    } else {
        q->push_back(place);
    }
}
static void update_or_q_header(gc_header** place, dyn_array<gc_header**>* q) {
    if ((*place)->type == GC_TYPE_FORWARD) {
        *place = (*place)->forward;
    } else {
        q->push_back(place);
    }
}

// copy an object to a new gc_card. THIS DOES NOT UPDATE INTERNAL POINTERS
static void copy_and_forward(istate* S, gc_header* obj) {
    auto alloc = S->alloc;
    auto new_loc = (gc_header*)alloc_bytes(alloc, obj->size);
    memcpy(new_loc, obj, obj->size);
    obj->type = GC_TYPE_FORWARD;
    obj->forward = new_loc;
}

void move_live_object(istate* S, gc_header* obj,
        dyn_array<gc_header**>* hdr_q, dyn_array<value*>* val_q) {
    copy_and_forward(S, obj);
    obj = obj->forward;

    switch (gc_type(*obj)) {
    case GC_TYPE_STRING: {
        auto s = (fn_string*)obj;
        s->data = (sizeof(fn_string) + (u8*)s);
        break;
    }
    case GC_TYPE_CONS:
        update_or_q_value(&((fn_cons*)obj)->head, val_q);
        update_or_q_value(&((fn_cons*)obj)->tail, val_q);
        break;
    case GC_TYPE_TABLE: {
        auto tab = (fn_table*)obj;
        update_or_q_value(&tab->metatable, val_q);
        auto m = tab->cap * 2;
        for (u32 i = 0; i < m; i += 2) {
            if (tab->contents[i].raw != V_UNIN.raw) {
                update_or_q_value(&tab->contents[i], val_q);
                update_or_q_value(&tab->contents[i+1], val_q);
            }
        } 
    }
        break;
    case GC_TYPE_FUNCTION: {
        auto f = (fn_function*)obj;
        // IMPORTANT! We must detect if the stub has moved and update it before
        // using it. Calling update_or_q_header() first is crucial.
        update_or_q_header((gc_header**)&f->stub, hdr_q);
        // FIXME: this should be handled in a subroutine to reduce code
        // duplication
        f->init_vals = (value*)(sizeof(fn_function) + (u8*)f);
        f->upvals = (upvalue_cell**) (sizeof(fn_function)
                + f->stub->num_opt * sizeof(value) + (u8*)f);
        // update the location of the upvals and initvals arrays
        for (local_address i = 0; i < f->stub->num_upvals; ++i) {
            update_or_q_header((gc_header**)&f->upvals[i], hdr_q);
        }
        // init vals
        for (u32 i = 0; i < f->stub->num_opt; ++i) {
            update_or_q_value(&f->init_vals[i], val_q);
        }
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto s = (function_stub*)obj;
        for (auto& x : s->sub_funs) {
            update_or_q_header((gc_header**)&x, hdr_q);
        }
        for (auto& x : s->const_arr) {
            update_or_q_value(&x, val_q);
        }
    }
        break;
    case GC_TYPE_UPVALUE: {
        auto u = (upvalue_cell*)obj;
        if(u->closed) {
            update_or_q_value(&u->datum.val, val_q);
            // open upvalues should be visible from the stack
        }
    }
        break;
    }
}

void mark(istate* S) {
    auto from_space = S->alloc->active_card;
    S->alloc->active_card = nullptr;
    add_gc_card(S->alloc);

    dyn_array<value*> val_q;
    dyn_array<gc_header**> hdr_q;

    // istate function
    if (S->callee) {
        hdr_q.push_back((gc_header**)&S->callee);
    }
    // stack
    for (u32 i = 0; i < S->sp; ++i) {
        val_q.push_back(&S->stack[i]);
    }
    // open upvalues
    for (auto& u : S->open_upvals) {
        hdr_q.push_back((gc_header**)&u);
    }
    // globals
    for (auto& v : S->G->def_arr) {
        val_q.push_back(&v);
    }
    for (auto e : S->G->macro_tab) {
        hdr_q.push_back((gc_header**)&e->val);
    }
    // handles
    auto prev = &(S->alloc->gc_handles);
    while (*prev != nullptr) {
        auto next = &(*prev)->next;
        if ((*prev)->alive) {
            hdr_q.push_back(&(*prev)->obj);
            prev = next;
        } else {
            auto tmp = *prev;
            *prev = *next;
            delete tmp;
        }
    }

    while (true) {
        while (val_q.size > 0) {
            auto i = val_q.size - 1;
            auto place = val_q[i];
            val_q.pop();
            if (vhas_header(*place)) {
                auto h = vheader(*place);
                if (h->type != GC_TYPE_FORWARD) {
                    move_live_object(S, h, &hdr_q, &val_q);
                }
                *place = vbox_header(h->forward);
            }
        }

        while (hdr_q.size > 0) {
            auto i = hdr_q.size - 1;
            auto place = hdr_q[i];
            hdr_q.pop();
            if ((*place)->type != GC_TYPE_FORWARD) {
                move_live_object(S, *place, &hdr_q, &val_q);
            }
            *place = (*place)->forward;
        }

        if (val_q.size == 0) {
            break;
        }
    }

    for (auto c = from_space; c != nullptr; ) {
        auto tmp = (gc_card*)c->u.h.next;
        S->alloc->mem_usage -= c->u.h.pointer;
        u32 i = round_to_align(sizeof(gc_header*));
        while (i < c->u.h.pointer) {
            auto h = (gc_header*)&c->u.data[i];
            switch (h->type) {
            case GC_TYPE_STRING:
                break;
            case GC_TYPE_TABLE: {
                free(((fn_table*)h)->contents);
                break;
            }
            case GC_TYPE_FUN_STUB: {
                auto stub = (function_stub*) h;
                stub->code.~dyn_array();
                stub->const_arr.~dyn_array();
                stub->sub_funs.~dyn_array();
                stub->upvals.~dyn_array();
                stub->upvals_direct.~dyn_array();
                break;
            }
            case GC_TYPE_FORWARD:
                break;
            }
            i += h->size;
        }
        S->alloc->card_pool.free_object(c);
        c = tmp;
    }
}

void collect(istate* S) {
#ifdef GC_DISABLE
    return;
#endif
    auto alloc = S->alloc;
#ifndef GC_STRESS
    if (alloc->mem_usage >= alloc->collect_threshold) {
#endif
        collect_now(S);
        if (100 * alloc->mem_usage >= RESCALE_TH * alloc->collect_threshold) {
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
#ifdef GC_VERBOSE
    std::cout << "<<GC END: " << S->alloc->count << " objects "
              << "(" << S->alloc->mem_usage << " bytes)" << '\n';
#endif
}


}
