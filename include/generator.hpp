#ifndef __FN_GENERATOR_HPP
#define __FN_GENERATOR_HPP

#include "base.hpp"

#include <functional>
#include <iostream>

namespace fn {

template <typename T> class Generator {
    std::function<optional<T>()> fun;

public:
    Generator() : fun([] { return std::nullopt; } ) { }
    Generator(const std::function<optional<T>()>& fun) : fun(fun) { }
    Generator(std::function<optional<T>()>&& fun) : fun(fun) { }
    Generator(optional<T> (*fptr)()) : fun([fptr] { return fptr(); } ) { }
    Generator(Generator<T>& g) : fun(g.fun) { }
    Generator(Generator<T>&& g) : fun() {
        fun.swap(g.fun);
    }

    optional<T> operator()() {
        return fun();
    }

    Generator operator+(Generator& other) {
        bool first = true;
        auto f = fun;
        std::function snd = other.fun;
        return Generator([=]() mutable {
            auto v = f();
            if (first && !v.has_value()) {
                first = false;
                f = snd;
                v = f();
            }
            return v;
        });
    }
    Generator operator+(Generator&& other) {
        bool first = true;
        auto f = fun;
        std::function snd = (std::function<optional<T>()>&&)other.fun;
        return Generator([=]() mutable {
            auto v = f();
            if (first && !v.has_value()) {
                first = false;
                f = snd;
                v = f();
            }
            return v;
        });
    }

    Generator& operator=(const Generator& other) {
        fun = other.fun;
        return *this;
    }
    Generator& operator=(Generator&& other) {
        fun.swap(other.fun);
        return *this;
    }

    Generator& operator+=(Generator<T>& other) {
        fun = [f=this->fun,this,g=other.fun]() mutable {
            auto v = f();
            if (v.has_value()) {
                return v;
            }
            this->fun.swap(g);
            return this->fun();
        };
        return *this;
    }
    Generator& operator+=(Generator<T>&& other) {
        fun = [done=false,f=this->fun,this,g{std::move(other.fun)}]() mutable {
            auto v = f();
            if (done || v.has_value()) {
                return v;
            }
            done = true;
            f = g;
            return f();
        };
        return *this;
    }

    class iterator {
        optional<T> val;
        Generator gen;
    public:
        // default makes the end() iterator
        iterator()
            : val(std::nullopt), gen([] { return std::nullopt; }) { }
        iterator(Generator& gen) : val(gen.fun()), gen(gen) { }

        iterator& operator++() {
            val = gen();
            return *this;
        }
        T& operator*() {
            return *val;
        }
        // equality can only happen when both iterators are at the end
        bool operator==(const iterator& other) const {
            return !(val.has_value() || other.val.has_value());
        }

        using iterator_category = std::input_iterator_tag;
        using difference_type = void;
        using value_type = T;
        using pointer = T*;
        using reference = T&;
    };

    iterator begin() {
        return iterator(*this);
    }
    iterator end() {
        return iterator();
    }
};

template<typename T,typename R> Generator<T> generator(const R& callable) {
    return Generator<T>([callable] { return callable(); });
}

template<typename T> Generator<T> generate1(T obj) {
    return Generator<T>([obj,done=false]() mutable -> optional<T> {
        if (done) {
            return { };
        }
        done = true;
        return obj;
    });
}

template<typename T, typename R> Generator<T> genIter(const R& iterable) {
    auto it = iterable.begin();
    auto end = iterable.end();
    return Generator([it,end]() mutable -> optional<T> {
            if (it == end) {
                return { };
            }
            return *it++;
        });
}

}

#endif
