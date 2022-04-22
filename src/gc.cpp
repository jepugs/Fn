#include "gc.hpp"

// uncomment to do a GC after every allocation
// #define GC_STRESS

namespace fn {

gc_reinitializer gc_reinitializer_table[MAX_GC_TYPES] = {};
gc_scavenger gc_scavenger_table[MAX_GC_TYPES] = {};

void default_reinitializer(gc_header* obj) {
    (void)obj;
}

void default_scavenger(gc_header* obj, gc_scavenge_state* s) {
    (void)obj;
    (void)s;
}

static void set_type_methods(u8 type, gc_reinitializer reinit,
        gc_scavenger scavenge) {
    gc_reinitializer_table[type] = reinit;
    gc_scavenger_table[type] = scavenge;
}

static void reinit_string(gc_header* obj) {
    auto s = (fn_string*)obj;
    s->data = (sizeof(fn_string) + (u8*)s);
}

static void reinit_function(gc_header* obj) {
    auto f = (fn_function*)obj;
    auto stub = f->stub;
    if (stub->h.forward) {
        stub = (function_stub*)stub->h.forward;
    }
    f->init_vals = (value*)(sizeof(fn_function) + (u8*)f);
    f->upvals = (upvalue_cell**) (sizeof(fn_function)
            + stub->num_opt * sizeof(value) + (u8*)f);
}

static void reinit_function_stub(gc_header* obj) {
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

static void reinit_gc_bytes(gc_header* obj) {
    auto bytes = (gc_bytes*)obj;
    bytes->data = (sizeof(gc_bytes) + (u8*)bytes);
}

static void scavenge_cons(gc_header* obj, gc_scavenge_state* s) {
    auto c = (fn_cons*)obj;
    scavenge_boxed_pointer(&c->head, s);
    scavenge_boxed_pointer(&c->tail, s);
}

static void scavenge_table(gc_header* obj, gc_scavenge_state* s) {
    auto tab = (fn_table*)obj;
    scavenge_boxed_pointer(&tab->metatable, s);
    scavenge_pointer((gc_header**)&tab->data, s);
    auto data = (value*)tab->data->data;
    auto m = tab->cap * 2;
    for (u32 i = 0; i < m; i += 2) {
        if (data[i].raw != V_UNIN.raw) {
            scavenge_boxed_pointer(&data[i], s);
            scavenge_boxed_pointer(&data[i+1], s);
        }
    }
}

static void scavenge_function(gc_header* obj, gc_scavenge_state* s) {
    auto f = (fn_function*)obj;
    // IMPORTANT! We must detect if the stub has moved and update it before
    // using it.
    scavenge_pointer((gc_header**)&f->stub, s);
    // update the location of the upvals and initvals arrays
    for (local_address i = 0; i < f->stub->num_upvals; ++i) {
        scavenge_pointer((gc_header**)&f->upvals[i], s);
    }
    // init vals
    for (u32 i = 0; i < f->stub->num_opt; ++i) {
        scavenge_boxed_pointer(&f->init_vals[i], s);
    }
}

static void scavenge_upvalue(gc_header* obj, gc_scavenge_state* s) {
    auto u = (upvalue_cell*)obj;
    if(u->closed) {
        scavenge_boxed_pointer(&u->datum.val, s);
        // open upvalues should be visible from the stack
    }
}

static void scavenge_function_stub(gc_header* obj, gc_scavenge_state* s) {
    auto stub = (function_stub*)obj;
    for (u64 i = 0; i < stub->num_sub_funs; ++i) {
        // check for nullptr to account for function stubs that are not fully
        // initialized
        // FIXME: this is stupid, to use nullptr this way
        if (stub->sub_funs[i]) {
            scavenge_pointer((gc_header**)&stub->sub_funs[i], s);
        }
    }
    for (u64 i = 0; i < stub->num_const; ++i) {
        scavenge_boxed_pointer(&stub->const_arr[i], s);
    }
    // metadata
    if (stub->name) {
        scavenge_pointer((gc_header**)&stub->name, s);
    }
    if (stub->filename) {
        scavenge_pointer((gc_header**)&stub->filename, s);
    }
}

void setup_gc_methods() {
    for (u64 i = 0; i < MAX_GC_TYPES; ++i) {
        gc_reinitializer_table[i] = default_reinitializer;
        gc_scavenger_table[i] = default_scavenger;
    }
    set_type_methods(GC_TYPE_STRING, reinit_string, default_scavenger);
    set_type_methods(GC_TYPE_CONS, default_reinitializer, scavenge_cons);
    set_type_methods(GC_TYPE_TABLE, default_reinitializer, scavenge_table);
    set_type_methods(GC_TYPE_FUNCTION, reinit_function, scavenge_function);
    set_type_methods(GC_TYPE_UPVALUE, default_reinitializer, scavenge_upvalue);
    set_type_methods(GC_TYPE_FUN_STUB, reinit_function_stub,
            scavenge_function_stub);
    set_type_methods(GC_TYPE_GC_BYTES, reinit_gc_bytes, default_scavenger);
}

static gc_card_header* init_gc_card(istate* S, u8 gen) {
    auto res = (gc_card_header*)S->alloc->card_pool.new_object();
    res->next = nullptr;
    res->prev = nullptr;
    res->pointer = GC_CARD_DATA_START;
    res->gen = gen;
    res->dirty = false;
    res->large = false;
    return res;
}

static gc_card_header* init_large_gc_card(u8 gen, u64 size) {
    auto res = (gc_card_header*)std::aligned_alloc(GC_CARD_SIZE,
            size + GC_CARD_DATA_START);
    res->next = nullptr;
    res->prev = nullptr;
    res->pointer = GC_CARD_DATA_START;
    res->gen = gen;
    res->dirty = false;
    res->large = true;
    return res;
}

static void add_card_to_deck(gc_deck& deck, istate* S) {
    auto new_card = init_gc_card(S, deck.gen);
    ++deck.num_cards;
    deck.foot->next = new_card;
    deck.foot = new_card;
}

static void init_deck(gc_deck& deck, istate* S, u8 gen) {
    deck.num_cards = 0;
    deck.num_objs = 0;
    deck.gen = gen;
    deck.head = init_gc_card(S, gen);
    deck.foot = deck.head;
    deck.large_obj_head = nullptr;
    deck.large_obj_foot = nullptr;
}

static void clear_deck(gc_deck& deck, istate* S) {
    for (auto card = deck.head; card != nullptr;) {
        auto next = card->next;
        S->alloc->card_pool.free_object((gc_card*)card);
        card = next;
    }
    for (auto card = deck.large_obj_head; card != nullptr;) {
        auto next = card->next;
        free(card);
        card = next;
    }
}

void init_allocator(allocator& alloc, istate* S) {
    init_deck(alloc.nursery, S, GC_GEN_NURSERY);
    init_deck(alloc.survivor, S, GC_GEN_SURVIVOR);
    init_deck(alloc.tenured, S, GC_GEN_TENURED);
    alloc.nursery_size = DEFAULT_NURSERY_SIZE;
    alloc.majorgc_th = DEFAULT_MAJORGC_TH;
    alloc.handles = nullptr;
}

void deinit_allocator(allocator& alloc, istate* S) {
    clear_deck(alloc.nursery, S);
    clear_deck(alloc.survivor, S);
    clear_deck(alloc.tenured, S);
}


static gc_header* try_alloc_object(gc_deck& deck, u64 size) {
    auto new_end = deck.foot->pointer + size;
    if (new_end > GC_CARD_SIZE) {
        return nullptr;
    }
    auto res = gc_card_object(deck.foot, deck.foot->pointer);
    deck.foot->pointer = new_end;
    ++deck.num_objs;
    return res;
}

static void remove_from_large_list(gc_card_header* card, gc_deck& deck) {
    auto old_prev = card->prev;
    auto old_next = card->next;
    if (old_prev) {
        old_prev->next = old_next;
    } else {
        // (prev == nullptr) implies this is the head of the generation
        deck.large_obj_head = old_next;
    }
    if (old_next) {
        old_next->prev = old_prev;
    } else {
        // (next == nullptr) implies this is the tail of the generation
        deck.large_obj_foot = old_prev;
    }
}

static void add_to_large_list(gc_card_header* card, gc_deck& deck) {
    card->prev = deck.large_obj_foot;
    card->next = nullptr;
    if (deck.large_obj_foot) {
        deck.large_obj_foot->next = card;
    } else {
        deck.large_obj_head = card;
    }
    deck.large_obj_foot = card;
}


static gc_header* alloc_large_in_deck(gc_deck& deck, istate* S, u64 size) {
    auto new_card = init_large_gc_card(deck.gen, size);
    add_to_large_list(new_card, deck);
    return gc_card_object(new_card, GC_CARD_DATA_START);
}

// this never triggers a collection or fails (unless the OS gives up on giving
// us memory lmao)
static gc_header* alloc_in_deck(gc_deck& deck, istate* S, u64 size) {
    // FIXME: handle large objects
    if (size > LARGE_OBJECT_CUTOFF) {
        return alloc_large_in_deck(deck, S, size);
    }
    auto res = try_alloc_object(deck, size);
    if (!res) {
        add_card_to_deck(deck, S);
        res = try_alloc_object(deck, size);
    }
    return res;
}

gc_header* alloc_nursery_object(istate* S, u64 size) {
    // FIXME: handle large objects
#ifdef GC_STRESS
    collect_now(S);
#endif
    if (size > LARGE_OBJECT_CUTOFF) {
        if (S->alloc->nursery.num_cards >= S->alloc->nursery_size) {
            // run a collection when the nursery is full
            collect_now(S);
        }
        return alloc_large_in_deck(S->alloc->nursery, S, size);
    }
    auto res = try_alloc_object(S->alloc->nursery, size);
    if (!res) {
        if (S->alloc->nursery.num_cards >= S->alloc->nursery_size) {
            // run a collection when the nursery is full
            collect_now(S);
        }
        add_card_to_deck(S->alloc->nursery, S);
        res = try_alloc_object(S->alloc->nursery, size);
    }
    return res;
}

gc_header* gc_card_object(gc_card_header* card, u16 addr) {
    return (gc_header*)&(((u8*)card)[addr]);
}

gc_card_header* get_gc_card_header(gc_header* obj) {
    return (gc_card_header*)((u64)obj & ~(GC_CARD_SIZE-1));
}

void write_guard(gc_card_header* card, gc_header* ref) {
    if (card->gen == GC_GEN_TENURED) {
        auto card2 = get_gc_card_header(ref);
        if (card2->gen != GC_GEN_TENURED) {
            card->dirty = true;
        }
    }
}

static gc_header* alloc_survivor_object(istate* S, u64 size) {
    return alloc_in_deck(S->alloc->survivor, S, size);
}

static gc_header* alloc_tenured_object(istate* S, u64 size) {
    return alloc_in_deck(S->alloc->tenured, S, size);
}

gc_header* copy_live_object(gc_header* obj, istate* S) {
    auto card = get_gc_card_header(obj);

    // check whether we even have to make a copy
    if (obj->forward) {
        return obj->forward;
    } else if (card->gen > S->alloc->max_compact_gen) {
        return obj;
    } else if (card->large) {
        if (card->mark) {
            // objects we've already visited this collection cycle
            return obj;
        }
        // objects we should move to a new list
        if (card->gen == GC_GEN_NURSERY) {
            // move card to survivor generation
            remove_from_large_list(card, S->alloc->nursery_from_space);
            card->gen = GC_GEN_SURVIVOR;
            add_to_large_list(card, S->alloc->survivor);
            ++obj->age;
        } else if (card->gen == GC_GEN_SURVIVOR) {
            remove_from_large_list(card, S->alloc->survivor_from_space);
            if (obj->age >= GC_TENURE_AGE) {
                // move card to tenured generation
                card->gen = GC_GEN_TENURED;
                add_to_large_list(card, S->alloc->tenured);
            } else {
                add_to_large_list(card, S->alloc->survivor);
                // FIXME: this next line seems unnecessary
                card->gen = GC_GEN_SURVIVOR;
                ++obj->age;
            }
        } else {
            remove_from_large_list(card, S->alloc->tenured_from_space);
            // FIXME: this next line seems unnecessary
            card->gen = GC_GEN_TENURED;
            add_to_large_list(card, S->alloc->tenured);
        }
        card->mark = true;
        // don't copy :)
        return obj;
    }

    gc_header* res;
    // copy bits from the old object
    if (obj->age >= GC_TENURE_AGE) {
        res = alloc_tenured_object(S, obj->size);
        memcpy(res, obj, obj->size);
    } else {
        res = alloc_survivor_object(S, obj->size);
        memcpy(res, obj, obj->size);
        ++res->age;
    }
    // update internal pointers
    gc_reinitializer_table[obj->type](res);

    // set forwarding information
    obj->forward = res;

    return res;
}

value copy_live_value(value v, istate* S) {
    if (!vhas_header(v)) {
        return v;
    }

    auto h = vheader(v);
    auto new_h = copy_live_object(h, S);
    return vbox_header(new_h);
}

void scavenge_pointer(gc_header** obj, gc_scavenge_state* s) {
    auto gen = get_gc_card_header(*obj)->gen;
    if (gen < s->youngest_ref) {
        s->youngest_ref = gen;
    }
    *obj = copy_live_object(*obj, s->S);
}

void scavenge_boxed_pointer(value* v, gc_scavenge_state* s) {
    if (!vhas_header(*v)) {
        return;
    }
    auto h = vheader(*v);
    auto gen = get_gc_card_header(h)->gen;
    if (gen < s->youngest_ref) {
        s->youngest_ref = gen;
    }
    *v = vbox_header(copy_live_object(h, s->S));
}

static void copy_gc_roots(istate* S) {
    if (S->callee) {
        S->callee = (fn_function*)copy_live_object((gc_header*)S->callee, S);
    }
    for (u32 i = 0; i < S->sp; ++i) {
        S->stack[i] = copy_live_value(S->stack[i], S);
    }
    for (auto& u : S->open_upvals) {
        u = (upvalue_cell*)copy_live_object((gc_header*)u, S);
    }
    for (auto& v : S->G->def_arr) {
        v = copy_live_value(v, S);
    }
    for (auto e : S->G->macro_tab) {
        e->val = (fn_function*)copy_live_object((gc_header*)e->val, S);
    }
    S->G->list_meta = copy_live_value(S->G->list_meta, S);
    S->G->string_meta = copy_live_value(S->G->string_meta, S);
    if (S->filename) {
        S->filename = (fn_string*)copy_live_object((gc_header*)S->filename, S);
    }
    if (S->wd) {
        S->wd = (fn_string*)copy_live_object((gc_header*)S->wd, S);
    }
    for (auto& f : S->stack_trace) {
        f.callee = (fn_function*)copy_live_object((gc_header*)f.callee, S);
    }
    // handles
    auto prev = &(S->alloc->handles);
    while (*prev != nullptr) {
        auto next = &(*prev)->next;
        if ((*prev)->alive) {
            (*prev)->obj = copy_live_object((*prev)->obj, S);
            prev = next;
        } else {
            auto tmp = *prev;
            *prev = *next;
            S->alloc->handle_pool.free_object(tmp);
        }
    }

}

static void scavenge_object(gc_header* obj, istate* S) {
    gc_scavenge_state s;
    s.youngest_ref = GC_GEN_TENURED;
    s.S = S;
    gc_scavenger_table[obj->type](obj, &s);
    auto card = get_gc_card_header(obj);
    if (s.youngest_ref < card->gen) {
        card->dirty = true;
    }
}

static void scavenge_card(gc_card_header* card, istate* S) {
    if (card->large) {
        scavenge_object(gc_card_object(card, GC_CARD_DATA_START), S);
    } else {
        u16 pointer = GC_CARD_DATA_START;
        while (pointer < card->pointer) {
            auto obj = gc_card_object(card, pointer);
            scavenge_object(obj, S);
            pointer += obj->size;
        }
    }
}

// iterate over all the cards in a generation, scavenging the dirty ones. We
// also take this opportunity to update the dirty bit
static void scavenge_dirty(gc_deck& deck, istate* S) {
    auto card = deck.head;
    for (; card != nullptr; card = card->next) {
        // FIXME: Some tests make it seem like it's actually slightly faster to
        // completely ignore the dirty bit. Should revisit this architecture
        // (i.e. marking entire cards dirty). It would likely be better to have
        // a per-generation gray list.
        if (card->dirty) {
            card->dirty = false;
            scavenge_card(card, S);
        }
    }
    card = deck.large_obj_head;
    for (; card != nullptr; card = card->next) {
        if (card->dirty) {
            card->dirty = false;
            scavenge_card(card, S);
        }
    }
}

// During collections, we treat generations like queues that store live objects
// we have yet to scavenge. This is possible because new pages and new objects
// are allocated one object at a time. The following structure holds our place
// in the queue.
struct gc_scavenge_pointer {
    u16 addr;
    gc_card_header* card;
    gc_card_header* large_obj;
};

static bool points_to_end(const gc_scavenge_pointer& p,
        const gc_deck& deck) {
    return p.addr == deck.foot->pointer
            && p.card == deck.foot;
}

static bool points_to_last_large(const gc_scavenge_pointer& p,
        const gc_deck& deck) {
    return p.large_obj == deck.large_obj_foot;
}

// this will not work if points_to_end() would return true
static void scavenge_next(gc_scavenge_pointer& p, istate* S) {
    if (p.addr == p.card->pointer) {
        p.card = p.card->next;
        p.addr = GC_CARD_DATA_START;
    }
    auto obj = gc_card_object(p.card, p.addr);
    p.addr += obj->size;
    scavenge_object(obj, S);
}

static void scavenge_next_large(gc_scavenge_pointer& p, istate* S,
        const gc_deck& deck) {
    if (p.large_obj == nullptr) {
        // indicates no object was scavenged previously
        p.large_obj = deck.large_obj_head;
        if (p.large_obj == nullptr) {
            return;
        }
    } else {
        p.large_obj = p.large_obj->next;
    }
    auto obj = gc_card_object(p.large_obj, GC_CARD_DATA_START);
    scavenge_object(obj, S);
}

static void unset_large_marks(gc_deck& deck) {
    for (auto card = deck.large_obj_head; card != nullptr; card = card->next) {
        card->mark = false;
    }
}

void minor_gc(istate* S) {
    S->alloc->max_compact_gen = GC_GEN_SURVIVOR;

    // nursery and survivor generations will be compacted
    unset_large_marks(S->alloc->nursery);
    unset_large_marks(S->alloc->survivor);
    S->alloc->nursery_from_space = S->alloc->nursery;
    S->alloc->survivor_from_space = S->alloc->survivor;
    init_deck(S->alloc->nursery, S, GC_GEN_NURSERY);
    init_deck(S->alloc->survivor, S, GC_GEN_SURVIVOR);

    // We use to-space as a queue holding unscavenged objects. These structs
    // track our place in the survivor and tenured generations
    gc_scavenge_pointer survivor_pointer;
    survivor_pointer.addr = S->alloc->survivor.foot->pointer;
    survivor_pointer.card = S->alloc->survivor.foot;
    survivor_pointer.large_obj = S->alloc->survivor.large_obj_foot;
    gc_scavenge_pointer tenured_pointer;
    tenured_pointer.addr = S->alloc->tenured.foot->pointer;
    tenured_pointer.card = S->alloc->tenured.foot;
    tenured_pointer.large_obj = S->alloc->tenured.large_obj_foot;

    // scavenge dirty cards
    scavenge_dirty(S->alloc->tenured, S);
    // add roots
    copy_gc_roots(S);

    // scavenge all new objects in the survivors generation
    while (true) {
        while (!points_to_end(survivor_pointer, S->alloc->survivor)) {
            scavenge_next(survivor_pointer, S);
        }
        while (!points_to_last_large(survivor_pointer, S->alloc->survivor)) {
            scavenge_next_large(survivor_pointer, S, S->alloc->survivor);
        }
        while (!points_to_end(tenured_pointer, S->alloc->tenured)) {
            scavenge_next(tenured_pointer, S);
        }
        while (!points_to_last_large(tenured_pointer, S->alloc->tenured)) {
            scavenge_next_large(tenured_pointer, S, S->alloc->tenured);
        }
        if (points_to_end(survivor_pointer, S->alloc->survivor)
                && points_to_last_large(survivor_pointer, S->alloc->survivor)
                && points_to_end(tenured_pointer, S->alloc->tenured)) {
            break;
        }
    }

    // delete all the cards in from space
    clear_deck(S->alloc->nursery_from_space, S);
    clear_deck(S->alloc->survivor_from_space, S);
}

void major_gc(istate* S) {
    S->alloc->max_compact_gen = GC_GEN_TENURED;

    // all generations will be compacted
    S->alloc->nursery_from_space = S->alloc->nursery;
    S->alloc->survivor_from_space = S->alloc->survivor;
    S->alloc->tenured_from_space = S->alloc->tenured;
    unset_large_marks(S->alloc->nursery);
    unset_large_marks(S->alloc->survivor);
    unset_large_marks(S->alloc->tenured);
    init_deck(S->alloc->nursery, S, GC_GEN_NURSERY);
    init_deck(S->alloc->survivor, S, GC_GEN_SURVIVOR);
    init_deck(S->alloc->tenured, S, GC_GEN_TENURED);

    gc_scavenge_pointer survivor_pointer;
    survivor_pointer.addr = S->alloc->survivor.foot->pointer;
    survivor_pointer.card = S->alloc->survivor.foot;
    survivor_pointer.large_obj = S->alloc->survivor.large_obj_foot;
    gc_scavenge_pointer tenured_pointer;
    tenured_pointer.addr = S->alloc->tenured.foot->pointer;
    tenured_pointer.card = S->alloc->tenured.foot;
    tenured_pointer.large_obj = S->alloc->tenured.large_obj_foot;

    // add roots
    copy_gc_roots(S);

    // scavenge all new objects in the survivors generation
    while (true) {
        while (!points_to_end(survivor_pointer, S->alloc->survivor)) {
            scavenge_next(survivor_pointer, S);
        }
        while (!points_to_last_large(survivor_pointer, S->alloc->survivor)) {
            scavenge_next_large(survivor_pointer, S, S->alloc->survivor);
        }
        while (!points_to_end(tenured_pointer, S->alloc->tenured)) {
            scavenge_next(tenured_pointer, S);
        }
        while (!points_to_last_large(tenured_pointer, S->alloc->tenured)) {
            scavenge_next_large(tenured_pointer, S, S->alloc->tenured);
        }
        if (points_to_end(survivor_pointer, S->alloc->survivor)
                && points_to_last_large(survivor_pointer, S->alloc->survivor)
                && points_to_end(tenured_pointer, S->alloc->tenured)) {
            break;
        }
    }

    // delete all the cards in from space
    clear_deck(S->alloc->nursery_from_space, S);
    clear_deck(S->alloc->survivor_from_space, S);
    clear_deck(S->alloc->tenured_from_space, S);
}

void collect_now(istate* S) {
    // NOTE: maybe add a timer to prevent major gc from occurring too often
    if (S->alloc->tenured.num_cards > S->alloc->majorgc_th) {
        major_gc(S);
        // TODO: maybe dynamically grow the heap here if there are too many
        // remaining live objects.
    } else {
        minor_gc(S);
    }
}

}
