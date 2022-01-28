#include "values.hpp"

namespace fn {

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


}
