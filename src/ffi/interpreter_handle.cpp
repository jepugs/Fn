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

// value interpreter_handle::v_add(value a, value b) {
//     assert_type(TAG_NUM, a);
//     assert_type(TAG_NUM, b);
//     return as_value(a.num + b.num);
// }

// value interpreter_handle::v_sub(value a, value b) {
//     assert_type(TAG_NUM, a);
//     assert_type(TAG_NUM, b);
//     return as_value(a.num - b.num);
// }

// value interpreter_handle::v_mul(value a, value b) {
//     assert_type(TAG_NUM, a);
//     assert_type(TAG_NUM, b);
//     return as_value(a.num * b.num);
// }

// value interpreter_handle::v_div(value a, value b) {
//     assert_type(TAG_NUM, a);
//     assert_type(TAG_NUM, b);
//     return as_value(a.num / b.num);
// }

// value interpreter_handle::v_abs(value a) {
//     assert_type(TAG_NUM, a);
//     return as_value(fabs(a.num));
// }

// value interpreter_handle::v_strlen(value a) {
//     assert_type(TAG_STRING, a);
//     return as_value((f64)vstring(a)->len);
// }

// value interpreter_handle::v_substr(value a, u32 start) {
//     assert_type(TAG_STRING, a);
//     auto s = vstring(a)->as_string().substr(start);
//     return ws->add_string(s);
// }

value interpreter_handle::string_concat(value l, value r) {
    assert_type(TAG_STRING, l);
    assert_type(TAG_STRING, r);
    auto s = vstring(l)->as_string() + vstring(r)->as_string();
    return ws->add_string(s);
}

value interpreter_handle::list_concat(value l, value r) {
    assert_list(l);
    assert_list(r);
    if (l == V_EMPTY) {
        return r;
    }

    auto res = ws->add_cons(vhead(l), V_EMPTY);
    auto end = res;
    for (auto it = vtail(l); it != V_EMPTY; it = vtail(it)) {
        auto next = ws->add_cons(vhead(it), V_EMPTY);
        vcons(end)->tail = next;
        end = next;
    }
    vcons(end)->tail = r;
    return res;
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
            return vbox_number(i);
        }
    case TAG_EMPTY:
        return vbox_number(0.0);
    case TAG_TABLE:
        return vbox_number((i64)vtable(x)->contents.get_size());
    case TAG_STRING:
        return vbox_number(vstring(x)->len);
    default:
        runtime_error("Can only compute length for lists and tables.");
    }
    return V_NIL;
}

}
