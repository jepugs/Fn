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

value interpreter_handle::vadd(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num + b.num);
}

value interpreter_handle::vsub(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num - b.num);
}

value interpreter_handle::vmul(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num * b.num);
}

value interpreter_handle::vdiv(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(a.num / b.num);
}

value interpreter_handle::vabs(value a) {
    assert_type(TAG_NUM, a);
    return as_value(fabs(a.num));
}

value interpreter_handle::vmod(value a, value b) {
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

value interpreter_handle::vpow(value a, value b) {
    assert_type(TAG_NUM, a);
    assert_type(TAG_NUM, b);
    return as_value(pow(a.num,b.num));
}

value interpreter_handle::vexp(value a) {
    assert_type(TAG_NUM, a);
    return as_value(exp(a.num));
}

value interpreter_handle::vlog(value a) {
    assert_type(TAG_NUM, a);
    return as_value(log(a.num));
}

}
