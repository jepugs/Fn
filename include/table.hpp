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

// hash table entry
template <typename K, typename T> struct Entry {
    const K key;
    T val;

    Entry(const K& k, T& v) : key(k), val(v) { }
};

/// hash table with string keys. Uses linear probing.
template <typename K, typename T> class Table {
private:
    u32 cap;
    u32 threshold;
    u32 size;
    Entry<K,T> **array;

    // Increase the capacity by a factor of 2. This involves recomputing all hashes
    void increaseCap() {
        auto *prev = array;
        auto oldCap = cap;

        // initialize a new array
        cap *= 2;
        threshold = (u32)(REHASH_THRESHOLD * this->cap);
        array = new Entry<K,T>*[this->cap];
        for (u32 i =0; i < cap; ++i) {
            array[i] = nullptr;
        }

        // insert the old data, deleting old entries as we go
        for (u32 i=0; i<oldCap; ++i) {
            if (prev[i] != nullptr) {
                insert(prev[i]->key, prev[i]->val);
                delete prev[i];
            }
        }
        delete[] prev;
    }

public:
    Table(u32 initCap=32)
        : cap(initCap)
    {
        threshold = (u32)(REHASH_THRESHOLD * initCap);
        size = 0;
        array = new Entry<K,T>*[initCap];
        for (u32 i =0; i < initCap; ++i) {
            array[i] = nullptr;
        }
    }
    Table(const Table<K,T>& src)
        : cap(src.cap)
        , threshold(src.threshold)
        , size(src.size)
        , array(new Entry<K,T>*[cap])
    {
        for (u32 i = 0; i < cap; ++i) {
            if (src.array[i] != nullptr) {
                array[i] = new Entry(src.array[i]->key, src.array[i]->val);
            } else {
                array[i] = nullptr;
            }
        }
    }
    ~Table() {
        for (u32 i=0; i < cap; ++i) {
            if (array[i] != nullptr)
                delete array[i];
        }
        delete[] array;
    }

    Table<K,T>& operator=(const Table<K,T>& src) {
        // clean up the old data and just replace this using new
        for (u32 i=0; i < cap; ++i) {
            if (array[i] != nullptr)
                delete array[i];
        }
        delete[] array;

        cap = src.cap;
        threshold = src.threshold;
        size = src.size;
        array = new Entry<K,T>*[cap];

        for (u32 i = 0; i < cap; ++i) {
            if (src.array[i] != nullptr) {
                array[i] = new Entry(src.array[i]->key, src.array[i]->val);
            } else {
                array[i] = nullptr;
            }
        }

        return *this;
    }

    u32 getSize() {
        return size;
    }

    // insert/overwrite a new entry
    T& insert(const K& k, T v) {
        if (size >= threshold) {
            increaseCap();
        }

        u32 h = hash<K>(k);
        u32 i = h % cap;
        while(true) {
            if (array[i] == nullptr) {
                // no collision; make a new entry
                ++size;
                array[i] = new Entry<K,T>(k, v);
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

    bool hasKey(const K& k) const {
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

    bool operator==(const Table<K,T>& x) const {
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
