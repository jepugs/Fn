#ifndef __FN_OBJECT_POOL_HPP
#define __FN_OBJECT_POOL_HPP

namespace fn {

// NOTE: we require that T occupies at least as much space as void*. This is not
// a problem for our purposes. Constructors/destructors are not invoked. You
// gotta use placement new for construction and then manually call the
// destructors.
template<typename T>
class object_pool {
private:
    u32 block_size = 64;
    // the beginning of the block holds a pointer to the next block, so e.g. a
    // block size of 256 has total size 257*sizeof(obj)
    T* first_block;

    // pointer to the next free object location. By casting between T* and void*
    // we can actually embed the free list into the block directly. (This is why
    // we require that sizeof(T) >= sizeof(void*)
    T* first_free;

    // Allocate another block for the pool. This works perfectly fine, but it's
    // absolutely vile.
    T* new_block() {
        auto res = (T*)malloc((1 + block_size)*sizeof(T));
        ((void**)res)[0] = nullptr;

        auto objs = &res[1];
        for (u32 i = 0; i < block_size-1; ++i) {
            // store the pointer to objs[i+1] in objs[i]
            *(void**)(&objs[i]) = &objs[i + 1];
        }
        // look at those casts. C++ is screaming at me not to do this
        *(void**)(&objs[block_size - 1]) = nullptr;
        return res;
    }

public:
    object_pool(u32 block_size=128)
        : block_size{block_size}
        , first_free{nullptr} {
        static_assert(sizeof(T) >= sizeof(void*));
        first_block = new_block();
        first_free = &first_block[1];
    }
    ~object_pool() {
        while (first_block != nullptr) {
            auto next = ((T**)first_block)[0];
            free(first_block);
            first_block = next;
        }
    }

    // get a new object. THIS DOES NOT INVOKE new; you must use placement new on
    // the returned pointer.
    T* new_object() {
        if (first_free == nullptr) {
            auto tmp = first_block;
            first_block = new_block();
            ((T**)first_block)[0] = tmp;
            first_free = &first_block[1];
        }
        auto res = first_free;
        // *first_free is actually a pointer to the next free position
        first_free = *((T**)first_free);
        return res;
    }
    // free an object within the pool. THIS DOES NOT INVOKE THE DESTRUCTOR. You
    // must do that yourself.
    void free_object(T* obj) {
        auto tmp = first_free;
        first_free = obj;
        *(T**)obj = tmp;
    }
};

}

#endif
