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
        .start_addr = to->code.size,
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
