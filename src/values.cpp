#include "values.hpp"

#include <cmath>
#include "vm.hpp"

namespace fn {

bool value::is_int() const {
    if (!is_num()) {
    }
    auto f = unum();
    return f == (i64)f;
}

value value::operator+(const value& v) const {
    return as_value(unum() + v.unum());
}
value value::operator-(const value& v) const {
    return as_value(unum() - v.unum());
}
value value::operator*(const value& v) const {
    return as_value(unum() * v.unum());
}
value value::operator/(const value& v) const {
    return as_value(unum() / v.unum());
}
value value::operator%(const value& v) const {
    i64 x = (i64)floor(unum());
    f64 frac = unum() - x;
    return as_value((x % (i64)floor(v.unum())) + frac);
}

bool value::operator<(const value& v) const {
    return unum() < v.unum();
}
bool value::operator>(const value& v) const {
    return unum() > v.unum();
}
bool value::operator<=(const value& v) const {
    return unum() <= v.unum();
}
bool value::operator>=(const value& v) const {
    return unum() >= v.unum();
}


value value::pow(const value& expt) const {
    return as_value(std::pow(unum(),expt.unum()));
}

optional<obj_header*> value::header() const {
    if (is_cons()) {
        return &ucons()->h;
    } else if (is_string()) {
        return &ustring()->h;
    } else if (is_table()) {
        return &utable()->h;
    } else if (is_function()) {
        return &ufunction()->h;
    }
    return { };
}


// natural log (unsafe & safe)
value v_ulog(value a) {
    return as_value(log(a.unum()));
}

// floor and ceiling functions
value v_ufloor(value a) {
    return as_value(floor(a.unum()));
}
value v_uceil(value a){
    return as_value(ceil(a.unum()));
}

// ordering
bool v_ult(value a, value b) {
    return a < b;
}
bool v_ugt(value a, value b) {
    return a > b;
}
bool v_ule(value a, value b) {
    return a <= b;
}
bool v_uge(value a, value b) {
    return a >= b;
}

symbol_id v_usym_id(value sym) {
    return sym.usym_id();
}

// list functions

// these only work on conses, not on empty
value v_uhead(value x) {
    return x.ucons()->head;
}
value v_utail(value x) {
    return x.ucons()->tail;
}


// table functions
forward_list<value> v_utab_get_keys(value obj) {
    forward_list<value> res;
    for (auto x : obj.utable()->contents.keys()) {
        res.push_front(*x);
    }
    return res;
}

bool v_utab_has_key(value obj, value key) {
    return obj.utable()->contents.has_key(key);
}

value v_utab_get(value obj, value key) {
    auto x = obj.utable()->contents.get(key);
    if (x.has_value()) {
        return **x;
    } else {
        return V_NULL;
    }
}

void v_utab_set(value obj, value key, value v) {
    obj.utable()->contents.insert(key, v);
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

const symbol* symbol_table::intern(const string& str) {
    auto v = find(str);
    if (v.has_value()) {
        return *v;
    } else {
        u32 id = by_id.size();
        symbol s{ .id=id, .name=str };
        by_id.push_back(s);
        by_name.insert(str,s);
        return &(by_id[by_id.size() - 1]);
    }
}

symbol_id symbol_table::intern_id(const string& str) {
    return intern(str)->id;
}

bool symbol_table::is_internal(const string& str) const {
    return by_name.get(str).has_value();
}

inline optional<const symbol*> symbol_table::find(const string& str) const {
    auto v = by_name.get(str);
    if (v.has_value()) {
        return *v;
    }
    return { };
}

local_address function_stub::get_upvalue(i32 offset) {
    for (local_address i = 0; i < num_upvals; ++i) {
        if (upvals[i] == offset) {
            // found the upvalue
            return i;
        }
    }
    // add a new upvalue
    upvals.push_back(offset);

    return num_upvals++;
}

function::function(function_stub* stub, bool gc)
    : h{as_value(this),gc}
    , stub{stub} {
    upvals = new upvalue_cell*[stub->num_upvals];
    if (stub->req_args < stub->pos_params.size()) {
        init_vals = new value[stub->pos_params.size() - stub->req_args];
    }
}

// TODO: use refcount on upvalues
function::~function() {
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
        return *ustring() == *v.ustring();
    case TAG_CONS:
        return v_uhead(*this) == v_uhead(v)
            && v_utail(*this) == v_utail(v);
    case TAG_TABLE:
        return utable()->contents == v.utable()->contents;

    // default behavior when raw values are inequal is to return false. note:
    // this default case accounts for numbers, symbols, true, false, null,
    // empty, functions (both foreign and native), and namespaces (which are
    // defined to be globally unique).
    default:
        return false;
    }
    
}

bool value::operator!=(const value& v) const {
    return !(*this==v);
}

// FIXME: should probably pick a better hash function
template<> u32 hash<value>(const value& v) {
    auto tag = v_tag(v);
    switch (tag) {
    case TAG_NUM:
    case TAG_STRING:
    case TAG_NULL:
    case TAG_TRUE:
    case TAG_FALSE:
    case TAG_EMPTY:
        return hash(v_to_string(v, nullptr)) + tag;
    case TAG_SYM:
        return v_usym_id(v) + tag;
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
        return std::to_string(v.unum());
    case TAG_CONS:
        res = "[ ";
        for (value x = v; v_tag(x) == TAG_CONS; x = v_utail(x)) {
            res += v_to_string(v_uhead(x),symbols) + " ";
        }
        return res + "]";
    case TAG_STRING:
        return "\"" + string{v.ustring()->data} + "\"";
    case TAG_TABLE:
        // TODO: recursively track which objects we've descended into
        res = "{ ";
        t = v.utable();
        for (auto k : t->contents.keys()) {
            res += v_to_string(*k,symbols) + " "
                + v_to_string(**t->contents.get(*k),symbols) + " ";
            if (res.size() >= 69) {
                res += "...";
                break;
            }
        }
        return res + "}";
    case TAG_FUNC:
        return "<function>";
    case TAG_NULL:
        return "null";
    case TAG_TRUE:
        return "true";
    case TAG_FALSE:
        return "false";
    case TAG_EMPTY:
        return "[]";
    case TAG_SYM:
        return "'" + (*symbols)[v_usym_id(v)].name;
    }
    return "<unprintable-object>";
}


}
