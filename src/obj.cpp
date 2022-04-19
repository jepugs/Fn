#include "allocator.hpp"
#include "base.hpp"
#include "obj.hpp"

namespace fn {

void init_gc_header(gc_header* dest, u8 type, u32 size) {
    if (dest == nullptr) {
        dest = new gc_header;
    }
    new(dest) gc_header {
        .type = type,
        .size = size,
        .age = 0
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

code_info* instr_loc(function_stub* stub, u32 pc) {
    for (u64 i = stub->ci_length; i > 0; --i) {
        if (stub->ci_arr[i-1].start_addr <= pc) {
            return &stub->ci_arr[i-1];
        }
    }
    // this is safe since the first location is always added when the function
    // is created.
    // FIXME: or is it?
    return &stub->ci_arr[0];
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
        symtab_entry s{id, new string{str}, hash(vbox_symbol(id))};
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

u64 symbol_table::get_hash(symbol_id sym) const {
    if (sym >= by_id.size) {
        return hash(vbox_symbol(sym));
    } else {
        return by_id[sym].hash_val;
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
