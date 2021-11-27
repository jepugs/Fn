#include "fnffi.h"

#include "base.hpp"
#include "ffi/fn_handle.hpp"
#include "values.hpp"

using namespace fn;

bool fn_handle::failed() {
    return err->happened();
}

void fn_handle::error(const string& message) {
    set_fault(err, origin, "ffi", message);
}

void fn_handle::assert_type(u64 tag, value v) {
    if (v_tag(v) != tag) {
        error("Type check failed.");
    }
}

void fn_handle::assert_list(u64 tag, value v) {
    if (v != V_EMPTY || v_tag(v) != TAG_CONS) {
        error("Type check failed.");
    }
}

value fn_handle::add_string(const char* str) {
    return ws->add_strgin(str);
}

value fn_handle::substr(value a, u32 start) {
    auto str = vstring(a);
    auto len = str->len - start;
}

extern "C" {



}
