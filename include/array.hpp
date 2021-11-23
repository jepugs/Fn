#ifndef __FN_ARRAY
#define __FN_ARRAY

#include <cstdlib>

#include "base.hpp"

// Reasons for defining these array data types:
// - vector is not a StandardLayoutType
// - vector checks bounds. We skip the check (and so don't raise exceptions)
// - vector has lots we don't need, so this gives smaller executable size
//   in theory
// - static array lets us automatically dealloc some stuff which is neat

namespace fn {

template<typename t>
struct dyn_array {
    u32 capacity;
    u32 size;
    t* contents;

    dyn_array()
        : capacity{32}
        , size{0}
        , contents{(t*)malloc(sizeof(t)*capacity)} {
    }
    dyn_array(const dyn_array<t>& other)
        : capacity{other.capacity}
        , size{other.size}
        , contents{(t*)malloc(size*sizeof(t))} {
        for (u32 i = 0; i < size; ++i) {
            new(&contents[i]) t{other[i]};
        }
    }
    dyn_array(dyn_array<t>&& other)
        : capacity{other.capacity}
        , size{other.size}
        , contents{other.contents} {
        other.capacity = 0;
        other.size = 0;
        other.contents = (t*)malloc(0);
    }
    ~dyn_array() {
        for (u32 i = 0; i < size; ++i) {
            contents[i].~t();
        }
        free(contents);
    }
    dyn_array& operator= (const dyn_array<t>& other) {
        this->~dyn_array();
        capacity = other.capacity;
        size = other.size;
        contents = (t*)malloc(size*sizeof(t));
        for (u32 i = 0; i < size; ++i) {
            new(&contents[i]) t{other[i]};
        }
        return *this;
    }
    dyn_array& operator= (dyn_array<t>&& other) {
        auto tmpcap = capacity;
        capacity = other.capacity;
        other.capacity = tmpcap;
        auto tmpsz = size;
        size = other.size;
        other.size = tmpsz;
        auto tmpc = contents;
        contents = other.contents;
        other.contents = tmpc;
        return *this;
    }

    void ensure_capacity(u32 min_cap) {
        if (capacity >= min_cap) {
            return;
        }
        auto new_cap = capacity;
        while (new_cap < min_cap) {
            new_cap *= 2;
        }
        contents = (t*)realloc(contents, new_cap*sizeof(t));
        capacity = new_cap;
    }
    void push_back(const t& item) {
        ensure_capacity(size + 1);
        new(&contents[size]) t{item};
        ++size;
    }
    void push_back(t&& item) {
        ensure_capacity(size + 1);
        new(&contents[size]) t{item};
        ++size;
    }

    inline t& operator[](u32 i) {
        return contents[i];
    }
    inline const t& operator[](u32 i) const {
        return contents[i];
    }

    struct iterator {
        u32 i;
        dyn_array<t>* arr;
        t& operator*() {
            return (*arr)[i];
        }
        iterator& operator++() {
            ++i;
            return *this;
        }
        bool operator!=(const iterator& other) const {
            return i != other.i;
        }
    };
    iterator begin() {
        return iterator {.i = 0, .arr=this};
    }
    iterator end() {
        return iterator {.i = size, .arr=this};
    }
};

template<typename t>
struct static_array {
    u32 size;
    t* contents;

    static_array()
        : size{0}
        , contents{new t[size]} {
    }
    // only valid when t has a default constructor and = operator
    static_array(u32 size)
        : size{size}
        , contents{new t[size]} {
        for (u32 i = 0; i < size; ++i) {
            new(&contents[i]) t;
        }
    }
    // only valid when t has an = operator
    static_array(u32 size, const t& init)
        : size{size}
        , contents{new t[size]} {
        for (u32 i = 0; i < size; ++i) {
            new(&contents[i]) t{init};
        }
    }
    static_array(const static_array& other) {
        size = other.size;
        contents = new t[size];
        for (u32 i = 0; i < size; ++i) {
            contents[i] = other.contents[i];
        }
    }
    static_array(const static_array&& other)
        : size{other.size}
        , contents{other.contents} {
        other.size = 0;
        other.contents = new t[0];
    }
    ~static_array() {
        for (u32 i = 0; i < size; ++i) {
            contents[i].~t();
        }
        delete[] contents;
    }

    inline t& operator[](u32 i) {
        return contents[i];
    }
    inline const t& operator[](u32 i) const {
        return contents[i];
    }

    static_array<t> operator=(const static_array<t>& other) {
        this->~static_array();

        size = other.size;
        contents = new t[size];
        for (u32 i = 0; i < size; ++i) {
            new(&contents[i]) t{other.contents[i]};
        }
        return *this;
    }
    static_array<t> operator=(static_array<t>&& other) {
        // just switch out the fields and let the other destructor take care of
        // deallocating our original array.
        auto tmps = size;
        size = other.size;
        other.size = tmps;
        auto tmpc = contents;
        contents = other.contents;
        other.contents = tmpc;
        return *this;
    }

    struct iterator {
        u32 i;
        static_array<t>* arr;
        t& operator*() {
            return (*arr)[i];
        }
        iterator& operator++() {
            ++i;
            return *this;
        }
        bool operator!=(const iterator& other) const {
            return i != other.i;
        }
    };
    iterator begin() {
        return iterator {.i = 0, .arr=this};
    }
    iterator end() {
        return iterator {.i = size, .arr=this};
    }
};

}

#endif
