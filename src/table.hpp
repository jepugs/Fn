#ifndef __FN_TABLE_HPP
#define __FN_TABLE_HPP

#include "base.hpp"

namespace fn {

static const float REHASH_THRESHOLD = 0.75;

class allocator;

/// hash table with string keys. uses linear probing.
template <typename K, typename T> class table {
    friend class allocator;
public:
    // hash table entry
    struct entry {
        bool live;
        K key;
        T val;

        entry(const K& k, const T& v) : live{true}, key{k}, val{v} { }
    };

private:

    u32 cap;
    u32 threshold;
    u32 size;
    entry* array;

    // increase the capacity by a factor of 2. this involves recomputing all hashes
    void increase_cap() {
        auto prev = array;
        auto old_cap = cap;

        // initialize a new array
        cap *= 2;
        threshold = (u32)(REHASH_THRESHOLD * cap);
        array = (entry*)malloc(sizeof(entry)*cap);
        for (u32 i =0; i < cap; ++i) {
            array[i].live = false;
        }

        // insert the old data, deleting old entries as we go
        for (u32 i=0; i<old_cap; ++i) {
            if (prev[i].live) {
                insert(prev[i].key, prev[i].val);
            }
        }
        free(prev);
    }

public:
    table(u32 init_cap=8)
        : cap{init_cap} {
        float th = REHASH_THRESHOLD * (float) init_cap;
        threshold = (u32)th;
        size = 0;
        array = (entry*)malloc(sizeof(entry)*cap);
        for (u32 i =0; i < init_cap; ++i) {
            array[i].live = false;
        }
    }
    table(const table<K,T>& src)
        : cap{src.cap}
        , threshold{src.threshold}
        , size{src.size}
        , array{(entry*)malloc(sizeof(entry)*cap)} {
        for (u32 i = 0; i < cap; ++i) {
            if (src.array[i].live) {
                new (&array[i]) entry {src.array[i].key, src.array[i].val};
            } else {
                array[i].live = false;
            }
        }
    }
    ~table() {
        for (u32 i = 0; i < cap; ++i) {
            if (array[i].live) {
                array[i].~entry();
            }
        }
        free(array);
    }

    table<K,T>& operator=(const table<K,T>& src) {
        // clean up the old data and just replace this using new
        free(array);

        cap = src.cap;
        threshold = src.threshold;
        size = src.size;
        array = (entry*)malloc(sizeof(entry)*cap);;

        for (u32 i = 0; i < cap; ++i) {
            if (src.array[i].live) {
                new (&array[i]) entry{src.array[i].key, src.array[i].val};
            } else {
                array[i].live = false;
            }
        }

        return *this;
    }

    u32 get_size() {
        return size;
    }

    // insert/overwrite a new entry
    T& insert(const K& k, T v) {
        if (size >= threshold) {
            increase_cap();
        }

        u32 h = hash<K>(k);
        u32 i = h % cap;
        while(true) {
            if (!array[i].live) {
                // no collision; make a new entry
                ++size;
                new (&array[i]) entry{k, v};
                break;
            } else if (array[i].key == k) {
                // no collision; overwrite previous entry
                array[i].val = v;
                break;
            }
            // collision; increment index and try again
            i = (i+1) % cap;
        }
        return array[i].val;
    }

    // returns nullptr when no object is associated to the key
    optional<T> get(const K& k) const {
        u32 h = hash(k);
        u32 i = h % this->cap;
        // this count is so that we don't wrap around a full array
        u64 ct = 0;
        // do linear probing
        while(ct < cap) {
            if (array[i].live) {
                if (array[i].key == k) {
                    // found the key
                    return array[i].val;
                }
            } else {
                // no entry for this key
                return std::nullopt;
            }
            i = (i+1) % cap;
            ++ct;
        }
        return std::nullopt;
    }

    bool has_key(const K& k) const {
        u32 h = hash(k);
        u32 i = h % this->cap;
        // do linear probing
        while(true) {
            if (array[i].live) {
                if (array[i].key == k) {
                    // found the key
                    return true;
                }
            } else {
                // no entry for this key
                return false;
            }
            i = (i+1) % cap;
        }
    }

    const forward_list<K> keys() const {
        forward_list<K> res;
        for (u32 i = 0; i < cap; ++i) {
            if(array[i].live) {
                res.push_front(array[i].key);
            }
        }
        return res;
    }

    bool operator==(const table<K,T>& x) const {
        if (this->size != x.size) {
            return false;
        }
        for (u32 i = 0; i < cap; ++i) {
            if (!array[i].live) continue;

            auto k = array[i].key;
            auto v1 = x.get(k);
            if (!v1.has_value()) {
                return false;
            } else if (this->get(k) != v1) {
                return false;
            }
        }
        return true;
    }

    struct iterator {
        // we guarantee that this i value will point to a non-null entry, or the
        // end.
        u32 i;
        table<K,T>* tab;

        iterator(table<K,T>* tab, u32 start)
            : tab{tab} {
            i = start;
            // search for non-nil
            while (i < tab->cap && !tab->array[i].live) {
                ++i;
            }
        }

        entry* operator*() {
            return &tab->array[i];
        }

        iterator& operator++() {
            ++i;
            // search for non-nil
            while (i < tab->cap && !tab->array[i].live) {
                ++i;
            }
            return *this;
        }
        bool operator!=(const iterator& other) {
            return i != other.i;
        }

    };

    iterator begin() {
        return iterator{this, 0};
    }
    iterator end() {
        return iterator{this, cap};
    }
};


}

#endif
