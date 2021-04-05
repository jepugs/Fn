#ifndef __FN_GENERATOR_HPP
#define __FN_GENERATOR_HPP

#include "base.hpp"

#include <functional>
#include <iostream>

namespace fn {

template <typename T> class generator {
    std::function<optional<T>()> fun;

public:
    generator()
        : fun([]() -> optional<T> { return { }; })
    { }
    generator(const std::function<optional<T>()>& fun) 
        : fun(fun)
    { }
    generator(std::function<optional<T>()>&& fun)
        : fun(fun)
    { }
    generator(generator<T>& g)
        : fun(g.fun)
    { }
    generator(generator<T>&& g)
        : fun()
    {
        fun.swap(g.fun);
    }

    optional<T> operator()() {
        return fun();
    }

    generator operator+(generator& other) {
        bool first = true;
        auto f = fun;
        std::function snd = other.fun;
        return generator([=]() mutable {
            auto v = f();
            if (first && !v.has_value()) {
                first = false;
                f = snd;
                v = f();
            }
            return v;
        });
    }
    generator operator+(generator&& other) {
        bool first = true;
        auto f = fun;
        std::function snd = (std::function<optional<T>()>&&)other.fun;
        return generator([=]() mutable {
            auto v = f();
            if (first && !v.has_value()) {
                first = false;
                f = snd;
                v = f();
            }
            return v;
        });
    }

    generator& operator=(const generator& other) {
        fun = other.fun;
        return *this;
    }
    generator& operator=(generator&& other) {
        fun.swap(other.fun);
        return *this;
    }

    generator& operator+=(generator<T>& other) {
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
    generator& operator+=(generator<T>&& other) {
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
        generator gen;
    public:
        // default makes the end() iterator
        iterator()
            : val({ })
            , gen([]() -> optional<T> { return { }; })
        { }
        iterator(generator& gen)
            : val(gen.fun())
            , gen(gen)
        { }

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
        bool operator!=(const iterator& other) const {
            return (val.has_value() || other.val.has_value());
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

template<typename T,typename R> generator<T> mk_generator(const R& callable) {
    return generator<T>([callable] { return callable(); });
}

template<typename T> generator<T> generate1(T obj) {
    return generator<T>([obj,done=false]() mutable -> optional<T> {
        if (done) {
            return { };
        }
        done = true;
        return obj;
    });
}

template<typename t, typename r> generator<t> gen_iter(const r& iterable) {
    auto it = iterable.begin();
    auto end = iterable.end();
    return mk_generator([it,end]() mutable -> optional<t> {
            if (it == end) {
                return { };
            }
            return *it++;
        });
}

}

#endif
