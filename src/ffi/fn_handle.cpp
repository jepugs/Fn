#include "ffi/fn_handle.hpp"

// things we don't want in the header
#include "allocator.hpp"
#include "vm.hpp"

namespace fn {


void fn_handle::error(const string& message) {
    set_fault(err, origin, "ffi", message);
}

void fn_handle::assert_type(u64 tag, value v) {
    if (vtag(v) != tag) {
        error("Value does not have asserted type.");
    }
}

void fn_handle::assert_list(value v) {
    if (vtag(v) != TAG_CONS && v != V_EMPTY) {
        error("Value does not have asserted type.");
    }
}

void fn_handle::assert_integer(value v) {
    if (vtag(v) != TAG_NUM || vnumber(v) != (i64)vnumber(v)) {
        error("Value is not an integer.");
    }
}

value fn_handle::push_string(const string& str) {
    return stack->push_string(str);
}

value fn_handle::make_string(local_address offset, const string& str) {
    return stack->make_string(stack->get_pointer() - offset - 1, str);
}

value fn_handle::push_cons(value hd, value tl) {
    return stack->push_cons(hd, tl);
}

value fn_handle::make_cons(local_address offset, value hd, value tl) {
    return stack->make_cons(stack->get_pointer() - offset - 1, hd, tl);
}

value fn_handle::intern(const char* str) {
    return vbox_symbol(((vm_thread*)vm)->get_symtab()->intern(str));
}

value fn_handle::gensym() {
    return vbox_symbol(((vm_thread*)vm)->get_symtab()->gensym());
}

value fn_handle::push_table() {
    return stack->push_table();
}

value fn_handle::peek(local_address offset) {
    return stack->peek(0);
}

void fn_handle::push(value v) {
    stack->push(v);
}

void fn_handle::pop() {
    stack->pop();
}

value fn_handle::substr(local_address offset, value a, u32 start) {
    auto str = vstring(a);
    auto len = str->len - start;
    if (len < 0) {
        return stack->push_string("");
    } else {
        // the null terminator is placed automatically
        auto sub = string{str->data}.substr(start);
        return stack->push_string(sub);
    }
}

value fn_handle::substr(local_address offset, value a, u32 start, u32 len) {
    auto str = vstring(a);
    if (len < 0) {
        return push_string("");
    } else {
        if (str->len < start + len) {
            len = str->len - start;
        }
        // the null terminator is placed automatically
    }
    auto sub = string{str->data}.substr(start);
    return stack->make_string(offset, sub);
}

string fn_handle::as_string(value a) {
    return v_to_string(a, ((vm_thread*)vm)->get_symtab());
}

// value fn_handle::string_concat(value l, value r) {
//     auto s = vstring(l)->as_string() + vstring(r)->as_string();
//     return add_string(s);
// }

value fn_handle::symname(local_address offset, value a) {
    auto id = vsymbol(a);
    auto s = ((vm_thread*)vm)->get_symtab()->symbol_name(id);
    return make_string(offset, s);
}

value fn_handle::list_concat(local_address offset, value l, value r) {
    if (l == V_EMPTY) {
        push(r);
        return peek();
    }

    auto res = make_cons(offset, vhead(l), V_EMPTY);
    // auto end = res;
    // for (auto it = vtail(l); it != V_EMPTY; it = vtail(it)) {
    //     auto next = push_cons(vhead(it), V_EMPTY);
    //     vcons(end)->tail = next;
    //     end = next;
    //     pop();
    // }
    // vcons(end)->tail = r;
    return res;
}

// value fn_handle::table_concat(value l, value r) {
//     auto res = add_table();
//     for (auto k : vtable(l)->contents.keys()) {
//         vset(res, k, vget(l, k));
//     }
//     for (auto k : vtable(r)->contents.keys()) {
//         vset(res, k, vget(r, k));
//     }
//     return res;
// }


}
