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

// Dynamic array type. This is used as a lightweight alternative to std::vector.
// It saves us tens of kilobytes of executable size.
template<typename t>
struct dyn_array {
    u32 capacity;
    u32 size;
    t* data;

    dyn_array()
        : capacity{16}
        , size{0}
        , data{(t*)malloc(sizeof(t)*capacity)} {
    }
    dyn_array(const dyn_array<t>& other)
        : capacity{other.capacity}
        , size{other.size}
        , data{(t*)malloc(size*sizeof(t))} {
        for (u32 i = 0; i < size; ++i) {
            new(&data[i]) t{other[i]};
        }
    }
    dyn_array(dyn_array<t>&& other)
        : capacity{other.capacity}
        , size{other.size}
        , data{other.data} {
        other.capacity = 0;
        other.size = 0;
        other.data = (t*)malloc(0);
    }
    ~dyn_array() {
        for (u32 i = 0; i < size; ++i) {
            data[i].~t();
        }
        free(data);
    }
    dyn_array& operator= (const dyn_array<t>& other) {
        this->~dyn_array();
        capacity = other.capacity;
        size = other.size;
        data = (t*)malloc(size*sizeof(t));
        for (u32 i = 0; i < size; ++i) {
            new(&data[i]) t{other[i]};
        }
        return *this;
    }
    dyn_array& operator= (dyn_array<t>&& other) {
        auto tmpc = capacity;
        capacity = other.capacity;
        other.capacity = tmpc;
        auto tmps = size;
        size = other.size;
        other.size = tmps;
        auto tmp = data;
        data = other.data;
        other.data = tmp;
        return *this;
    }

    // get the size of this array, including the contents of its buffer, in
    // bytes
    size_t mem_size() {
        return sizeof(dyn_array<t>) + capacity*sizeof(t);
    }

    void ensure_capacity(u32 min_cap) {
        if (capacity >= min_cap) {
            return;
        }
        auto new_cap = capacity;
        while (new_cap < min_cap) {
            new_cap *= 2;
        }
        if constexpr (std::is_trivially_copyable<t>::value) {
            data = (t*)realloc(data, new_cap*sizeof(t));
        } else {
            auto old_data = data;
            data = (t*)malloc(new_cap*sizeof(t));
            for (u32 i = 0; i < size; ++i) {
                new(&data[i]) t{std::move(old_data[i])};
            }
            free(old_data);
        }
        capacity = new_cap;
    }
    void push_back(const t& item) {
        ensure_capacity(size + 1);
        new(&data[size]) t{item};
        ++size;
    }
    void push_back(t&& item) {
        ensure_capacity(size + 1);
        new(&data[size]) t{item};
        ++size;
    }

    void resize(u32 new_size) {
        ensure_capacity(new_size);
        for (u32 i = size; i < new_size; ++i) {
            new(&data[i]) t;
        }
        size = new_size;
    }

    inline t& operator[](u32 i) {
        return data[i];
    }
    inline const t& operator[](u32 i) const {
        return data[i];
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
    t* data;

    static_array()
        : size{0}
        , data{new t[size]} {
    }
    // only valid when t has a default constructor and = operator
    static_array(u32 size)
        : size{size}
        , data{new t[size]} {
        for (u32 i = 0; i < size; ++i) {
            new(&data[i]) t;
        }
    }
    // only valid when t has an = operator
    static_array(u32 size, const t& init)
        : size{size}
        , data{new t[size]} {
        for (u32 i = 0; i < size; ++i) {
            new(&data[i]) t{init};
        }
    }
    static_array(const static_array& other) {
        size = other.size;
        data = new t[size];
        for (u32 i = 0; i < size; ++i) {
            data[i] = other.data[i];
        }
    }
    static_array(const static_array&& other)
        : size{other.size}
        , data{other.data} {
        other.size = 0;
        other.data = new t[0];
    }
    ~static_array() {
        for (u32 i = 0; i < size; ++i) {
            data[i].~t();
        }
        delete[] data;
    }

    inline t& operator[](u32 i) {
        return data[i];
    }
    inline const t& operator[](u32 i) const {
        return data[i];
    }

    static_array<t> operator=(const static_array<t>& other) {
        this->~static_array();

        size = other.size;
        data = new t[size];
        for (u32 i = 0; i < size; ++i) {
            new(&data[i]) t{other.data[i]};
        }
        return *this;
    }
    static_array<t> operator=(static_array<t>&& other) {
        // just switch out the fields and let the other destructor take care of
        // deallocating our original array.
        auto tmpc = size;
        size = other.size;
        other.size = tmpc;
        auto tmpd = data;
        data = other.data;
        other.data = tmpd;
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
