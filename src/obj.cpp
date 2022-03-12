#include "allocator.hpp"
#include "base.hpp"
#include "obj.hpp"

namespace fn {

void init_gc_header(gc_header* dest, u8 type, u32 size) {
    if (dest == nullptr) {
        dest = new gc_header;
    }
    new(dest) gc_header {
        .type=type,
            .size = size
    };
}

bool fn_string::operator==(const fn_string& s) const {
    if (size != s.size) {
        return false;
    }
    for (u32 i=0; i < size; ++i) {
        if (data[i] != s.data[i]) {
            return false;
        }
    }
    return true;
}

void update_code_info(function_stub* to, const source_loc& loc) {
    auto c = new code_info {
        .start_addr = (u32)to->code_size,
        .loc = loc,
        .prev = to->ci_head
    };
    to->ci_head = c;
}

code_info* instr_loc(function_stub* stub, u32 pc) {
    auto c = stub->ci_head;
    while (c != nullptr) {
        if (c->start_addr <= pc) {
            break;
        }
        c = c->prev;
    }
    return c;
}

constant_id push_back_const(istate* S, gc_handle* stub_handle, value v) {
    auto stub = (function_stub*) stub_handle->obj;
    grow_gc_array(S->alloc, &stub->const_arr, &stub->const_cap,
            &stub->const_size, sizeof(value));
    stub = (function_stub*) stub_handle->obj;
    auto id = stub->const_size - 1;
    ((value*)stub->const_arr->data)[id] = v;
    return id;
}

void push_back_code(istate* S, gc_handle* stub_handle, u8 b) {
    auto stub = (function_stub*) stub_handle->obj;
    grow_gc_array(S->alloc, &stub->code, &stub->code_cap, &stub->code_size,
            sizeof(u8));
    stub = (function_stub*) stub_handle->obj;
    stub->code->data[stub->code_size - 1] = b;
}
void push_back_upval(istate* S, gc_handle* stub_handle, bool direct, u8 index) {
    auto stub = (function_stub*) stub_handle->obj;
    grow_gc_array(S->alloc, &stub->upvals, &stub->upvals_cap,
            &stub->upvals_size, sizeof(u8));
    stub = (function_stub*) stub_handle->obj;
    stub->upvals->data[stub->upvals_size - 1] = index;
    grow_gc_array(S->alloc, &stub->upvals_direct, &stub->upvals_direct_cap,
            &stub->upvals_direct_size, sizeof(bool));
    stub = (function_stub*) stub_handle->obj;
    ((bool*)stub->upvals_direct->data)[stub->upvals_direct_size - 1] = direct;
    ++stub->num_upvals;
}

symbol_table::~symbol_table() {
    for (auto x : by_id) {
        delete x.name;
    }
}

symbol_id symbol_table::intern(const string& str) {
    if (next_gensym <= by_id.size) {
        // this is just fatal lmao
        throw std::runtime_error("Symbol table exhausted.");
    }
    auto v = by_name.get2(str);
    if (v) {
        return v->val.id;
    } else {
        u32 id = by_id.size;
        symtab_entry s{id, new string{str}};
        by_id.push_back(s);
        by_name.insert(str,s);
        return id;
    }
}

bool symbol_table::is_internal(const string& str) const {
    return by_name.get(str).has_value();
}

string symbol_table::symbol_name(symbol_id sym) const {
    if (sym >= by_id.size) {
        return "";
    } else {
        return *by_id[sym].name;
    }
}

symbol_id symbol_table::gensym() {
    if (next_gensym <= by_id.size) {
        throw std::runtime_error("Symbol table exhausted.");
    }
    return next_gensym--;
}

bool symbol_table::is_gensym(symbol_id sym) const {
    return sym > next_gensym;
}

string symbol_table::gensym_name(symbol_id sym) const {
    return "#gensym:" + std::to_string((symbol_id)(-1) - sym);
}

string symbol_table::nice_name(symbol_id sym) const {
    if (is_gensym(sym)) {
        return gensym_name(sym);
    } else {
        return symbol_name(sym);
    }
}

}
