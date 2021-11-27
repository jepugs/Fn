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

value fn_handle::add_string(const char* str) {
    return ((working_set*)ws)->add_string(str);
}

value fn_handle::add_string(const string& str) {
    return ((working_set*)ws)->add_string(str);
}

value fn_handle::add_string(u32 len) {
    return ((working_set*)ws)->add_string(len);
}

value fn_handle::add_cons(value hd, value tl) {
    return ((working_set*)ws)->add_cons(hd, tl);
}

value fn_handle::intern(const char* str) {
    return vbox_symbol(((vm_thread*)vm)->get_symtab()->intern(str));
}

value fn_handle::gensym() {
    return vbox_symbol(((vm_thread*)vm)->get_symtab()->gensym());
}

value fn_handle::add_table() {
    return ((working_set*)ws)->add_table();
}

value fn_handle::substr(value a, u32 start) {
    auto str = vstring(a);
    auto len = str->len - start;
    if (len < 0) {
        return add_string("");
    } else {
        // the null terminator is placed automatically
        auto res = vstring(add_string(len));
        memcpy(res->data, &str->data[start], len);
        return vbox_string(res);
    }
}

value fn_handle::substr(value a, u32 start, u32 len) {
    auto str = vstring(a);
    if (len < 0) {
        return add_string("");
    } else {
        if (str->len < start + len) {
            len = str->len - start;
        }
        // the null terminator is placed automatically
        auto res = new fn_string{len};
        memcpy(res->data, &str->data[start], len);
        return vbox_string(res);
    }
}

string fn_handle::as_string(value a) {
    return v_to_string(a, ((vm_thread*)vm)->get_symtab());
}

value fn_handle::string_concat(value l, value r) {
    auto s = vstring(l)->as_string() + vstring(r)->as_string();
    return add_string(s);
}

value fn_handle::symname(value a) {
    auto id = vsymbol(a);
    auto s = ((vm_thread*)vm)->get_symtab()->symbol_name(id);
    return add_string(s);
}

value fn_handle::list_concat(value l, value r) {
    if (l == V_EMPTY) {
        return r;
    }

    auto res = add_cons(vhead(l), V_EMPTY);
    auto end = res;
    for (auto it = vtail(l); it != V_EMPTY; it = vtail(it)) {
        auto next = add_cons(vhead(it), V_EMPTY);
        vcons(end)->tail = next;
        end = next;
    }
    vcons(end)->tail = r;
    return res;
}

value fn_handle::table_concat(value l, value r) {
    auto res = add_table();
    for (auto k : vtable(l)->contents.keys()) {
        vset(res, k, vget(l, k));
    }
    for (auto k : vtable(r)->contents.keys()) {
        vset(res, k, vget(r, k));
    }
    return res;
}


}
