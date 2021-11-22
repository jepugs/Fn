#include <cmath>

#include "ffi/interpreter_handle.hpp"

#include "interpret.hpp"

namespace fn {

void interpreter_handle::runtime_error(const string& msg) {
    auto v = (vm_thread*)inter;
    v->runtime_error(func_name + ": " + msg);
}

void interpreter_handle::assert_type(u64 tag, value v) {
    if (v_tag(v) != tag) {
        runtime_error("Value does not have asserted type.");
    }
}
void interpreter_handle::assert_list(value v) {
    if (v_tag(v) != TAG_CONS && v_tag(v) != TAG_EMPTY) {
        runtime_error("Value is not a list.");
    }
}

value interpreter_handle::v_add(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num + b.num);
}

value interpreter_handle::v_sub(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num - b.num);
}

value interpreter_handle::v_mul(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num * b.num);
}

value interpreter_handle::v_div(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num / b.num);
}

value interpreter_handle::v_abs(value a) {
    assert_type(TAG_NUM, a);
    return as_value(fabs(a.num));
}

value interpreter_handle::v_mod(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    auto x = a.num, y = b.num;
    u64 x_int = (u64)x;
    u64 y_int = (u64)y;
    if (y != y_int) {
        runtime_error("modulus must be an integer");
    }
    return as_value((f64)(x_int % y_int) + (x - x_int));
}

value interpreter_handle::v_pow(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(pow(a.num,b.num));
}

value interpreter_handle::v_exp(value a) {
    assert_type(TAG_NUM, a);
    return as_value(exp(a.num));
}

value interpreter_handle::v_log(value a) {
    assert_type(TAG_NUM, a);
    return as_value(log(a.num));
}

value interpreter_handle::v_head(value a) {
    assert_type(TAG_CONS, a);
    return vcons(a)->head;
}

value interpreter_handle::v_tail(value a) {
    if (a.is_empty()) {
        return V_EMPTY;
    } else if (!a.is_cons()) {
        runtime_error("Value does not have legal type.");
    }
    return vcons(a)->tail;
}

value interpreter_handle::v_cons(value hd, value tl) {
    if (!tl.is_cons() && !tl.is_empty()) {
        runtime_error("Value does not have legal type.");
    }
    return ws->add_cons(hd, tl);
}

value interpreter_handle::v_nth(i64 n, value lst) {
    if (lst.is_empty()) {
        return V_NIL;
    }
    assert_type(TAG_CONS, lst);
    while (n != 0) {
        if (lst.is_empty()) {
            return V_NIL;
        }
        lst = vcons(lst)->tail;
        --n;
    }
    return vcons(lst)->head;
}

value interpreter_handle::v_length(value x) {
    switch (v_tag(x)) {
    case TAG_CONS:
        {
            i64 i = 1;
            auto c = vcons(x)->tail;
            for (; c != V_EMPTY; c = vcons(c)->tail) {
                ++i;
            }
            return as_value(i);
        }
    case TAG_EMPTY:
        return as_value(0);
    case TAG_TABLE:
        return as_value((i64)vtable(x)->contents.get_size());
    default:
        runtime_error("Can only compute length for lists and tables.");
    }
    return V_NIL;
}

}
