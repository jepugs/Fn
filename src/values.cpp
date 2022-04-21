#include "values.hpp"

#include "alloc.hpp"
#include "gc.hpp"

namespace fn {

bool value::operator==(const value& v) const {
    if (vsame(*this,v)) {
        return true;
    }

    auto tag = vtag(*this);
    switch (tag) {
    case TAG_STRING:
        return vtag(v) == TAG_STRING && *vstring(*this) == *vstring(v);
    case TAG_CONS:
        return vtag(v) == TAG_CONS
            && vcons(*this)->head == vcons(v)->head
            && vcons(*this)->tail == vcons(v)->tail;
    case TAG_TABLE: {
        if (vtag(v) != TAG_TABLE) {
            return false;
        }
        auto tab1 = vtable(*this);
        auto tab2 = vtable(v);
        const auto m = tab1->cap;
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
    case TAG_CONST:
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
            t = vtable(v);
            res += "{";
            auto arr = (value*)t->data->data;
            for (u32 i = 0; i < t->cap; ++i) {
                if (arr[2*i] != V_UNIN) {
                    res += v_to_string(arr[2*i], symbols, code_format);
                    res += " ";
                    res += v_to_string(arr[2*i+1], symbols, code_format);
                    res += " ";
                }
            }
            res += "}";
        }
        return res;
    case TAG_FUNC:
        return "<function>";
    case TAG_CONST:
        switch (vext_tag(v)) {
        case TAG_NIL:
            return "nil";
        case TAG_YES:
            return "yes";
        case TAG_NO:
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
        default:
            break;
        }
        // fall thru
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

value* table_get(fn_table* tab, value k) {
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

value* table_get_linear(fn_table* tab, value k) {
    auto m = 2 * tab->cap;
    auto data = (value*)tab->data->data;
    for (u32 i = 0; i < m; i += 2) {
        if (data[i] == k) {
            return &data[i];
        }
    }
    return nullptr;
}


void table_insert(istate* S, u32 table_pos, u32 key_pos, u32 val_pos) {
    auto tab = vtable(S->stack[table_pos]);
    // grow the table if necessary. This uses a 3/4 threshold
    if (tab->size >= tab->rehash) {
        auto old_cap = tab->cap;
        tab->cap = 2 * tab->cap;
        tab->rehash = tab->cap * 3 / 4;
        auto new_data = alloc_gc_bytes(S, 2*tab->cap*sizeof(value));
        // allocation may trigger garbage collection and move the table we were
        // just working on
        tab = vtable(S->stack[table_pos]);
        auto old_arr = (value*)tab->data->data;
        tab->data = new_data;
        write_guard(get_gc_card_header(&tab->h), &new_data->h);
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
    auto k = S->stack[key_pos];
    auto v = S->stack[val_pos];
    auto x = find_table_slot(tab, k);
    x[0] = k;
    x[1] = v;
    // set the dirty bit
    auto card = get_gc_card_header(&tab->h);
    if (vhas_header(k)) {
        write_guard(card, vheader(k));
    }
    if (vhas_header(v)) {
        write_guard(card, vheader(v));
    }    
}


value get_metatable(istate* S, value obj) {
    if (vis_list(obj)) {
        return S->G->list_meta;
    } else if (vis_string(obj)) {
        return S->G->string_meta;
    } else if (vis_table(obj)) {
        return vtable(obj)->metatable;
    } else {
        return V_NIL;
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
    case TAG_YES:
    case TAG_NO:
        return "bool\n";
    case TAG_UNIN:
        return "<uninitialized>\n";
    default:
        return "<unrecognized>";
    }
}

}
