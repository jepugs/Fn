#ifndef __FN_TABLE_HPP
#define __FN_TABLE_HPP

#include "base.hpp"

namespace fn {

static const float REHASH_THRESHOLD = 0.3;

// string hashing function
template<> inline u32 hash<string>(const string& s) {
    // prime: 16777619
    // offset basis: 2166136261
    u32 res = 2166136261;
    for (u32 i=0; i<s.length(); ++i) {
        res ^= (u32)s[i];
        res *= 16777619;
    }
    return res;
}

// integer hashing
template<> inline u32 hash<u32>(const u32& id) {
    return id;
}

// hash table entry
template <typename K, typename T> struct entry {
    const K key;
    T val;

    entry(const K& k, T& v) : key(k), val(v) { }
};

/// hash table with string keys. uses linear probing.
template <typename K, typename T> class table {
private:
    u32 cap;
    u32 threshold;
    u32 size;
    entry<K,T> **array;

    // increase the capacity by a factor of 2. this involves recomputing all hashes
    void increase_cap() {
        auto *prev = array;
        auto old_cap = cap;

        // initialize a new array
        cap *= 2;
        threshold = (u32)(REHASH_THRESHOLD * this->cap);
        array = new entry<K,T>*[this->cap];
        for (u32 i =0; i < cap; ++i) {
            array[i] = nullptr;
        }

        // insert the old data, deleting old entries as we go
        for (u32 i=0; i<old_cap; ++i) {
            if (prev[i] != nullptr) {
                insert(prev[i]->key, prev[i]->val);
                delete prev[i];
            }
        }
        delete[] prev;
    }

public:
    table(u32 init_cap=32)
        : cap(init_cap)
    {
        threshold = (u32)(REHASH_THRESHOLD * init_cap);
        size = 0;
        array = new entry<K,T>*[init_cap];
        for (u32 i =0; i < init_cap; ++i) {
            array[i] = nullptr;
        }
    }
    table(const table<K,T>& src)
        : cap(src.cap)
        , threshold(src.threshold)
        , size(src.size)
        , array(new entry<K,T>*[cap])
    {
        for (u32 i = 0; i < cap; ++i) {
            if (src.array[i] != nullptr) {
                array[i] = new entry(src.array[i]->key, src.array[i]->val);
            } else {
                array[i] = nullptr;
            }
        }
    }
    ~table() {
        for (u32 i=0; i < cap; ++i) {
            if (array[i] != nullptr)
                delete array[i];
        }
        delete[] array;
    }

    table<K,T>& operator=(const table<K,T>& src) {
        // clean up the old data and just replace this using new
        for (u32 i=0; i < cap; ++i) {
            if (array[i] != nullptr)
                delete array[i];
        }
        delete[] array;

        cap = src.cap;
        threshold = src.threshold;
        size = src.size;
        array = new entry<K,T>*[cap];

        for (u32 i = 0; i < cap; ++i) {
            if (src.array[i] != nullptr) {
                array[i] = new entry(src.array[i]->key, src.array[i]->val);
            } else {
                array[i] = nullptr;
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
            if (array[i] == nullptr) {
                // no collision; make a new entry
                ++size;
                array[i] = new entry<K,T>(k, v);
                break;
            } else if (array[i]->key == k) {
                // no collision; overwrite previous entry
                array[i]->val = v;
                break;
            }
            // collision; increment index and try again
            i = (i+1) % cap;
        }
        return array[i]->val;
    }

    // returns nullptr when no object is associated to the key
    optional<T*> get(const K& k) const {
        u32 h = hash(k);
        u32 i = h % this->cap;
        // do linear probing
        while(true) {
            if (array[i] == nullptr) {
                // no entry for this key
                return { };
            } else if (array[i]->key == k) {
                // found the key
                return optional(&array[i]->val);
            }
            i = (i+1) % cap;
        }
    }

    bool has_key(const K& k) const {
        u32 h = hash(k);
        u32 i = h % this->cap;
        // do linear probing
        while(true) {
            if (array[i] == nullptr) {
                // no entry for this key
                return false;
            } else if (array[i]->key == k) {
                // found the key
                return true;
            }
            i = (i+1) % cap;
        }
    }

    forward_list<const K*> keys() const {
        forward_list<const K*> res;
        for (u32 i = 0; i < cap; ++i) {
            if(array[i] != nullptr) {
                res.push_front(&array[i]->key);
            }
        }
        return res;
    }

    bool operator==(const table<K,T>& x) const {
        if (this->size != x.size) {
            return false;
        }
        for (u32 i = 0; i < cap; ++i) {
            if (array[i] == nullptr) continue;

            auto k = array[i]->key;
            auto v1 = x.get(k);
            if (!v1.has_value()) {
                return false;
            } else if (**this->get(k) != **v1) {
                return false;
            }
        }
        return true;
    }

};


}

#endif
