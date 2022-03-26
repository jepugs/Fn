#include "allocator.hpp"

#include "istate.hpp"
#include <iostream>

// uncomment to disable the GC. This will lead to very high memory consumption.
// It is used to develop new data structures without fully implementing
// their allocation/collection functions first.
//#define GC_DISABLE

// uncomment to run the GC before every allocation. This will obviously tank
// performance and is only used to locate GC bugs
//#define GC_STRESS

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

static void add_gc_card(allocator* alloc, gc_card** where, u8 level) {
    auto new_card = alloc->card_pool.new_object();
    new_card->u.h.next = (gc_card_header*)(*where);
    new_card->u.h.count = 0;
    new_card->u.h.pointer = round_to_align(sizeof(gc_card_header));
    new_card->u.h.level = level;
    new_card->u.h.dirty = false;
    *where = new_card;
}

gc_card* get_gc_card(gc_header* h) {
    return (gc_card*) ((u64)h & ~(GC_CARD_SIZE-1));
}

void write_guard(gc_card* card, gc_header* ref) {
    auto card2 = get_gc_card(ref);
    if (card2->u.h.level < card->u.h.level) {
        card->u.h.dirty = true;
    }
}

allocator::allocator(istate* S)
    : mem_usage{0}
    , collect_threshold{FIRST_COLLECT}
    , eden{nullptr}
    , survivor{nullptr}
    , oldgen{nullptr}
    , cycles{0}
    , S{S}
    , gc_handles{nullptr} {
    add_gc_card(this, &eden, GC_LEVEL_EDEN);
    add_gc_card(this, &survivor, GC_LEVEL_SURVIVOR);
    add_gc_card(this, &oldgen, GC_LEVEL_OLDGEN);
}

allocator::~allocator() {
    for (auto h = gc_handles; h != nullptr;) {
        auto tmp = h;
        h = h->next;
        delete tmp;
    }
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

void* try_alloc(allocator* alloc, gc_card* card, u64 size) {
    // TODO: account for very large objects
    alloc->mem_usage += size;
    if (size + card->u.h.pointer > GC_CARD_SIZE) {
        return nullptr;
    }
    auto res = (void*)&card->u.data[card->u.h.pointer];
    card->u.h.pointer += size;
    ++card->u.h.count;
    return res;
}

void* get_bytes_eden(allocator* alloc, u64 size) {
    auto ptr = try_alloc(alloc, alloc->eden, size);
    if (ptr) {
        return ptr;
    }
    add_gc_card(alloc, &alloc->eden, GC_LEVEL_EDEN);
    return try_alloc(alloc, alloc->eden, size);
}

void* get_bytes_survivor(allocator* alloc, u64 size) {
    auto ptr = try_alloc(alloc, alloc->survivor, size);
    if (ptr) {
        return ptr;
    }
    add_gc_card(alloc, &alloc->survivor, GC_LEVEL_SURVIVOR);
    return try_alloc(alloc, alloc->survivor, size);
}

void* get_bytes_oldgen(allocator* alloc, u64 size) {
    auto ptr = try_alloc(alloc, alloc->oldgen, size);
    if (ptr) {
        return ptr;
    }
    add_gc_card(alloc, &alloc->oldgen, GC_LEVEL_OLDGEN);
    return try_alloc(alloc, alloc->oldgen, size);
}

gc_bytes* alloc_gc_bytes(allocator* alloc, u64 nbytes, u8 level) {
    auto sz = round_to_align(sizeof(gc_bytes) + nbytes);
    gc_bytes* res;
    switch (level) {
    case GC_LEVEL_EDEN:
        res = (gc_bytes*)get_bytes_eden(alloc, sz);
        break;
    case GC_LEVEL_SURVIVOR:
        res = (gc_bytes*)get_bytes_survivor(alloc, sz);
        break;
    default:
        res = (gc_bytes*)get_bytes_oldgen(alloc, sz);
        break;
    }
    init_gc_header(&res->h, GC_TYPE_GC_BYTES, sz);
    res->data = (u8*)(sizeof(gc_bytes) + (u64)res);
    return res;
}

gc_bytes* realloc_gc_bytes(allocator* alloc, gc_bytes* src, u64 new_size) {
    auto sz = round_to_align(sizeof(gc_bytes) + new_size);
    auto res = alloc_gc_bytes(alloc, new_size,
            get_gc_card((gc_header*)src)->u.h.level);
    memcpy(res, src, src->h.size);
    // the memcpy overwrites the size
    res->h.size = sz;
    res->data = (u8*)(sizeof(gc_bytes) + (u64)res);
    return res;
}

fn_string* create_string(istate* S, u32 len) {
    auto sz = round_to_align(sizeof(fn_string) + len + 1);
    auto res = (fn_string*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_STRING, sz);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    res->data[len] = 0;
    return res;
}

void alloc_string(istate* S, u32 where, u32 len) {
    collect(S);
    S->stack[where] = vbox_string(create_string(S, len));
}

// create a string without doing collection first
fn_string* create_string(istate* S, const string& str) {
    auto len = str.size();
    auto res = create_string(S, len);
    memcpy(res->data, str.c_str(), len);
    return res;
}

void alloc_string(istate* S, u32 where, const string& str) {
    collect(S);
    auto len = str.size();
    auto sz = round_to_align(sizeof(fn_string) + len + 1);
    auto res = (fn_string*)get_bytes_eden(S->alloc, sz);
    res->size = len;
    init_gc_header(&res->h, GC_TYPE_STRING, sz);
    res->data = (u8*) (((u8*)res) + sizeof(fn_string));
    memcpy(res->data, str.c_str(), len);
    res->data[len] = 0;
    S->stack[where] = vbox_string(res);
}

void alloc_cons(istate* S, u32 where, u32 hd, u32 tl) {
    collect(S);
    auto sz = round_to_align(sizeof(fn_cons));
    auto res = (fn_cons*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_CONS, sz);
    res->head = S->stack[hd];
    res->tail = S->stack[tl];
    S->stack[where] = vbox_cons(res);
}

void alloc_table(istate* S, u32 where) {
    collect(S);
    auto sz =  round_to_align(sizeof(fn_table));
    auto res = (fn_table*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_TABLE, sz);
    res->size = 0;
    res->cap = FN_TABLE_INIT_CAP;
    res->rehash = 3 * FN_TABLE_INIT_CAP / 4;
    res->data = alloc_gc_bytes(S->alloc, 2*FN_TABLE_INIT_CAP*sizeof(value));
    for (u32 i = 0; i < 2*FN_TABLE_INIT_CAP; i += 2) {
        ((value*)(res->data->data))[i] = V_UNIN;
    }
    res->metatable = V_NIL;
    S->stack[where] = vbox_table(res);
}

static function_stub* mk_fun_stub(istate* S, symbol_id ns_id, fn_string* name) {
    auto& alloc = S->alloc;
    auto sz = sizeof(function_stub);
    auto res = (function_stub*)get_bytes_eden(alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUN_STUB, sz);
    init_gc_array(S, &res->code);
    init_gc_array(S, &res->const_arr);
    init_gc_array(S, &res->sub_funs);
    init_gc_array(S, &res->upvals);
    init_gc_array(S, &res->upvals_direct);
    init_gc_array(S, &res->ci_arr);
    res->num_params = 0;
    res->num_opt = 0;
    res->space = 1;
    res->vari = false;
    res->foreign = nullptr;
    res->ns_id = ns_id;
    res->name = name;
    res->filename = S->filename;
    return res;
}

void alloc_sub_stub(istate* S, gc_handle* stub_handle, const string& name) {
    // push_string invokes the collector so we don't need a separate collect
    // call here. Sorry for the leaky abstraction
    push_string(S, name);
    auto stub = (function_stub*)stub_handle->obj;
    auto res = mk_fun_stub(S, stub->ns_id, vstring(peek(S)));
    pop(S); // get rid of the name
    push_back_gc_array(S, &stub->sub_funs, res);
}

void alloc_empty_fun(istate* S,
        u32 where,
        symbol_id ns_id) {
    // empty name for the empty function
    push_string(S, symname(S, ns_id));
    auto sz = sizeof(fn_function);
    auto res = (fn_function*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->init_vals = nullptr;
    res->upvals = nullptr;
    res->stub = mk_fun_stub(S, ns_id, vstring(peek(S)));
    pop(S); // name
    S->stack[where] = vbox_function(res);
}

static upvalue_cell* alloc_open_upval(istate* S, u32 pos) {
    auto sz = round_to_align(sizeof(upvalue_cell));
    auto res = (upvalue_cell*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_UPVALUE, sz);
    res->closed = false;
    res->datum.pos = pos;
    return res;
}

static upvalue_cell* alloc_closed_upval(istate* S) {
    auto sz = sizeof(upvalue_cell);
    auto res = (upvalue_cell*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_UPVALUE, sz);
    res->closed = true;
    res->datum.val = V_NIL;
    return res;
}

void alloc_foreign_fun(istate* S,
        u32 where,
        void (*foreign)(istate*),
        u32 num_params,
        bool vari,
        u32 num_upvals,
        const string& name) {
    push_string(S, name);
    auto sz = round_to_align(sizeof(fn_function)
            + num_upvals * sizeof(upvalue_cell*));
    auto res = (fn_function*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->init_vals = nullptr;
    res->upvals = (upvalue_cell**)((u8*)res + sizeof(fn_function));
    for (u32 i = 0; i < num_upvals; ++i) {
        res->upvals[i] = alloc_closed_upval(S);
        res->upvals[i]->closed = true;
    }
    res->stub = mk_fun_stub(S, S->ns_id, vstring(peek(S)));
    pop(S);
    res->stub->foreign = foreign;
    res->stub->num_params = num_params;
    res->stub->vari = vari;
    // FIXME: allocate foreign fun upvals
    S->stack[where] = vbox_function(res);
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
    auto stub = gc_array_get(&enc_fun->stub->sub_funs, fid);
    // size of upvals + initvals arrays
    auto sz = round_to_align(sizeof(fn_function)
            + stub->num_opt*sizeof(value)
            + stub->upvals.size*sizeof(upvalue_cell*));
    auto res = (fn_function*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->init_vals = (value*)((u8*)res + sizeof(fn_function));
    for (u32 i = 0; i < stub->num_opt; ++i) {
        res->init_vals[i] = V_NIL;
    }
    res->upvals = (upvalue_cell**)(stub->num_opt*sizeof(value) + (u8*)res->init_vals);
    res->stub = stub;

    // set up upvalues
    for (u32 i = 0; i < stub->upvals.size; ++i) {
        if (gc_array_get(&stub->upvals_direct, i)) {
            res->upvals[i] = open_upval(S, S->bp + gc_array_get(&stub->upvals, i));
        } else {
            res->upvals[i] = enc_fun->upvals[gc_array_get(&stub->upvals, i)];
        }
    }

    S->stack[where] = vbox_function(res);
}

static void setup_symcache(istate* S) {
    for (u32 i = 0; i < SYMCACHE_SIZE; ++i) {
        S->symcache->syms[i] = intern(S, sc_names[i]);
    }
}

istate* alloc_istate(const string& filename, const string& wd) {
    auto res = new istate;
    res->alloc = new allocator{res};
    // TODO: allocate this through the allocator instead
    res->symtab = new symbol_table;
    res->symcache = new symbol_cache;
    setup_symcache(res);
    res->G = new global_env;
    res->G->list_meta = V_NIL;
    res->G->string_meta = V_NIL;
    res->ns_id = intern(res, "fn/user");
    res->pc = 0;
    res->bp = 0;
    res->sp = 0;
    res->callee = nullptr;
    res->filename = create_string(res, filename);
    res->wd = create_string(res, wd);
    res->err_happened = false;
    res->err_msg = nullptr;
    // set up namespace
    add_ns(res, res->ns_id);
    return res;
}

constant_id push_back_const(istate* S, gc_handle* stub_handle, value v) {
    auto stub = (function_stub*) stub_handle->obj;
    push_back_gc_array(S, &stub->const_arr, v);
    if (vhas_header(v)) {
        write_guard(get_gc_card(&stub->h), vheader(v));
    }
    return stub->const_arr.size - 1;
}

void push_back_code(istate* S, gc_handle* stub_handle, u8 b) {
    auto stub = (function_stub*) stub_handle->obj;
    push_back_gc_array(S, &stub->code, b);
}

void push_back_upval(istate* S, gc_handle* stub_handle, bool direct, u8 index) {
    auto stub = (function_stub*) stub_handle->obj;
    push_back_gc_array(S, &stub->upvals, index);
    push_back_gc_array(S, &stub->upvals_direct, direct);
}

void update_code_info(istate* S, function_stub* to, const source_loc& loc) {
    push_back_gc_array(S, &to->ci_arr,
            code_info {
                .start_addr = (u32)to->code.size,
                .loc = loc
            });
}

code_info* instr_loc(function_stub* stub, u32 pc) {
    for (u64 i = stub->ci_arr.size; i > 0; --i) {
        auto& c = gc_array_get(&stub->ci_arr, i-1);
        if (c.start_addr <= pc) {
            return &c;
        }
    }
    // this is safe since the first location is always added when the function
    // is created.
    return &gc_array_get(&stub->ci_arr, 0);
}

// copy an object to a new gc_card. THIS DOES NOT UPDATE INTERNAL POINTERS
static gc_header* copy_and_forward(istate* S, gc_header* obj) {
    auto alloc = S->alloc;
    gc_header* new_loc;
    if (obj->age >= GC_RETIREMENT_AGE) {
        new_loc = (gc_header*)get_bytes_oldgen(alloc, obj->size);
    } else {
        // increment age before copying
        ++obj->age;
        new_loc = (gc_header*)get_bytes_survivor(alloc, obj->size);

    }
    memcpy(new_loc, obj, obj->size);
    obj->type = GC_TYPE_FORWARD;
    obj->forward = new_loc;
    return new_loc;
}

static void copy_gc_bytes(istate* S, gc_bytes** obj) {
    *obj = (gc_bytes*)copy_and_forward(S, (gc_header*)*obj);
    (*obj)->data = (sizeof(gc_bytes) + (u8*)*obj);
}

template<typename T>
static void copy_gc_array(istate* S, gc_array<T>* arr) {
    copy_gc_bytes(S, &arr->data);
}

static gc_header* move_object(istate* S, gc_header* obj) {
    obj = copy_and_forward(S, obj);
    switch (gc_type(*obj)) {
    case GC_TYPE_STRING: {
        auto s = (fn_string*)obj;
        s->data = (sizeof(fn_string) + (u8*)s);
        break;
    }
    case GC_TYPE_TABLE: {
        auto tab = (fn_table*)obj;
        // move the array
        copy_gc_bytes(S, &tab->data);
    }
        break;
    case GC_TYPE_FUNCTION: {
        auto f = (fn_function*)obj;
        auto stub = f->stub;
        if (stub->h.type == GC_TYPE_FORWARD) {
            stub = (function_stub*)stub->h.forward;
        }
        f->init_vals = (value*)(sizeof(fn_function) + (u8*)f);
        f->upvals = (upvalue_cell**) (sizeof(fn_function)
                + stub->num_opt * sizeof(value) + (u8*)f);
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto s = (function_stub*)obj;
        copy_gc_array(S, &s->code);
        copy_gc_array(S, &s->const_arr);
        copy_gc_array(S, &s->sub_funs);
        copy_gc_array(S, &s->upvals);
        copy_gc_array(S, &s->upvals_direct);
        copy_gc_array(S, &s->ci_arr);
    }
        break;
    }
    return obj;
}


// analyzes a reference contained in another object. This does three things: (1)
// if the object has already been forwarded, it updates the reference. (2) if
// the level of the reference is at or below the provided level and the object
// has not yet been forwarded, it adds it to the provided queue. (3) it
// determines whether to set the dirty bit of the receiving object's gc card.
static void trace_obj_ref(gc_header** place, dyn_array<gc_header**>* q,
        gc_header* receiver, u8 level) {
    if ((*place)->type == GC_TYPE_FORWARD) {
        *place = (*place)->forward;
    } else if (get_gc_card(*place)->u.h.level <= level) {
        q->push_back(place);
    }
    // update the dirty bit
    auto recv_card = get_gc_card(receiver);
    if (get_gc_card(*place)->u.h.level < recv_card->u.h.level) {
        recv_card->u.h.dirty = true;
    }
}

// like trace_obj_ref, but works on value pointers. Values without headers are
// ignored.
static void trace_value_ref(value* place, dyn_array<value*>* q,
        gc_header* receiver, u8 level) {
    if (!vhas_header(*place)) {
        return;
    }
    auto ptr = vheader(*place);
    if (ptr->type == GC_TYPE_FORWARD) {
        *place = vbox_header(ptr->forward);
    } else if (get_gc_card(ptr)->u.h.level <= level) {
        q->push_back(place);
    }
    // update the dirty bit
    auto recv_card = get_gc_card(receiver);
    if (get_gc_card(ptr)->u.h.level < recv_card->u.h.level) {
        recv_card->u.h.dirty = true;
    }
}

// find all the pointers in an object, adding them to the respective queues.
// Pointers are ignored if they live in a card of a higher level than the one
// provided. E.g. tracing with level GC_LEVEL_EDEN will only add pointers to
// eden, ignoring objects in survivor and oldgen. This also updates dirt bits
// when appropriate.
static void trace_object(gc_header* obj, dyn_array<gc_header**>* hdr_q,
        dyn_array<value*>* val_q, u8 level) {
    switch (gc_type(*obj)) {
    case GC_TYPE_CONS:
        trace_value_ref(&((fn_cons*)obj)->head, val_q, obj, level);
        trace_value_ref(&((fn_cons*)obj)->tail, val_q, obj, level);
        break;
    case GC_TYPE_TABLE: {
        auto tab = (fn_table*)obj;
        trace_value_ref(&tab->metatable, val_q, obj, level);
        auto data = (value*)tab->data->data;
        auto m = tab->cap * 2;
        for (u32 i = 0; i < m; i += 2) {
            if (data[i].raw != V_UNIN.raw) {
                trace_value_ref(&data[i], val_q, obj, level);
                trace_value_ref(&data[i+1], val_q, obj, level);
            }
        }
    }
        break;
    case GC_TYPE_FUNCTION: {
        auto f = (fn_function*)obj;
        // IMPORTANT! We must detect if the stub has moved and update it before
        // using it. Calling trace_obj_ref() first is crucial.
        trace_obj_ref((gc_header**)&f->stub, hdr_q, obj, level);
        // update the location of the upvals and initvals arrays
        for (local_address i = 0; i < f->stub->upvals.size; ++i) {
            trace_obj_ref((gc_header**)&f->upvals[i], hdr_q, obj, level);
        }
        // init vals
        for (u32 i = 0; i < f->stub->num_opt; ++i) {
            trace_value_ref(&f->init_vals[i], val_q, obj, level);
        }
    }
        break;
    case GC_TYPE_FUN_STUB: {
        auto s = (function_stub*)obj;
        for (u64 i = 0; i < s->sub_funs.size; ++i) {
            trace_obj_ref((gc_header**)&gc_array_get(&s->sub_funs, i),
                    hdr_q, obj, level);
        }
        for (u64 i = 0; i < s->const_arr.size; ++i) {
            trace_value_ref(&gc_array_get(&s->const_arr, i), val_q, obj, level);
        }
        // metadata
        trace_obj_ref((gc_header**)&s->name, hdr_q, obj, level);
        trace_obj_ref((gc_header**)&s->filename, hdr_q, obj, level);
    }
        break;
    case GC_TYPE_UPVALUE: {
        auto u = (upvalue_cell*)obj;
        if(u->closed) {
            trace_value_ref(&u->datum.val, val_q, obj, level);
            // open upvalues should be visible from the stack
        }
    }
        break;
    }
}

// trace all the objects in a card. This also updates the dirty bit.
static void trace_card(gc_card* card, dyn_array<gc_header**>* hdr_q,
        dyn_array<value*>* val_q, u8 level) {
    card->u.h.dirty = false;
    auto i = round_to_align(sizeof(gc_card_header));
    while (i < card->u.h.pointer) {
        auto obj = (gc_header*)&card->u.data[i];
        trace_object(obj, hdr_q, val_q, level);
        i += obj->size;
    }
}

static gc_header* update_live_object(istate* S, gc_header* obj,
        dyn_array<gc_header**>* hdr_q, dyn_array<value*>* val_q, u8 level) {
    if (get_gc_card(obj)->u.h.level <= level) {
        obj = move_object(S, obj);
    }
    trace_object(obj, hdr_q, val_q, level);
    return obj;
}

static void mark(istate* S, u8 level) {
    auto from_eden = S->alloc->eden;
    S->alloc->eden = nullptr;
    add_gc_card(S->alloc, &S->alloc->eden, GC_LEVEL_EDEN);
    auto from_survivor = S->alloc->survivor;
    if (level >= GC_LEVEL_SURVIVOR) {
        S->alloc->survivor = nullptr;
        add_gc_card(S->alloc, &S->alloc->survivor, GC_LEVEL_SURVIVOR);
    }
    auto from_oldgen = S->alloc->oldgen;
    if (level >= GC_LEVEL_OLDGEN) {
        S->alloc->oldgen = nullptr;
        add_gc_card(S->alloc, &S->alloc->oldgen, GC_LEVEL_OLDGEN);
    }

    dyn_array<value*> val_q;
    dyn_array<gc_header**> hdr_q;

    // roots are queued at level OLDGEN so that they are always added to the
    // list.

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
            hdr_q.push_back((gc_header**)&(*prev)->obj);
            prev = next;
        } else {
            auto tmp = *prev;
            *prev = *next;
            delete tmp;
        }
    }
    // debugging info
    hdr_q.push_back((gc_header**)&S->filename);
    hdr_q.push_back((gc_header**)&S->wd);
    for (auto& f : S->stack_trace) {
        hdr_q.push_back((gc_header**)&f.callee);
    }
    // builtin metatables
    val_q.push_back(&S->G->list_meta);
    val_q.push_back(&S->G->string_meta);

    // scavenge older generations
    if (level < GC_LEVEL_SURVIVOR) {
        for (auto card = S->alloc->survivor; card != nullptr;
             card = (gc_card*)(card->u.h.next)) {
            if (card->u.h.dirty) {
                trace_card(card, &hdr_q, &val_q, level);
            }
        }
    }
    if (level < GC_LEVEL_OLDGEN) {
        for (auto card = S->alloc->oldgen; card != nullptr;
             card = (gc_card*)(card->u.h.next)) {
            if (card->u.h.dirty) {
                trace_card(card, &hdr_q, &val_q, level);
            }
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
                    *place = vbox_header(update_live_object(S, h, &hdr_q,
                                    &val_q, level));
                } else {
                    *place = vbox_header(h->forward);
                }
            }
        }

        while (hdr_q.size > 0) {
            auto i = hdr_q.size - 1;
            auto place = hdr_q[i];
            hdr_q.pop();
            if ((*place)->type != GC_TYPE_FORWARD) {
                *place = update_live_object(S, *place, &hdr_q, &val_q, level);
            } else {
                *place = (*place)->forward;
            }
        }

        if (val_q.size == 0) {
            break;
        }
    }

    // clean up newly freed gc cards
    for (auto c = from_eden; c != nullptr; ) {
        auto tmp = (gc_card*)c->u.h.next;
        S->alloc->mem_usage -= c->u.h.pointer;
        S->alloc->card_pool.free_object(c);
        c = tmp;
    }
    if (level >= GC_LEVEL_SURVIVOR) {
        for (auto c = from_survivor; c != nullptr; ) {
            auto tmp = (gc_card*)c->u.h.next;
            S->alloc->mem_usage -= c->u.h.pointer;
            S->alloc->card_pool.free_object(c);
            c = tmp;
        }
    }
    if (level >= GC_LEVEL_OLDGEN) {
        for (auto c = from_oldgen; c != nullptr; ) {
            auto tmp = (gc_card*)c->u.h.next;
            S->alloc->mem_usage -= c->u.h.pointer;
            S->alloc->card_pool.free_object(c);
            c = tmp;
        }
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

#ifdef GC_VERBOSE
static u64 count_objects(istate* S) {
    auto ct = 0;
    for (auto c = S->alloc->eden; c != nullptr; c = (gc_card*)c->u.h.next) {
        ct += c->u.h.count;
    }
    for (auto c = S->alloc->survivor; c != nullptr; c = (gc_card*)c->u.h.next) {
        ct += c->u.h.count;
    }
    for (auto c = S->alloc->oldgen; c != nullptr; c = (gc_card*)c->u.h.next) {
        ct += c->u.h.count;
    }
    return ct;
}
#endif

void collect_now(istate* S) {
#ifdef GC_VERBOSE
    std::cout << ">>GC START: " << count_objects(S) << " objects "
              << "(" << S->alloc->mem_usage << " bytes)" << '\n';
#endif
    if (S->alloc->cycles % GC_MAJOR_MULT == 0) {
#ifdef GC_VERBOSE
        std::cout << "[MAJOR GC CYCLE]\n";
#endif
        mark(S, 2);
    } else if (S->alloc->cycles % GC_MINOR_MULT == 0) {
#ifdef GC_VERBOSE
        std::cout << "[MINOR GC CYCLE]\n";
#endif
        mark(S, 1);
    } else {
#ifdef GC_VERBOSE
        std::cout << "[EVACUATION GC CYCLE]\n";
#endif
        mark(S, 0);
    }
    ++S->alloc->cycles;
#ifdef GC_VERBOSE
    std::cout << "<<GC END: " << count_objects(S) << " objects "
              << "(" << S->alloc->mem_usage << " bytes)" << '\n';
#endif
}


}
