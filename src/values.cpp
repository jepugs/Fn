#include "values.hpp"

#include "allocator.hpp"

namespace fn {

bool value::operator==(const value& v) const {
    if (vsame(*this,v)) {
        return true;
    }

    auto tag = vtag(*this);
    if (vtag(v) != tag) {
        return false;
    }
    switch (vtag(*this)) {
    case TAG_STRING:
        return *vstring(*this) == *vstring(v);
    case TAG_CONS:
        return vcons(*this)->head == vcons(v)->head
            && vcons(*this)->tail == vcons(v)->tail;
    case TAG_TABLE: {
        auto tab1 = vtable(*this);
        auto tab2 = vtable(v);
        auto m = tab1->cap;
        if (tab2->cap != m) {
            return false;
        }
        if (tab1->size != tab2->size) {
            return false;
        }
        auto data1 = (value*)tab1->data->data;
        auto data2 = (value*)tab2->data->data;
        for (u32 i = 0; i < 2*m; i += 2) {
            if (data1[i] != data2[i]) {
                return false;
            }
            if (data1[i] != V_UNIN && data1[i+1] != data2[i+1]) {
                return false;
            }
        }
        return true;
    }

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
    auto tag = vtag(v);
    switch (tag) {
    case TAG_NUM:
    case TAG_SYM:
    case TAG_NIL:
    case TAG_TRUE:
    case TAG_FALSE:
    case TAG_EMPTY:
    case TAG_UNIN:
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
    auto tag = vtag(v);
    string res;
    fn_table* t;
    // TODO: add escaping to strings/characters
    switch(tag) {
    case TAG_NUM:
        {   std::ostringstream os;
            auto n = vnumber(v);
            if (n == (u64)n) {
                os << (u64)n;
            } else {
                os << std::noshowpoint << vnumber(v);
            }
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
        {   // TODO: recursively print objects
            //t = vtable(v);
            // auto keys = t->contents.keys();
            // if (keys.empty()) {
            //     return "{}";
            // }
            // res = "{";
            // auto k = keys.front();
            // res += v_to_string(k,symbols,code_format) + " "
            //     + v_to_string(*t->contents.get(k),symbols,code_format);
            // keys.pop_front();
            // for (auto k : keys) {
            //     res += " " + v_to_string(k,symbols) + " "
            //         + v_to_string(*t->contents.get(k),symbols,code_format);
            // }
            // return res + "}";
            return "{}";
        }
    case TAG_FUNC:
        return "<function>";
    case TAG_NIL:
        return "nil";
    case TAG_TRUE:
        return "yes";
    case TAG_FALSE:
        return "no";
    case TAG_EMPTY:
        return "[]";
    case TAG_UNIN:
        return "<uninitialized>";
    case TAG_SYM:
        if (code_format) {
            return "'" + symbols->nice_name(vsymbol(v));
        } else {
            return symbols->nice_name(vsymbol(v));
        }
    }
    return "<unprintable-object>";
}

static value* find_table_slot(fn_table* tab, value k) {
    auto h = hash(k);
    auto m = 2 * tab->cap;
    auto start = 2 * (h % tab->cap);
    auto data = (value*)tab->data->data;
    for (u32 i = start; i < m; i += 2) {
        if (data[i] == V_UNIN
                || data[i] == k) {
            return &data[i];
        }
    }
    // restart search from the beginning of the tree
    for (u32 i = 0; i < start; ++i) {
        if (data[i] == V_UNIN
                || data[i] == k) {
            return &data[i];
        }
    }
    return nullptr;
}

value* table_get(istate* S, fn_table* tab, value k) {
    auto h = hash(k);
    auto m = 2 * tab->cap;
    auto start = 2 * (h % tab->cap);
    auto data = (value*)tab->data->data;
    for (u32 i = start; i < m; i += 2) {
        if (data[i] == V_UNIN) {
            return nullptr;
        } else if (data[i] == k) {
            return &data[i];
        }
    }
    // restart search from the beginning of the tree
    for (u32 i = 0; i < start; ++i) {
        if (data[i] == V_UNIN) {
            return nullptr;
        } else if (data[i] == k) {
            return &data[i];
        }
    }
    return nullptr;
}

void table_set(istate* S, fn_table* tab, value k, value v) {
    // grow the table if necessary. This uses a 3/4 threshold
    if (tab->size >= tab->rehash) {
        auto old_cap = tab->cap;
        tab->cap = 2 * tab->cap;
        tab->rehash = tab->cap * 3 / 4;
        auto old_arr = (value*)tab->data->data;
        tab->data = alloc_gc_bytes(S->alloc, 2*tab->cap*sizeof(value));
        tab->size = 0;

        // initialize new array
        auto m = 2 * tab->cap;
        for (u32 i = 0; i < m; i += 2) {
            ((value*)tab->data->data)[i] = V_UNIN;
        }

        // insert old elements
        m = 2 * old_cap;
        for (u32 i = 0; i < m; i += 2) {
            if (old_arr[i] != V_UNIN) {
                auto x = find_table_slot(tab, old_arr[i]);
                x[0] = old_arr[i];
                x[1] = old_arr[i+1];
            }
        }
    }
    auto x = find_table_slot(tab, k);
    x[0] = k;
    x[1] = v;
    // set the dirty bit
    auto card = get_gc_card(&tab->h);
    if (vhas_header(k)) {
        write_guard(card, vheader(k));
    }
    if (vhas_header(v)) {
        write_guard(card, vheader(v));
    }
}

string type_string(value v) {
    switch (vtag(v)) {
    case TAG_STRING:
        return "string\n";
    case TAG_CONS:
    case TAG_EMPTY:
        return "list\n";
    case TAG_TABLE:
        return "table\n";
    case TAG_FUNC:
        return "function\n";
    case TAG_SYM:
        return "symbol\n";
    case TAG_NIL:
        return "nil\n";
    case TAG_TRUE:
    case TAG_FALSE:
        return "bool\n";
    case TAG_UNIN:
        return "<uninitialized>\n";
    default:
        return "<unrecognized>";
    }
}

}
