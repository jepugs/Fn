#include "values.hpp"

#include <cmath>

namespace fn {

void value::error(u64 expected) const {
    throw value_error(expected, *this);
}

value& value::operator+=(const value& v) {
    return *this = *this + v;
}

bool value::is_int() const {
    if (!is_num()) {
        return false;
    }
    auto f = unum();
    return f == i64(f);
}

f64 value::vnum() const {
    if (!is_num()) {
        error(TAG_NUM);
    }
    return this->unum();
}

cons* value::vcons() const {
    if (!is_cons()) {
        error(TAG_CONS);
    }
    return this->ucons();
}

fn_string* value::vstr() const {
    if (!is_str()) {
        error(TAG_STR);
    }
    return this->ustr();
}
object* value::vobj() const {
    if (!is_object()) {
        error(TAG_OBJ);
    }
    return this->uobj();
}
function* value::vfunc() const {
    if (!is_func()) {
        error(TAG_FUNC);
    }
    return this->ufunc();
}
foreign_func* value::vforeign() const {
    if (!is_foreign()) {
        error(TAG_FOREIGN);
    }
    return this->uforeign();
}

value value::operator+(const value& v) const {
    if (!is_num() || !v.is_num()) {
        error(TAG_NUM);
    }
    return as_value(unum() + v.unum());
}
value value::operator-(const value& v) const {
    if (!is_num() || !v.is_num()) {
        error(TAG_NUM);
    }
    return as_value(unum() - v.unum());
}
value value::operator*(const value& v) const {
    if (!is_num() || !v.is_num()) {
        error(TAG_NUM);
    }
    return as_value(unum() * v.unum());
}
value value::operator/(const value& v) const {
    if (!is_num() || !v.is_num()) {
        error(TAG_NUM);
    }
    return as_value(unum() / v.unum());
}

value value::pow(const value& expt) const {
    if (!is_num() || !expt.is_num()) {
        error(TAG_NUM);
    }
    return as_value(std::pow(unum(),expt.unum()));
}

// cons functions
value& value::rhead() const {
    if (!is_cons()){
        error(TAG_CONS);
    }
    return ucons()->head;
}
value& value::rtail() const {
    if (!is_cons()) {
        error(TAG_CONS);
    }
    return ucons()->tail;
}

// str functions
u32 value::str_len() const {
    if (!is_str()) {
        error(TAG_STR);
    }
    return ustr()->len;
}

// obj functions
value& value::get(const value& key) const {
    if (!is_object()) {
        error(TAG_OBJ);
    }
    auto v = uobj()->contents.get(key);
    if (v.has_value()) {
        return **v;
    }
    return uobj()->contents.insert(key, V_NULL);
}
void value::set(const value& key, const value& val) const {
    if (!is_object()) {
        error(TAG_OBJ);
    }
    uobj()->contents.insert(key, val);
}
bool value::has_key(const value& key) const {
    if (!is_object()) {
        error(TAG_OBJ);
    }
    return uobj()->contents.has_key(key);
}
// t_od_o: add unsafe versions of all these accessors (incl. ones above)
forward_list<value> value::obj_keys() const {
    if (!is_object()) {
        error(TAG_OBJ);
    }
    forward_list<value> res;
    for (auto p : uobj()->contents.keys()) {
        res.push_front(*p);
    }
    return res;
}

optional<obj_header*> value::header() const {
    if (is_cons()) {
        return &ucons()->h;
    } else if (is_str()) {
        return &ustr()->h;
    } else if (is_object()) {
        return &uobj()->h;
    } else if (is_func()) {
        return &ufunc()->h;
    } else if (is_foreign()) {
        return &uforeign()->h;
    }
    return { };
}

obj_header::obj_header(value ptr, bool gc)
    : ptr(ptr)
    , gc(gc)
    , mark(false)
{ }

cons::cons(value head, value tail, bool gc)
    : h(as_value(this),gc)
    , head(head)
    , tail(tail)
{ }

fn_string::fn_string(const string& src, bool gc)
    : h(as_value(this),gc)
    , len(src.size())
{
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, src.c_str(), len);
    data = v;
}
fn_string::fn_string(const char* src, bool gc)
    : h(as_value(this),gc)
    , len(string(src).size())
{
    string s(src);
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, s.c_str(), len);
    data = v;
}
fn_string::~fn_string() {
    delete[] data;
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

object::object(bool gc)
    : h(as_value(this),gc)
    , contents()
{ }

function::function(func_stub* stub, const std::function<void (upvalue_slot*)>& populate, bool gc)
    : h(as_value(this),gc)
    , stub(stub)
{
    upvals = new upvalue_slot[stub->num_upvals];
    populate(upvals);
}

// t_od_o: use refcount on upvalues
function::~function() {
    delete[] upvals;
}

foreign_func::foreign_func(local_addr min_args,
                           bool var_args,
                           value (*func)(local_addr, value*, virtual_machine*),
                           bool gc)
    : h(as_value(this),gc)
    , min_args(min_args)
    , var_args(var_args)
    , func(func)
{ }

bool value::operator==(const value& v) const {
    if (v_same(*this,v)) {
        return true;
    }

    auto tag = v_tag(*this);
    if (v_tag(v) != tag) {
        return false;
    }
    switch (v_tag(*this)) {
    case TAG_STR:
        return *v_str(*this) == *v_str(v);
    case TAG_OBJ:
        return v_obj(*this)->contents == v_obj(v)->contents;
    case TAG_CONS:
        // t_od_o: write these
        return false;

    // default behavior when raw values are inequal is to return false.
    // note: this default case accounts for numbers, symbols, true, false, null, empty, and
    // functions (both foreign and native).
    default:
        return false;
    }
    
}

bool value::operator!=(const value& v) const {
    return !(*this==v);
}

// f_ix_me: should probably pick a better hash function
template<> u32 hash<value>(const value& v) {
    auto tag = v_tag(v);
    switch (tag) {
    case TAG_NUM:
    case TAG_STR:
    case TAG_NULL:
    case TAG_BOOL:
    case TAG_EMPTY:
        return hash(v_to_string(v, nullptr)) + tag;
    case TAG_SYM:
        return v_sym_id(v) + tag;
    case TAG_OBJ:
    case TAG_CONS:
        // t_od_o: write these
        return 0;
    case TAG_FUNC:
    case TAG_FOREIGN:
    default:
        return 0;
    }
}

string v_to_string(value v, const symbol_table* symbols) {
    auto tag = v_tag(v);
    string res;
    object* o;
    // t_od_o: add escaping to strings/characters
    switch(tag) {
    case TAG_NUM:
        return std::to_string(v_num(v));
    case TAG_CONS:
        res = "[ ";
        for (value x = v; v_tag(x) == TAG_CONS; x = v_tail(x)) {
            res += v_to_string(v_head(x),symbols) + " ";
        }
        return res + "]";
    case TAG_STR:
        return string(v_str(v)->data);
    case TAG_OBJ:
        // t_od_o: recursively track which objects we've descended into
        res = "{ ";
        o = v_obj(v);
        for (auto k : o->contents.keys()) {
            res += v_to_string(*k,symbols) + " " + v_to_string(**o->contents.get(*k),symbols) + " ";
            if (res.size() >= 69) {
                res += "...";
                break;
            }
        }
        return res + "}";
    case TAG_FUNC:
        return "<function>";
    case TAG_FOREIGN:
        return "<foreign>";
    case TAG_NULL:
        return "null";
    case TAG_BOOL:
        return v_bool(v) ? "true" : "false";
    case TAG_EMPTY:
        return "[]";
    case TAG_SYM:
        return "'" + (*symbols)[v_sym_id(v)].name;
    }
    return "<unprintable-object>";
}


}
