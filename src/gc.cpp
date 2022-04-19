#include "gc.hpp"

namespace fn {

gc_reinitializer gc_reinitializer_table[MAX_GC_TYPES] = {};
gc_scavenger gc_scavenger_table[MAX_GC_TYPES] = {};

static gc_card_header* init_gc_card(istate* S, u8 gen) {
    auto res = (gc_card_header*)S->alloc->card_pool.new_object();
    res->next = nullptr;
    res->prev = nullptr;
    res->pointer = GC_CARD_DATA_START;
    res->gen = gen;
    res->mark = false;
    res->dirty = false;
    res->large = false;
    return res;
}

static void add_card_to_deck(gc_deck& deck, istate* S) {
    auto new_card = init_gc_card(S, deck.gen);
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

void init_allocator(allocator& alloc, istate* S) {
    init_deck(alloc.nursery, S, GC_GEN_NURSERY);
    init_deck(alloc.survivor, S, GC_GEN_SURVIVOR);
    init_deck(alloc.tenured, S, GC_GEN_TENURED);
    alloc.handles = nullptr;
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

gc_header* alloc_nursery_object(istate* S, u64 size) {
    // FIXME: handle large objects
    auto res = try_alloc_object(S->alloc->nursery, size);
    if (!res) {
        if (S->alloc->nursery.num_cards == DEFAULT_NURSERY_SIZE) {
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
    auto card2 = get_gc_card_header(ref);
    if (card2->gen < card->gen) {
        card->dirty = true;
    }
}


// this never triggers a collection or fails (unless the OS gives up on giving
// us memory lmao)
static gc_header* alloc_in_generation(gc_deck& deck, istate* S, u64 size) {
    // FIXME: handle large objects
    auto res = try_alloc_object(deck, size);
    if (!res) {
        add_card_to_deck(deck, S);
    }
    return try_alloc_object(deck, size);
}

static gc_header* alloc_survivor_object(istate* S, u64 size) {
    return alloc_in_generation(S->alloc->survivor, S, size);
}

static gc_header* alloc_tenured_object(istate* S, u64 size) {
    return alloc_in_generation(S->alloc->tenured, S, size);
}

static void remove_from_large_list(gc_card_header* card, gc_deck& deck) {
    auto old_prev = card->prev;
    auto old_next = card->next;
    if (old_prev) {
        old_prev->next = old_next;
    } else {
        // (prev == nullptr) implies this is the head of the generation
        deck.head = old_next;
    }
    if (old_next) {
        old_next->prev = old_prev;
    } else {
        // (next == nullptr) implies this is the tail of the generation
        deck.foot = old_prev;
    }
}

static void add_to_large_list(gc_card_header* card, gc_deck& deck) {
    card->prev = deck.large_obj_foot;
    card->next = nullptr;
    deck.large_obj_foot->next = card;
    deck.large_obj_foot = card;
}

gc_header* copy_live_object(gc_header* obj, istate* S) {
    auto card = get_gc_card_header(obj);

    // check whether we even have to make a copy
    if (obj->type == GC_TYPE_FORWARD) {
        return obj->forward;
    } else if (card->gen > S->alloc->max_compact_gen) {
        // mark the card as containing live objects
        card->mark = true;
        return obj;
    } else if (card->large) {
        card->mark = true;
        if (card->gen == GC_GEN_NURSERY) {
            // move card to survivor generation
            remove_from_large_list(card, S->alloc->nursery);
            add_to_large_list(card, S->alloc->survivor);
            ++obj->age;
        } else if (card->gen == GC_GEN_SURVIVOR
                && obj->age >= GC_TENURE_AGE) {
            // move card to tenured generation
            remove_from_large_list(card, S->alloc->survivor);
            add_to_large_list(card, S->alloc->tenured);
        } else {
            ++obj->age;
        }
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
    obj->type = GC_TYPE_FORWARD;
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
    S->filename = (fn_string*)copy_live_object((gc_header*)S->filename, S);
    S->wd = (fn_string*)copy_live_object((gc_header*)S->wd, S);
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
    gc_scavenger_table[obj->type](obj, S);
}

static void scavenge_card(gc_card_header* card, istate* S) {
    u16 pointer = GC_CARD_DATA_START;
    while (pointer < card->pointer) {
        auto obj = gc_card_object(card, pointer);
        scavenge_object(obj, S);
        pointer += obj->size;
    }
}

// iterate over all the cards in a generation, scavenging the dirty ones. We
// also take this opportunity to update the dirty bit
static void scavenge_dirty(gc_deck& deck, istate* S) {
    auto card = deck.head;
    for (; card != nullptr; card = card->next) {
        if (!card->dirty) {
            continue;
        }
        scavenge_card(card, S);
    }
    card = deck.large_obj_head;
    for (; card != nullptr; card = card->next) {
        if (!card->dirty) {
            continue;
        }
        scavenge_card(card, S);
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
        p.addr = 0;
    }
    auto obj = gc_card_object(p.card, p.addr);
    scavenge_object(obj, S);
    p.addr += obj->size;
}

static void scavenge_next_large(gc_scavenge_pointer& p, istate* S,
        const gc_deck& deck) {
    if (p.large_obj == nullptr) {
        // indicates no object was scavenged previously
        p.large_obj = deck.large_obj_foot;
    }
    p.large_obj = p.large_obj->next;
    auto obj = gc_card_object(p.large_obj, GC_CARD_DATA_START);
    scavenge_object(obj, S);
}

void evacuate_nursery(istate* S) {
    // we need this static assertion here because it guarantees that evacuated
    // nursery objects never end up directly in the tenured generation.
    static_assert(GC_TENURE_AGE >= 1);

    // indicate that we only want nursery objects to be moved
    S->alloc->max_compact_gen = GC_GEN_NURSERY;

    // set up the scavenge pointer to track copied objects
    gc_scavenge_pointer survivor_pointer;
    survivor_pointer.addr = S->alloc->survivor.foot->pointer;
    survivor_pointer.card = S->alloc->survivor.foot;
    survivor_pointer.large_obj = S->alloc->survivor.large_obj_foot;

    // iterate over the survivor and tenured spaces and scavenge dirty cards
    scavenge_dirty(S->alloc->survivor, S);
    scavenge_dirty(S->alloc->tenured, S);
    auto from_space = S->alloc->nursery;
    // initialize a new nursery
    init_deck(S->alloc->nursery, S, GC_GEN_NURSERY);
    copy_gc_roots(S);

    // scavenge all new objects in the survivors generation
    while (true) {
        while (!points_to_end(survivor_pointer, S->alloc->survivor)) {
            scavenge_next(survivor_pointer, S);
        }
        while (!points_to_last_large(survivor_pointer, S->alloc->survivor)) {
            scavenge_next_large(survivor_pointer, S, S->alloc->survivor);
        }
        if (points_to_end(survivor_pointer, S->alloc->survivor)) {
            break;
        }
    }

    // delete all the cards in from space
    for (auto card = from_space.head; card != nullptr;) {
        auto next = card->next;
        S->alloc->card_pool.free_object((gc_card*)card);
        card = next;
    }
    for (auto card = from_space.large_obj_head; card != nullptr;) {
        auto next = card->next;
        S->alloc->card_pool.free_object((gc_card*)card);
        card = next;
    }
}

void collect_now(istate* S) {
    // TODO: implement :'(
}

}
