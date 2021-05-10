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

fn_string* value::vstring() const {
    if (!is_string()) {
        error(TAG_STRING);
    }
    return this->ustring();
}
fn_table* value::vtable() const {
    if (!is_table()) {
        error(TAG_TABLE);
    }
    return this->utable();
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
fn_namespace* value::vnamespace() const {
    if (!is_namespace()) {
        error(TAG_NAMESPACE);
    }
    return this->unamespace();
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
u32 value::string_len() const {
    if (!is_string()) {
        error(TAG_STRING);
    }
    return ustring()->len;
}

// table functions
value value::table_get(const value& key) const {
    if (!is_table()) {
        error(TAG_TABLE);
    }
    auto v = utable()->contents.get(key);
    if (v.has_value()) {
        return **v;
    }
    return utable()->contents.insert(key, V_NULL);
}
void value::table_set(const value& key, const value& val) const {
    if (!is_table()) {
        error(TAG_TABLE);
    }
    utable()->contents.insert(key, val);
}
bool value::table_has_key(const value& key) const {
    if (!is_table()) {
        error(TAG_TABLE);
    }
    return utable()->contents.has_key(key);
}
// TODO: add unsafe versions of all these accessors (incl. ones above)
forward_list<value> value::table_keys() const {
    if (!is_table()) {
        error(TAG_TABLE);
    }
    forward_list<value> res;
    for (auto p : utable()->contents.keys()) {
        res.push_front(*p);
    }
    return res;
}

// namespace functions
optional<value> value::namespace_get(symbol_id name) const {
    if (!is_namespace()) {
        error(TAG_NAMESPACE);
    }
    return unamespace()->get(name);
}
void value::namespace_set(symbol_id name, const value& val) const {
    if (!is_namespace()) {
        error(TAG_NAMESPACE);
    }
    return unamespace()->set(name, val);
}
bool value::namespace_has_name(symbol_id name) const {
    if (!is_namespace()) {
        error(TAG_NAMESPACE);
    }
    return unamespace()->contents.has_key(name);
}
// TODO: add unsafe versions of all these accessors (incl. ones above)
forward_list<symbol_id> value::namespace_names() const {
    if (!is_namespace()) {
        error(TAG_NAMESPACE);
    }
    forward_list<symbol_id> res;
    for (auto p : unamespace()->contents.keys()) {
        res.push_front(*p);
    }
    return res;
}

optional<value> value::get(const value& key) const {
    if(is_namespace()) {
        if(!key.is_sym()) {
            error(TAG_SYM);
        }
        return namespace_get(v_sym_id(key));
    } else {
        return table_get(key);
    }
}

optional<obj_header*> value::header() const {
    if (is_cons()) {
        return &ucons()->h;
    } else if (is_string()) {
        return &ustring()->h;
    } else if (is_table()) {
        return &utable()->h;
    } else if (is_func()) {
        return &ufunc()->h;
    } else if (is_foreign()) {
        return &uforeign()->h;
    }
    return { };
}

obj_header::obj_header(value ptr, bool gc)
    : ptr{ptr}
    , gc{gc}
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

u8 func_stub::get_upvalue(local_addr slot, bool direct) {
    for (local_addr i = 0; i < num_upvals; ++i) {
        auto u = upvals[i];
        if (u.slot == slot && u.direct == direct) {
            // found the upvalue
            return i;
        }
    }
    // add a new upvalue
    upvals.push_back({ .slot=slot, .direct=direct });

    return num_upvals++;
}

function::function(func_stub* stub,
                   const std::function<void (upvalue_slot*,value*)>& populate,
                   bool gc)
    : h{as_value(this),gc}
    , stub{stub} {
    upvals = new upvalue_slot[stub->num_upvals];
    if (stub->optional_index < stub->positional.size()) {
        init_vals = new value[stub->positional.size() - stub->optional_index];
    } else {
        init_vals = nullptr;
    }
    populate(upvals, init_vals);
}

// TODO: use refcount on upvalues
function::~function() {
    delete[] upvals;
    if (stub->optional_index < stub->positional.size()) {
        delete[] init_vals;
    }
}

foreign_func::foreign_func(local_addr min_args,
                           bool var_args,
                           value (*func)(local_addr, value*, virtual_machine*),
                           bool gc)
    : h{as_value(this),gc}
    , min_args{min_args}
    , var_args{var_args}
    , func{func} {
}

fn_namespace::fn_namespace(bool gc)
    : h{as_value(this),gc}
    , contents{} {
}

optional<value> fn_namespace::get(symbol_id sym) {
    auto x = contents.get(sym);
    if (x.has_value()) {
        return **x;
    } else {
        return std::nullopt;
    }
}

void fn_namespace::set(symbol_id sym, const value& v) {
    contents.insert(sym, v);
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
        return *ustring() == *v.vstring();
    case TAG_CONS:
        return rhead() == v.rhead() && rtail() == v.rtail();
        return false;
    case TAG_TABLE:
        return utable()->contents == v.vtable()->contents;

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
        return v_sym_id(v) + tag;
    case TAG_TABLE:
    case TAG_CONS:
        // TODO: write these
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
    fn_table* o;
    // TODO: add escaping to strings/characters
    switch(tag) {
    case TAG_NUM:
        return std::to_string(v_num(v));
    case TAG_CONS:
        res = "[ ";
        for (value x = v; v_tag(x) == TAG_CONS; x = v_tail(x)) {
            res += v_to_string(v_head(x),symbols) + " ";
        }
        return res + "]";
    case TAG_STRING:
        return string{v_string(v)->data};
    case TAG_TABLE:
        // TODO: recursively track which objects we've descended into
        res = "{ ";
        o = v_table(v);
        for (auto k : o->contents.keys()) {
            res += v_to_string(*k,symbols) + " "
                + v_to_string(**o->contents.get(*k),symbols) + " ";
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
    case TAG_TRUE:
        return "true";
    case TAG_FALSE:
        return "false";
    case TAG_EMPTY:
        return "[]";
    case TAG_SYM:
        return "'" + (*symbols)[v_sym_id(v)].name;
    }
    return "<unprintable-object>";
}


}
