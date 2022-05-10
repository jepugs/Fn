#include "vector.hpp"

namespace fn {

// Implementation notes:

// initialize an fn_vec_node. By this sets len and height to 0 by default. They
// must be changed for nonempty/internal nodes.
static void init_vec_node(fn_vec_node* node, u64 nbytes, u8 len, u8 height) {
    init_gc_header(&node->h, GC_TYPE_VEC_NODE, nbytes);
    node->len = len;
    node->height = height;
    node->data.values = (value*)raw_ptr_add(node, sizeof(fn_vec_node));
}

static void init_vec_obj(fn_vec* obj, u64 length, u64 tail_offset,
        fn_vec_node* root, fn_vec_node* tail) {
    init_gc_header(&obj->h, GC_TYPE_VEC, sizeof(fn_vec));
    obj->subvec = false;
    obj->length = length;
    obj->tail_offset = tail_offset;
    obj->root = root;
    obj->tail = tail;
}

void push_empty_vec(istate* S) {
    gc_header* objs[2];
    u64 sizes[2] = {
        round_to_align(sizeof(fn_vec)),
        round_to_align(sizeof(fn_vec_node))
    };
    alloc_nursery_objects(objs, S, sizes, 2);
    auto vec = (fn_vec*)objs[0];
    auto tail = (fn_vec_node*)objs[1];
    init_vec_obj(vec, 0, 0, nullptr, tail);
    init_vec_node(tail, sizes[1], 0, 0);
    push(S, vbox_vec(vec));
}

bool vec_is_empty(istate* S, u32 vec_pos) {
    return vvec(S->stack[vec_pos])->length == 0;
}

// extend the tail of a vector, without writing any new values into it yet.
// new_len must be greater than the current tail length. This replaces (but does
// not mutate) the vector at the specified stack position. len starts out set to
// 0.
static void vec_extend_tail(istate* S, u32 vec_pos, u8 new_len) {
    u64 sizes[2];
    sizes[0] = round_to_align(sizeof(fn_vec));
    sizes[1] = round_to_align(sizeof(fn_vec_node) + new_len * sizeof(value));
    gc_header* objs[2];
    alloc_nursery_objects(objs, S, sizes, 2);

    auto old_vec = vvec(S->stack[vec_pos]);
    auto new_vec = (fn_vec*)objs[0];
    auto tail = (fn_vec_node*)objs[1];
    init_vec_obj(new_vec, new_len + old_vec->tail_offset, old_vec->tail_offset,
            old_vec->root, tail);
    init_vec_node(tail, sizes[1], new_len, 0);
    memcpy(tail->data.values, old_vec->tail->data.values,
            old_vec->tail->len * sizeof(value));

    S->stack[vec_pos] = vbox_vec(new_vec);
}

// non-destructively replace the vector at vec_pos with a new one. The previous
// vector's tail is inserted in the trie and a new uninitialized tail of the
// specified length is created.
static void vec_insert_tail(istate* S, u32 vec_pos, u8 new_tail_len) {
    auto old_vec = vvec(S->stack[vec_pos]);
    if (old_vec->root == nullptr) {
        gc_header* objs[2];
        u64 sizes[2];
        sizes[0] = round_to_align(sizeof(fn_vec));
        sizes[1] = round_to_align(sizeof(fn_vec_node)
                + new_tail_len*sizeof(value));
        alloc_nursery_objects(objs, S, sizes, 2);
        old_vec = vvec(S->stack[vec_pos]);

        auto vec = (fn_vec*)objs[0];
        auto tail = (fn_vec_node*)objs[1];
        init_vec_obj(vec, VEC_BREADTH + new_tail_len, VEC_BREADTH,
                old_vec->tail, tail);
        init_vec_node(tail, sizes[1], new_tail_len, 0);
        S->stack[vec_pos] = vbox_vec(vec);
    } else if ((1u << VEC_INDEX_SHIFT * (old_vec->root->height+1)) == old_vec->tail_offset) {
        // root overflow
        auto height = old_vec->root->height + 1;
        // need to allocate vector object, new root, new tail, and (height - 1)
        // internal nodes with one child each, in that order
        u32 num_objs = height + 2;
        gc_header** objs;
        u64* sizes;
        // FIXME: probably use alloca here instead of malloc
        objs = (gc_header**)malloc(sizeof(gc_header*) * num_objs);
        sizes = (u64*)malloc(sizeof(u64) * num_objs);
        // vector object
        sizes[0] = round_to_align(sizeof(fn_vec));
        // root w/ two nodes
        sizes[1] = round_to_align(sizeof(fn_vec_node) + 2*sizeof(fn_vec_node*));
        // new tail
        sizes[2] = round_to_align(sizeof(fn_vec_node) + new_tail_len*sizeof(value));
        // internal nodes each w/ one pointer
        for (u32 i = 3; i < num_objs; ++i) {
            sizes[i] = round_to_align(sizeof(fn_vec_node) + sizeof(fn_vec_node*));
        }
        alloc_nursery_objects(objs, S, sizes, num_objs);
        old_vec = vvec(S->stack[vec_pos]);
        auto new_vec = (fn_vec*)objs[0];
        auto root = (fn_vec_node*)objs[1];
        auto new_tail = (fn_vec_node*)objs[2];

        auto new_offset = old_vec->tail_offset + VEC_BREADTH;
        init_vec_obj(new_vec, new_offset + new_tail_len, new_offset, root,
                new_tail);
        init_vec_node(new_tail, sizes[2], new_tail_len, 0);
        init_vec_node(root, sizes[1], 2, height);
        root->data.children[0] = old_vec->root;
        if (height == 1) {
            root->data.children[1] = old_vec->tail;
            // FIXME: write barrier here
        } else {
            root->data.children[1] = (fn_vec_node*)objs[3];
            u32 i;
            for (i = 3; i < num_objs - 1; ++i) {
                auto node = (fn_vec_node*)objs[i];
                init_vec_node(node, sizes[i], 1, height + 3 - i);
                node->data.children[0] = (fn_vec_node*)objs[i+1];
            }
            auto node = (fn_vec_node*)objs[i];
            init_vec_node(node, sizes[i], 1, 1);
            node->data.children[0] = old_vec->tail;
            // FIXME: write barrier here
        }
        S->stack[vec_pos] = vbox_vec(new_vec);
        free(objs);
        free(sizes);
    } else {
        // no root overflow
        auto height = old_vec->root->height;
        auto addr = old_vec->tail_offset;
        // FIXME: use alloca
        auto objs = (gc_header**) malloc((height+2) * sizeof(fn_vec_node*));
        auto sizes = (u64*) malloc((height+2) * sizeof(u64));
        sizes[0] = round_to_align(sizeof(fn_vec));
        sizes[height+1] = round_to_align(sizeof(fn_vec_node)
                + new_tail_len*sizeof(value));
        // layout of the objs array: objs[0] = vector object, objs[height+1] =
        // new tail node, and objs[i] = new node at height i

        auto place = old_vec->root;
        u32 i = height;
        // first time descending in the tree is just to get the sizes of the new
        // nodes
        for (; i != 0; --i) {
            auto key = (addr >> i * VEC_INDEX_SHIFT) & VEC_INDEX_MASK;
            if (key >= place->len) {
                sizes[i] = round_to_align(sizeof(fn_vec_node)
                        + (key + 1) * sizeof(fn_vec_node*));
                --i;
                break;
            } else {
                sizes[i] = place->h.size;
                place = place->data.children[key];
            }
        }
        for (; i != 0; --i) {
            // if we're here, it means we're creating new nodes with only one
            // child
            sizes[i] = round_to_align(sizeof(fn_vec_node) + sizeof(fn_vec_node*));
        }
        alloc_nursery_objects(objs, S, sizes, height + 2);
        old_vec = vvec(S->stack[vec_pos]);

        // second time descending we actually build the tree
        place = old_vec->root;
        // ugly flag variable plz ignore
        bool insert_more_nodes = false;
        // set up the new root and internal nodes
        for (i = height; i != 1; --i) {
            auto key = (addr >> i * VEC_INDEX_SHIFT) & VEC_INDEX_MASK;
            auto node = (fn_vec_node*) objs[i];
            if (key >= place->len) {
                init_vec_node(node, sizes[i], key+1, i);
                memcpy(node->data.children, place->data.children,
                        place->len * sizeof(fn_vec_node*));
                node->data.children[key] = (fn_vec_node*) objs[i - 1];
                --i;
                insert_more_nodes = true;
                break;
            } else {
                init_vec_node(node, sizes[i], place->len, i);
                memcpy(node->data.children, place->data.children,
                        place->len * sizeof(fn_vec_node*));
                // this line is the reason we stop at i == 1
                node->data.children[key] = (fn_vec_node*) objs[i - 1];
                place = place->data.children[key];
            }
        }
        if (!insert_more_nodes) {
            auto node = (fn_vec_node*) objs[1];
            // last internal node is a copy with one additional entry
            init_vec_node(node, sizes[1], place->len + 1, 1);
            memcpy(node->data.children, place->data.children,
                    place->len * sizeof(fn_vec_node*));
            // insert the tail
            node->data.children[place->len] = old_vec->tail;
            // FIXME: write barrier
        } else {
            // finish initializing the internal nodes
            for (; i != 1; --i) {
                auto node = (fn_vec_node*) objs[i];
                init_vec_node(node, sizes[i], 1, i);
                node->data.children[0] = (fn_vec_node*) objs[i - 1];
            }
            auto node = (fn_vec_node*) objs[1];
            init_vec_node(node, sizes[1], 1, 1);
            node->data.children[0] = old_vec->tail;
            // FIXME: write barrier
        }
        auto new_vec = (fn_vec*)objs[0];
        auto new_tail = (fn_vec_node*)objs[height + 1];
        init_vec_node(new_tail, sizes[height + 1], new_tail_len, 0);
        init_vec_obj(new_vec, old_vec->length + new_tail_len,
                old_vec->tail_offset + VEC_BREADTH, (fn_vec_node*)objs[height],
                new_tail);

        S->stack[vec_pos] = vbox_vec(new_vec);

        free(objs);
        free(sizes);
    }
}

void vec_append(istate* S, u32 vec_pos, u32 val_pos) {
    auto vec = vvec(S->stack[vec_pos]);
    // check for space in the tail
    if (vec->length - vec->tail_offset < VEC_BREADTH) {
        vec_extend_tail(S, vec_pos, vec->tail->len + 1);
        vec = vvec(S->stack[vec_pos]);
        vec->tail->data.values[vec->tail->len-1] = S->stack[val_pos];
        // FIXME: trigger write barrier
    } else {
        vec_insert_tail(S, vec_pos, 1);
        vvec(S->stack[vec_pos])->tail->data.values[0] = S->stack[val_pos];
        // FIXME: trigger write barrier
    }
}

bool push_vec_lookup(istate* S, u32 vec_pos, u64 index) {
    auto vec = vvec(S->stack[vec_pos]);
    if (index >= vec->length) {
        return false;
    }

    if (index >= vec->tail_offset) {
        push(S, vec->tail->data.values[index - vec->tail_offset]);
    } else {
        auto place = vec->root;
        for (u32 i = vec->root->height; i > 0; --i) {
            auto key = (index >> i * VEC_INDEX_SHIFT) & VEC_INDEX_MASK;
            place = place->data.children[key];
        }
        auto key = index & VEC_INDEX_MASK;
        push(S, place->data.values[key]);
    }
    return true;
}

void pop_to_vec(istate* S, u32 num) {
    push_empty_vec(S);
    auto vec_pos = S->sp - 1;
    for (u32 i = S->sp - 1 - num; i < vec_pos; ++i) {
        vec_append(S, vec_pos, i);
    }
    S->stack[S->sp - 1 - num] = peek(S);
    S->sp = S->sp - num;
}

}
