#include "allocator.hpp"
#include "istate.hpp"
#include "compile2.hpp"
#include "namespace.hpp"
#include <iostream>

// uncomment to disable the GC. This will lead to very high memory consumption.
// It is used to develop new data structures without fully implementing
// their allocation/collection functions first.
//#define GC_DISABLE

// uncomment to run the GC before every allocation. This will obviously tank
// performance and is only used to locate GC bugs
// #define GC_STRESS

// uncomment to have the GC print information to STDOUT on every collection
//#define GC_VERBOSE

#define raw_ptr_add(ptr, offset) ((((u8*)ptr) + offset))

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
    new_card->u.h.lowest_ref = level;
    *where = new_card;
}

gc_card* get_gc_card(gc_header* h) {
    return (gc_card*) ((u64)h & ~(GC_CARD_SIZE-1));
}

void write_guard(gc_card* card, gc_header* ref) {
    auto card2 = get_gc_card(ref);
    if (card2->u.h.level < card->u.h.lowest_ref) {
        card->u.h.lowest_ref = card2->u.h.level;
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
    // collect(S);
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

void alloc_cons(istate* S, u32 stack_pos, u32 hd, u32 tl) {
    collect(S);
    auto sz = round_to_align(sizeof(fn_cons));
    auto res = (fn_cons*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_CONS, sz);
    res->head = S->stack[hd];
    res->tail = S->stack[tl];
    S->stack[stack_pos] = vbox_cons(res);
}

void alloc_table(istate* S, u32 stack_pos) {
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
    S->stack[stack_pos] = vbox_table(res);
}

static void reify_bc_const(istate* S, const scanner_string_table& sst,
        const bc_output_const& k) {
    switch(k.kind) {
    case bck_number:
        push_number(S, k.d.num);
        break;
    case bck_string:
        push_string(S, scanner_name(sst, k.d.str_id));
        break;
    case bck_symbol:
        push_sym(S, intern(S, scanner_name(sst, k.d.str_id)));
        break;
    case bck_quoted:
        push_quoted(S, sst, k.d.quoted);
        return;
    }
}

gc_handle<function_stub>* gen_function_stub(istate* S,
        const scanner_string_table& sst, const bc_compiler_output& compiled) {
    auto& alloc = S->alloc;
    // compute the size of the object
    // FIXME: there might be a better way to write this...
    // FIXME: also these only need to be aligned to 8 bytes, not 32
    auto code_sz = round_to_align(compiled.code.size);
    auto const_sz = sizeof(value) * compiled.const_table.size;
    auto sub_funs_sz = sizeof(function_stub*) * compiled.sub_funs.size;
    auto upvals_sz = sizeof(upvalue_cell*) * compiled.num_upvals;
    auto upvals_direct_sz = round_to_align(sizeof(bool) * compiled.num_upvals);
    auto code_info_sz = round_to_align(sizeof(code_info) * compiled.ci_arr.size);
    auto sz = round_to_align(sizeof(function_stub) + code_sz + const_sz
            + sub_funs_sz + upvals_sz + upvals_direct_sz + code_info_sz);

    // set up the object
    auto o = (function_stub*)get_bytes_eden(alloc, sz);
    init_gc_header(&o->h, GC_TYPE_FUN_STUB, sz);
    o->foreign = nullptr;
    o->num_params = compiled.params.size;
    o->num_opt = compiled.num_opt;
    o->vari = compiled.has_vari;
    o->space = compiled.stack_required;
    o->ns_id = S->ns_id;
    // TODO: sort this out
    o->name = nullptr;
    o->filename = S->filename;

    // we have to take care when setting up the arrays, as the garbage collector
    // could be invoked while we're building them. Therefore we initialize the
    // traversable arrays (e.g. const array) with length 0, so the collector
    // won't try to traverse an undefined pointer.

    // FIXME: There... there has to be a better way... (to set up these arrays)
    // FIXME: Maybe I can use a template that depends on the array types
    o->code_length = compiled.code.size;
    o->code = (u8*)raw_ptr_add(o, sizeof(function_stub));
    o->num_const = 0;
    o->const_arr = (value*)raw_ptr_add(o, sizeof(function_stub) + code_sz);
    o->num_sub_funs = 0;
    o->sub_funs = (function_stub**)raw_ptr_add(o, sizeof(function_stub)
            + code_sz + const_sz);
    o->num_upvals = compiled.num_upvals;
    o->upvals = (u8*)raw_ptr_add(o, sizeof(function_stub) + code_sz + const_sz
            + sub_funs_sz);
    o->upvals_direct = (bool*)raw_ptr_add(o, sizeof(function_stub) + code_sz
            + const_sz + sub_funs_sz + upvals_sz);
    o->ci_length = compiled.ci_arr.size;
    o->ci_arr = (code_info*)raw_ptr_add(o, sizeof(function_stub) + code_sz
            + const_sz + sub_funs_sz + upvals_sz + upvals_direct_sz);

    // fill out the arrays. Now we need to make a handle since we might trigger
    // garbage collection.
    auto h = get_handle(alloc, o);
    memcpy(h->obj->code, compiled.code.data, compiled.code.size * sizeof(u8));
    // TODO: create and patch in global IDs
    for (u32 i = 0; i < compiled.globals.size; ++i) {
        auto g = compiled.globals[i];
        // FIXME: use a function for this
        auto name = intern(S, scanner_name(sst, g.raw_name));
        auto fqn = resolve_sym(S, S->ns_id, name);
        // FIXME: also use a function to insert the u32
        *(u32*)&h->obj->code[g.patch_addr] = get_global_id(S, fqn);
    }
    for (u32 i = 0; i < compiled.const_table.size; ++i) {
        reify_bc_const(S, sst, compiled.const_table[i]);
        h->obj->const_arr[i] = peek(S);
        pop(S);
        ++h->obj->num_const;
    }
    for (u32 i = 0; i < compiled.sub_funs.size; ++i) {
        auto h2 = gen_function_stub(S, sst, compiled.sub_funs[i]);
        h->obj->sub_funs[i] = h2->obj;
        ++h->obj->num_sub_funs;
        release_handle(h2);
    }
    memcpy(h->obj->upvals, compiled.upvals.data, compiled.num_upvals * sizeof(u8));
    memcpy(h->obj->upvals_direct, compiled.upvals_direct.data,
            compiled.num_upvals * sizeof(bool));
    memcpy(h->obj->ci_arr, compiled.ci_arr.data,
            compiled.ci_arr.size * sizeof(code_info));

    // set the function name
    push_string(S, scanner_name(sst, compiled.name_id));
    h->obj->name = vstring(peek(S));
    pop(S);

    return h;
}

bool reify_function(istate* S, const scanner_string_table& sst,
        const bc_compiler_output& bco) {
    auto stub_handle = gen_function_stub(S, sst, bco);
    // TODO: should error if num_upvals or parameter information is nontrivial.
    // Toplevel compiled functions should have neither arguments nor upvalues.

    auto sz = sizeof(fn_function);
    auto res = (fn_function*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);
    res->stub = stub_handle->obj;
    push(S, vbox_function(res));
    release_handle(stub_handle);
    return true;
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
    auto stub_sz = round_to_align(sizeof(function_stub) + sizeof(code_info));
    res->stub = (function_stub*)get_bytes_eden(S->alloc, stub_sz);
    init_gc_header(&res->stub->h, GC_TYPE_FUN_STUB, stub_sz);
    res->stub->num_upvals = num_upvals;
    res->stub->name = vstring(peek(S));
    pop(S);
    res->stub->filename = S->filename;
    res->stub->foreign = foreign;
    res->stub->num_params = num_params;
    res->stub->vari = vari;

    res->stub->code_length = 0;
    res->stub->num_const = 0;
    res->stub->num_opt = 0;
    res->stub->num_sub_funs = 0;
    res->stub->ci_length = 1;
    res->stub->ci_arr = (code_info*)raw_ptr_add(res->stub,
            sizeof(function_stub));
    res->stub->ci_arr[0] = code_info{
        0,
        source_loc{0, 0}
    };

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

void alloc_fun(istate* S, u32 enclosing, constant_id fid) {
    collect(S);
    // size of upvals + initvals arrays
    auto enc_fun = vfunction(S->stack[enclosing]);
    auto stub = enc_fun->stub->sub_funs[fid];
    auto sz = round_to_align(sizeof(fn_function)
            + stub->num_opt*sizeof(value)
            + stub->num_upvals*sizeof(upvalue_cell*));
    auto res = (fn_function*)get_bytes_eden(S->alloc, sz);
    init_gc_header(&res->h, GC_TYPE_FUNCTION, sz);

    // in case the gc moved the function or stub
    enc_fun = vfunction(S->stack[enclosing]);
    stub = enc_fun->stub->sub_funs[fid];

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

    push(S, vbox_function(res));
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
    res->err.happened = false;
    res->err.message = nullptr;
    // set up namespace
    add_ns(res, res->ns_id);
    return res;
}

// FIXME: move this
code_info* instr_loc(function_stub* stub, u32 pc) {
    for (u64 i = stub->ci_length; i > 0; --i) {
        if (stub->ci_arr[i-1].start_addr <= pc) {
            return &stub->ci_arr[i-1];
        }
    }
    // this is safe since the first location is always added when the function
    // is created.
    // FIXME: or is it?
    return &stub->ci_arr[0];
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
        // FIXME: lotsa pointers to update here...
        auto s = (function_stub*)obj;
        auto code_sz = round_to_align(s->code_length);
        auto const_sz = sizeof(value) * s->num_const;
        auto sub_funs_sz = sizeof(function_stub*) * s->num_sub_funs;
        auto upvals_sz = sizeof(upvalue_cell*) * s->num_upvals;
        auto upvals_direct_sz = round_to_align(sizeof(bool) * s->num_upvals);
        s->code = (u8*)raw_ptr_add(s, sizeof(function_stub));
        s->const_arr = (value*)raw_ptr_add(s, sizeof(function_stub) + code_sz);
        s->sub_funs = (function_stub**)raw_ptr_add(s, sizeof(function_stub)
                + code_sz + const_sz);
        s->upvals = (u8*)raw_ptr_add(s, sizeof(function_stub) + code_sz + const_sz
                + sub_funs_sz);
        s->upvals_direct = (bool*)raw_ptr_add(s, sizeof(function_stub) + code_sz
                + const_sz + sub_funs_sz + upvals_sz);
        s->ci_arr = (code_info*)raw_ptr_add(s, sizeof(function_stub) + code_sz
                + const_sz + sub_funs_sz + upvals_sz + upvals_direct_sz);
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
    if (get_gc_card(*place)->u.h.level < recv_card->u.h.lowest_ref) {
        recv_card->u.h.lowest_ref = get_gc_card(*place)->u.h.level;
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
    if (get_gc_card(ptr)->u.h.level < recv_card->u.h.lowest_ref) {
        recv_card->u.h.lowest_ref = get_gc_card(ptr)->u.h.level;
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
        for (local_address i = 0; i < f->stub->num_upvals; ++i) {
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
        for (u64 i = 0; i < s->num_sub_funs; ++i) {
            trace_obj_ref((gc_header**)&s->sub_funs[i],
                    hdr_q, obj, level);
        }
        for (u64 i = 0; i < s->num_const; ++i) {
            trace_value_ref(&s->const_arr[i], val_q, obj, level);
        }
        // metadata
        if (s->name) {
            trace_obj_ref((gc_header**)&s->name, hdr_q, obj, level);
        }
        if (s->filename) {
            trace_obj_ref((gc_header**)&s->filename, hdr_q, obj, level);
        }
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
    card->u.h.lowest_ref = card->u.h.level;
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
            if (card->u.h.lowest_ref <= level) {
                trace_card(card, &hdr_q, &val_q, level);
            }
        }
    }
    if (level < GC_LEVEL_OLDGEN) {
        for (auto card = S->alloc->oldgen; card != nullptr;
             card = (gc_card*)(card->u.h.next)) {
            if (card->u.h.lowest_ref <= level) {
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
