#include "values.hpp"

namespace fn {

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
    auto v = by_name.get(str);
    if (v.has_value()) {
        return v->id;
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

local_address function_stub::add_upvalue(u8 addr, bool direct) {
    upvals.push_back(addr);
    upvals_direct.push_back(direct);
    return num_upvals++;
}

bool value::operator==(const value& v) const {
    if (vsame(*this,v)) {
        return true;
    }

    auto tag = v_tag(*this);
    if (vtag(v) != tag) {
        return false;
    }
    switch (vtag(*this)) {
    case TAG_STRING:
        return *vstring(*this) == *vstring(v);
    case TAG_CONS:
        return vcons(*this)->head == vcons(v)->head
            && vcons(*this)->tail == vcons(v)->tail;
    case TAG_TABLE:
        return vtable(*this)->contents == vtable(v)->contents;

    // default behavior when raw values are inequal is to return false. note:
    // this default case accounts for numbers, symbols, true, false, null,
    // empty, functions (both foreign and native), and namespaces (which are
    // defined to be globally unique).
    default:
        return false;
    }
    
}

bool value::operator!=(const value& v) const {
    return !(*this == v);
}


template<> u64 hash<value>(const value& v) {
    auto tag = v_tag(v);
    switch (tag) {
    case TAG_NUM:
    case TAG_SYM:
    case TAG_NIL:
    case TAG_TRUE:
    case TAG_FALSE:
    case TAG_EMPTY:
        return hash(v.raw);
    case TAG_STRING:
        return hash(string{(char*)vstring(v)->data});
    case TAG_TABLE:
    case TAG_CONS:
    case TAG_FUNC:
    default:
        // FIXME: need I say more?
        return 0;
    }
}

string v_to_string(value v, const symbol_table* symbols, bool code_format) {
    auto tag = v_tag(v);
    string res;
    fn_table* t;
    // TODO: add escaping to strings/characters
    switch(tag) {
    case TAG_NUM:
        {   std::ostringstream os;
            os << std::noshowpoint << vnumber(v);
            return os.str();
        }
    case TAG_CONS:
        {   res = "[" + v_to_string(vhead(v), symbols, code_format);
            auto it = vtail(v);
            for (; it != V_EMPTY; it = vtail(it)) {
                res += " " + v_to_string(vhead(it), symbols, code_format);
            }
            return res + "]";
        }
    case TAG_STRING:
        if (code_format) {
            // TODO: handle escapes
            return "\"" + string{(char*)vstring(v)->data} + "\"";
        } else {
            return string{(char*)vstring(v)->data};
        }
    case TAG_TABLE:
        {   // TODO: recursively track which objects we've descended into
            t = vtable(v);
            auto keys = t->contents.keys();
            if (keys.empty()) {
                return "{}";
            }
            res = "{";
            auto k = keys.front();
            res += v_to_string(k,symbols,code_format) + " "
                + v_to_string(*t->contents.get(k),symbols,code_format);
            keys.pop_front();
            for (auto k : keys) {
                res += " " + v_to_string(k,symbols) + " "
                    + v_to_string(*t->contents.get(k),symbols,code_format);
            }
            return res + "}";
        }
    case TAG_FUNC:
        return "<function>";
    case TAG_NIL:
        return "nil";
    case TAG_TRUE:
        return "true";
    case TAG_FALSE:
        return "false";
    case TAG_EMPTY:
        return "[]";
    case TAG_SYM:
        if (code_format) {
            return "'" + symbols->nice_name(vsymbol(v));
        } else {
            return symbols->nice_name(vsymbol(v));
        }
    }
    return "<unprintable-object>";
}


}
