#include "alloc2.hpp"

namespace fn {

static void add_card_to_gen(gc_generation& gen, istate* S) {
    auto new_card = S->alloc->card_pool.new_object();
}

static gc_header* try_alloc_object(gc_generation& gen, u64 size) {
    auto new_end = gen.foot.pointer + size;
    if (new_end > GC_CARD_SIZE) {
        return nullptr;
    }
    auto res = gc_card_object(gen.foot, size);
    gen.foot.pointer = new_end;
    ++gen.num_objs;
    return res;
}

static gc_header* alloc_nursery_object(istate* S, u64 size) {
    // FIXME: handle large objects
    auto res = try_alloc_object(S->alloc->nursery, size);
    if (!res) {
        if (S->alloc->nursery.num_cards == DEFAULT_NURSERY_SIZE) {
            // run a collection when the nursery is full
            collect_now(S);
        } else {
            // add another card to the nursery
            add_card_to_gen(S->alloc->nursery);
        }
    }
    return try_alloc_object(S->alloc->survivor, size);
}

// this never fails (unless the OS gives up on giving us memory lmao)
static gc_header* alloc_in_generation(gc_generation& gen, istate* S, u64 size) {
    // FIXME: handle large objects
    auto res = try_alloc_object(gen, size);
    if (!res) {
        add_card_to_gen(gen);
    }
    return try_alloc_object(gen, size);
}

static gc_header* alloc_survivor_object(istate* S, u64 size) {
    return alloc_in_generation(S->alloc->survivor, S, size);
}

static gc_header* alloc_tenured_object(istate* S, u64 size) {
    return alloc_in_generation(S->alloc->tenured, S, size);
}

static void remove_from_large_list(gc_card_header* card, gc_generation& gen) {
    auto old_prev = card->prev;
    auto old_next = card->next;
    if (old_prev) {
        old_prev->next = old_next;
    } else {
        // (prev == nullptr) implies this is the head of the generation
        gen.head = old_next;
    }
    if (old_next) {
        old_next->prev = old_prev;
    } else {
        // (next == nullptr) implies this is the tail of the generation
        gen.tail = old_prev;
    }
}

static void add_to_large_list(gc_card_header* card, gc_generation& gen) {
    card->prev = gen.large_objs_foot;
    card->next = nullptr;
    gen.large_objs_foot->next = card;
    gen.large_objs_foot = card;
}

gc_header* copy_live_object(gc_header* obj, istate* S) {
    auto card = get_gc_card_header(obj);
    // check whether to move the object
    if (card->gen > S->alloc->max_compact_gen) {
        // mark the card as containing live objects
        card->mark = true;
        return obj;
    } else if (card->large) {
        card->mark = true;
        if (card->gen == GC_GEN_NURSERY) {
            // move card to survivor generation
            remove_from_large_list(card, S->alloc->nursery);
            add_to_large_list(card, S->alloc->survivors);
        } else if (card->gen == GC_GEN_SURVIVOR
                && card->age >= GC_TENURE_AGE) {
            // move card to tenured generation
            remove_from_large_list(card, S->alloc->survivors);
            add_to_large_list(card, S->alloc->tenured);
        }
        // don't copy :)
        return;
    }

    if (obj->type == GC_TYPE_FORWARD) {
        return res->forward;
    }

    gc_header* res;
    // copy bits from the old object
    if (obj->age >= GC_TENURE_AGE) {
        res = alloc_tenured_object(obj->size);
        memcpy(res, obj, obj->size);
    } else {
        res = alloc_survivor_object(obj->size);
        memcpy(res, obj, obj->size);
        ++res->age;
    }
    // update internal pointers
    gc_init_table[obj->type](res);

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
    auto new_h = copy_live_object(S, h);
    return vbox_header(new_h);
}

static void copy_gc_roots(istate* S) {
    if (S->callee) {
        S->callee = copy_live_object((gc_header*)S->callee, S);
    }
    for (u32 i = 0; i < S->sp; ++i) {
        S->stack[i] = copy_live_value(S->stack[i], S);
    }
    for (auto& u : S->open_upvals) {
        u = copy_live_object((gc_header*)u, S);
    }
    for (auto& v : S->G->def_arr) {
        v = copy_live_value(v, S);
    }
    for (auto e : S->G->macro_tab) {
        S->val = copy_live_object((gc_header*)e->val, S);
    }
    S->G->list_meta = copy_live_value(S->G->list_meta, S);
    S->G->string_meta = copy_live_value(S->G->string_meta, S);
    S->filename = copy_live_object((gc_header*)S->filename, S);
    S->wd = copy_live_object((gc_header*)S->wd, S);
    for (auto& f : S->stack_trace) {
        f.callee = copy_live_object((gc_header*)f.callee, S);
    }
}

static void scavenge_object(gc_header* obj, istate* S) {
    gc_scavenge_table[obj->type](obj, S);
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
static void scavenge_dirty(gc_generation& gen, istate* S) {
    auto card = gen.head;
    for (; card != nullptr; card = card->next) {
        if (!card->dirty) {
            continue;
        }
        scavenge_card(card, S);
    }
    card = gen.large_obj_head;
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
        const gc_generation& gen) {
    return p.addr = gen.foot.pointer
        && p.card = gen.foot;
}

static bool points_to_last_large(const gc_scavenge_pointer& p,
        const gc_generation& gen) {
    return p.large_obj == gen.large_obj_foot;
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
        const gc_generation& gen) {
    if (p.large_obj == nullptr) {
        // indicates no object was scavenged previously
        p.large_obj = gen.large_obj_foot;
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
    gc_scavenge_pointer survivors_pointer;
    survivors_pointer.addr = S->alloc.survivors.foot.pointer;
    survivors_pointer.card = S->alloc.survivors.foot;
    survivors_pointer.large_obj = S->alloc.survivors.large_obj_foot;

    // iterate over the survivor and tenured spaces and scavenge dirty cards
    scavenge_dirty(S->alloc->survivors, S);
    scavenge_dirty(S->alloc->tenured, S);
    auto from_space = S->alloc->nursery;
    // initialize a new nursery
    init_generation(S->alloc->nursery, GC_GEN_NURSERY);
    copy_gc_roots(S);

    // scavenge all new objects in the survivors generation
    while (true) {
        while (!points_to_end(survivors_pointer, S->alloc->survivors)) {
            scavenge_next(survivors_pointer, S);
        }
        while (!points_to_last_large(survivors_pointer, S->alloc->survivors)) {
            scavenge_next_large(survivors_pointer, S, S->alloc->survivors);
        }
        if (points_to_end(survivors_pointer, S->alloc->survivors)) {
            break;
        }
    }

    // delete all the cards in from space
    for (auto card = from_space.head; card != nullptr;) {
        auto next = card->next;
        S->alloc->card_pool.free_object(card);
        card = next;
    }
    for (auto card = from_space.large_obj_head; card != nullptr;) {
        auto next = card->next;
        S->alloc->card_pool.free_object(card);
        card = next;
    }
}

}
