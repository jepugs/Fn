#include "values.hpp"

#include <cmath>
#include "vm.hpp"

namespace fn {

optional<obj_header*> value::header() const {
    if (is_cons()) {
        return &vcons(*this)->h;
    } else if (is_string()) {
        return &vstring(*this)->h;
    } else if (is_table()) {
        return &vtable(*this)->h;
    } else if (is_function()) {
        return &vfunction(*this)->h;
    }
    return { };
}

obj_header::obj_header(value ptr, bool gc)
    : ptr{ptr}
    , gc{gc}
    , pin_count{0}
    , mark{false} {
}

cons::cons(value head, value tail, bool gc)
    : h{as_value(this),gc}
    , head{head}
    , tail{tail} {
}

fn_string::fn_string(const string& src, bool gc)
    : h{as_value(this),gc}
    , len{static_cast<u32>(src.size())} {
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, src.c_str(), len);
    data = v;
}
fn_string::fn_string(const char* src, bool gc)
    : h{as_value(this),gc}
    , len{static_cast<u32>(string{src}.size())} {
    string s(src);
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, s.c_str(), len);
    data = v;
}
fn_string::fn_string(const fn_string& src, bool gc)
    : h{as_value(this),gc}
    , len{src.len} {
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, src.data, len);
    data = v;
}
fn_string::~fn_string() {
    delete[] data;
}

string fn_string::as_string() {
    return string{data, len};
}

bool fn_string::operator==(const fn_string& s) const {
    if (len != s.len) {
        return false;
    }
    for (u32 i=0; i < len; ++i) {
        if (data[i] != s.data[i]) {
            return false;
        }
    }
    return true;
}

fn_table::fn_table(bool gc)
    : h{as_value(this),gc}
    , contents{} {
}
symbol_table::symbol_table()
    : by_name{}
    , by_id{} {
}

symbol_id symbol_table::intern(const string& str) {
    auto v = by_name.get(str);
    if (v.has_value()) {
        return v->id;
    } else {
        u32 id = by_id.size();
        symtab_entry s{ .id=id, .name=str };
        by_id.push_back(s);
        by_name.insert(str,s);
        return id;
    }
}

bool symbol_table::is_internal(const string& str) const {
    return by_name.get(str).has_value();
}

string symbol_table::symbol_name(symbol_id sym) const {
    if (sym >= by_id.size()) {
        return "";
    } else {
        return by_id[sym].name;
    }
}

local_address function_stub::add_upvalue(u8 addr, bool direct) {
    upvals.push_back(addr);
    upvals_direct.push_back(direct);
    return num_upvals++;
}

function::function(function_stub* stub, bool gc)
    : h{as_value(this),gc}
    , stub{stub} {
    if (stub == nullptr) {
        return;
    }
    upvals = new upvalue_cell*[stub->num_upvals];
    if (stub->req_args < stub->pos_params.size()) {
        init_vals = new value[stub->pos_params.size() - stub->req_args];
    }
}

// TODO: use refcount on upvalues
function::~function() {
    if (stub == nullptr) {
        return;
    }
    delete[] upvals;
    if (stub->req_args < stub->pos_params.size()) {
        delete[] init_vals;
    }
}

bool value::operator==(const value& v) const {
    if (v_same(*this,v)) {
        return true;
    }

    auto tag = v_tag(*this);
    if (v_tag(v) != tag) {
        return false;
    }
    switch (v_tag(*this)) {
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

// FIXME: should probably pick a better hash function
template<> u32 hash<value>(const value& v) {
    auto tag = v_tag(v);
    switch (tag) {
    case TAG_NUM:
    case TAG_STRING:
    case TAG_NIL:
    case TAG_TRUE:
    case TAG_FALSE:
    case TAG_EMPTY:
        return hash(v_to_string(v, nullptr)) + tag;
    case TAG_SYM:
        return vsymbol(v) + tag;
    case TAG_TABLE:
    case TAG_CONS:
    case TAG_FUNC:
    default:
        return 0;
    }
}

string v_to_string(value v, const symbol_table* symbols) {
    auto tag = v_tag(v);
    string res;
    fn_table* t;
    // TODO: add escaping to strings/characters
    switch(tag) {
    case TAG_NUM:
        return std::to_string(vnumber(v));
    case TAG_CONS:
        res = "[ ";
        for (value x = v; v_tag(x) == TAG_CONS; x = vcons(x)->tail) {
            res += v_to_string(vcons(x)->head,symbols) + " ";
        }
        return res + "]";
    case TAG_STRING:
        return "\"" + string{vstring(v)->data} + "\"";
    case TAG_TABLE:
        // TODO: recursively track which objects we've descended into
        res = "{ ";
        t = vtable(v);
        for (auto k : t->contents.keys()) {
            res += v_to_string(k,symbols) + " "
                + v_to_string(*t->contents.get(k),symbols) + " ";
            if (res.size() >= 69) {
                res += "...";
                break;
            }
        }
        return res + "}";
    case TAG_FUNC:
        return "<function>";
    case TAG_NIL:
        return "null";
    case TAG_TRUE:
        return "true";
    case TAG_FALSE:
        return "false";
    case TAG_EMPTY:
        return "[]";
    case TAG_SYM:
        return "'" + symbols->symbol_name(vsymbol(v));
    }
    return "<unprintable-object>";
}


}
