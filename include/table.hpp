#ifndef __FN_TABLE_HPP
#define __FN_TABLE_HPP

#include "base.hpp"

namespace fn {

using namespace std;

const float REHASH_THRESHOLD = 0.3;

// string hashing function
inline u32 hashString(string s) {
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
template <typename T> struct Entry {
    string key;
    T val;

    Entry(string k, T v) : key(k), val(v) { }
};

/// hash table with string keys. Uses linear probing.
template <typename T> class Table {
private:
    u32 cap;
    u32 threshold;
    u32 size;
    Entry<T> **array;

    // Increase the capacity by a factor of 2. This involves recomputing all hashes
    void increaseCap() {
        auto *prev = array;
        auto oldCap = cap;

        // initialize a new array
        cap *= 2;
        threshold = (u32)(REHASH_THRESHOLD * this->cap);
        array = new Entry<T>*[this->cap];
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
        delete prev;
    }

public:
    Table(u32 initCap=32) {
        cap = initCap;
        threshold = (u32)(REHASH_THRESHOLD * initCap);
        size = 0;
        array = new Entry<T>*[initCap];
        for (u32 i =0; i < initCap; ++i) {
            array[i] = nullptr;
        }
    }
    ~Table() {
        for (u32 i=0; i < cap; ++i) {
            if (array[i] != nullptr)
                delete array[i];
        }
        delete array;
    }

    // insert/overwrite a new entry
    void insert(string k, T v) {
        if (size >= threshold) {
            increaseCap();
        }

        u32 h = hashString(k);
        u32 i = h % cap;
        while(true) {
            if (array[i] == nullptr) {
                // no collision; make a new entry
                ++size;
                array[i] = new Entry<T>(k, v);
                break;
            } else if (array[i]->key == k) {
                // no collision; overwrite previous entry
                array[i]->val = v;
                break;
            }
            // collision; increment index and try again
            i = (i+1) % cap;
        }
    }

    // returns nullptr when no object is associated to the key
    T *get(string k) {
        u32 h = hashString(k);
        u32 i = h % cap;
        // do linear probing
        while(true) {
            if (array[i] == nullptr) {
                // no entry for this key
                return nullptr;
            } else if (array[i]->key == k) {
                // found the key
                return &(array[i]->val);
            }
            i = (i+1) % cap;
        }
    }
};


}

#endif
