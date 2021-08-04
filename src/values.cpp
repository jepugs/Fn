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
    } else if (is_namespace()) {
        return &unamespace()->h;
    } else if (is_foreign()) {
        return &uforeign()->h;
    }
    return { };
}

f64 v_num(vm_handle vm, value v) {
    if (!v.is_num()) {
        runtime_error(vm, err_type, "Expected number.");
    }
    return v.unum();
}
bool v_bool(vm_handle vm, value v) {
    if (!v.is_bool()) {
        runtime_error(vm, err_type, "Expected bool.");
    }
    return v.ubool();
}
fn_string* v_string(vm_handle vm, value v) {
    if (!v.is_string()) {
        runtime_error(vm, err_type, "Expected string.");
    }
    return v.ustring();
}
cons* v_cons(vm_handle vm, value v) {
    if (!v.is_cons()) {
        runtime_error(vm, err_type, "Expected cons.");
    }
    return v.ucons();
}
fn_table* v_table(vm_handle vm, value v) {
    if (!v.is_table()) {
        runtime_error(vm, err_type, "Expected table.");
    }
    return v.utable();
}
function* v_function(vm_handle vm, value v) {
    if (!v.is_function()) {
        runtime_error(vm, err_type, "Expected function.");
    }
    return v.ufunction();
}
foreign_func* v_foreign(vm_handle vm, value v) {
    if (!v.is_foreign()) {
        runtime_error(vm, err_type, "Expected foreign function.");
    }
    return v.uforeign();
}
fn_namespace* v_namespace(vm_handle vm, value v) {
    if (!v.is_namespace()) {
        runtime_error(vm, err_type, "Expected namespace.");
    }
    return v.unamespace();
}

// functions to create memory managed objects
value alloc_string(vm_handle vm, const string& str) {
    return vm->get_alloc()->add_string(str);
}
value alloc_string(vm_handle vm, const char* str) {
    return vm->get_alloc()->add_string(str);
}
value alloc_cons(vm_handle vm, value head, value tail) {
    return vm->get_alloc()->add_cons(head, tail);
}
value alloc_table(vm_handle vm) {
    return vm->get_alloc()->add_table();
}

// generate a symbol with a unique ID
// TODO
//value v_gensym(vm_handle vm);

// safe arithmetic operations
value v_plus(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "plus: not a number.");
    }
    return a + b;
}
value v_minus(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "minus: not a number.");
    }
    return a - b;
}
value v_times(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "times: not a number.");
    }
    return a * b;
}
value v_div(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "div: not a number.");
    }
    return a / b;
}
value v_pow(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "pow: not a number.");
    }
    return a.pow(b);
}
value v_mod(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "mod: not a number.");
    }
    return a % b;
}


// these overloads allow us to operate on values with plain f64's
value v_plus(vm_handle vm, value a, f64 b) {
    return v_plus(vm, a, as_value(b));
}
value v_minus(vm_handle vm, value a, f64 b) {
    return v_minus(vm, a, as_value(b));
}
value v_times(vm_handle vm, value a, f64 b) {
    return v_times(vm, a, as_value(b));
}
value v_div(vm_handle vm, value a, f64 b) {
    return v_div(vm, a, as_value(b));
}
value v_pow(vm_handle vm, value a, f64 b) {
    return v_pow(vm, a, as_value(b));
}
value v_mod(vm_handle vm, value a, f64 b) {
    return v_mod(vm, a, as_value(b));
}

// natural log (unsafe & safe)
value v_ulog(value a) {
    return as_value(log(a.unum()));
}
value v_log(vm_handle vm, value a) {
    if (!a.is_num()) {
        runtime_error(vm, err_type, "log: not a number.");
    }
    return v_ulog(a);
}

// floor and ceiling functions
value v_ufloor(value a) {
    return as_value(floor(a.unum()));
}
value v_uceil(value a){
    return as_value(ceil(a.unum()));
}

value v_floor(vm_handle vm, value a) {
    if (!a.is_num()) {
        runtime_error(vm, err_type, "floor: not a number.");
    }
    return v_ufloor(a);
}
value v_ceil(vm_handle vm, value a) {
    if (!a.is_num()) {
        runtime_error(vm, err_type, "ceil: not a number.");
    }
    return v_uceil(a);
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

bool v_lt(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "lt: not a number.");
    }
    return v_ult(a,b);
}
bool v_gt(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "gt: not a number.");
    }
    return v_ugt(a,b);
}
bool v_le(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "le: not a number.");
    }
    return v_ule(a,b);
}
bool v_ge(vm_handle vm, value a, value b) {
    if (!a.is_num() || !b.is_num()) {
        runtime_error(vm, err_type, "ge: not a number.");
    }
    return v_uge(a,b);
}

// string functions
value v_ustrlen(value str) {
    return as_value((i64)str.ustring()->len);
}
value v_strlen(vm_handle vm, value str) {
    if (!str.is_string()) {
        runtime_error(vm, err_type, "strlen: not a string.");
    }
    return v_ustrlen(str);
}

// symbol functions
const symbol* v_lookup_symbol(vm_handle vm, value sym) {
    auto& symtab = vm->get_symtab();
    auto id = v_usym_id(sym);
    return &symtab[id];
}
value v_intern(vm_handle vm, value name) {
    if (!name.is_string()) {
        runtime_error(vm, err_type, "intern: name must be a string.");
    }
    auto& symtab = vm->get_symtab();
    return as_value(symtab.intern(name.ustring()->as_string()));
}
value v_intern(vm_handle vm, const string& name) {
    auto& symtab = vm->get_symtab();
    return as_value(symtab.intern(name));
}
value v_intern(vm_handle vm, const char* name) {
    return v_intern(vm, string{name});
}

string v_usym_name(vm_handle vm, value sym) {
    return v_lookup_symbol(vm, sym)->name;
}
symbol_id v_usym_id(value sym) {
    return sym.usym_id();
}
string v_sym_name(vm_handle vm, value sym) {
    if (!sym.is_symbol()) {
        runtime_error(vm, err_type, "sym_name: not a symbol.");
    }
    return v_usym_name(vm, sym);
}

symbol_id v_sym_id(vm_handle vm, value sym) {
    if (!sym.is_symbol()) {
        runtime_error(vm, err_type, "sym_id: not a symbol.");
    }
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

// generates error on cons
value v_head(vm_handle vm, value x) {
    if (!x.is_cons()) {
        runtime_error(vm, err_type, "head: object must be a cons.");
    }
    return v_uhead(x);
}
// this works on empty
value v_tail(vm_handle vm, value x) {
    if (x.is_empty()) {
        return V_EMPTY;
    } else if (!x.is_cons()) {
        runtime_error(vm, err_type, "tail: object must be a cons.");
    }
    return v_utail(x);
}

// table/namespace functions
forward_list<value> v_utab_get_keys(value obj) {
    forward_list<value> res;
    for (auto x : obj.utable()->contents.keys()) {
        res.push_front(*x);
    }
    return res;
}
forward_list<value> v_uns_get_keys(value obj) {
    forward_list<value> res;
    for (auto x : obj.unamespace()->contents.keys()) {
        res.push_front(as_sym_value(*x));
    }
    return res;
}
forward_list<value> v_tab_get_keys(vm_handle vm, value obj) {
    if (!obj.is_table()) {
        runtime_error(vm, err_type, "tab_get_keys: object must be a table.");
    }
    return v_utab_get_keys(obj);
}
forward_list<value> v_ns_get_keys(vm_handle vm, value obj) {
    if (!obj.is_namespace()) {
        runtime_error(vm, err_type, "ns_get_keys: object must be a namespace.");
    }
    return v_uns_get_keys(obj);
}

bool v_utab_has_key(value obj, value key) {
    return obj.utable()->contents.has_key(key);
}
bool v_uns_has_key(value obj, value key) {
    if (!key.is_symbol()) {
        return false;
    }
    return obj.unamespace()->contents.has_key(v_usym_id(key));
}

bool v_tab_has_key(vm_handle vm, value obj, value key) {
    if (!obj.is_table()) {
        runtime_error(vm, err_type, "tab_has_key: object must be a table.");
    }
    return v_utab_has_key(obj, key);
}

bool v_ns_has_key(vm_handle vm, value obj, value key) {
    if (!obj.is_namespace()) {
        runtime_error(vm, err_type, "ns_has_key: object must be a namespace.");
    }
    return v_uns_has_key(obj, key);
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

value v_uns_get(value obj, value key) {
    auto x = obj.unamespace()->contents.get(v_usym_id(key));
    if (x.has_value()) {
        return **x;
    } else {
        return V_NULL;
    }
}

void v_uns_set(value obj, value key, value v) {
    obj.unamespace()->contents.insert(v_usym_id(key), v);
}

value v_tab_get(vm_handle vm, value obj, value key) {
    if (!key.is_symbol()) {
        runtime_error(vm, err_type, "tab_get: namespace key must be symbol.");
    }
    auto x = v_table(vm, obj)->contents.get(key);
    if (x.has_value()) {
        return **x;
    } else {
        return V_NULL;
    }
}

void v_tab_set(vm_handle vm, value obj, value key, value v) {
    if (!obj.is_table()) {
        runtime_error(vm, err_type, "tab_has_key: object must be a table.");
    }
    v_utab_set(obj, key, v);
}

value v_ns_get(vm_handle vm, value obj, value key) {
    if (!obj.is_namespace()) {
        runtime_error(vm, err_type, "ns_get: object must be a namespace.");
    }
    return v_uns_get(obj, key);
}

void v_ns_set(vm_handle vm, value obj, value key, value v) {
    if (!obj.is_namespace()) {
        runtime_error(vm, err_type, "ns_set: object must be a namespace.");
    }
    if (!key.is_symbol()) {
        runtime_error(vm, err_type, "ns_set: namespace key must be symbol.");
    }
    v_uns_set(obj, key, v);
}

forward_list<value> v_get_keys(vm_handle vm, value obj) {
    if (obj.is_table()) {
        return v_utab_get_keys(obj);
    } else if (obj.is_namespace()) {
        return v_uns_get_keys(obj);
    } else {
        runtime_error(vm, err_type, "get_keys: object must be table or namespace.");
    }
    return forward_list<value>{};
}

value v_get(vm_handle vm, value obj, value key) {
    if (obj.is_table()) {
        return v_utab_get(obj, key);
    } else if (obj.is_namespace()) {
        return v_uns_get(obj, key);
    } else {
        runtime_error(vm, err_type, "get: object must be table or namespace.");
    }
    return V_NULL;
}

void v_set(vm_handle vm, value obj, value key, value v) {
    if (obj.is_table()) {
        v_utab_set(obj, key, v);
    } else if (obj.is_namespace()) {
        v_uns_set(obj, key, v);
    } else {
        runtime_error(vm, err_type, "set: object must be table or namespace.");
    }
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
                           optional<value> (*func)(local_addr, value*, virtual_machine*),
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
    fn_table* t;
    fn_namespace* ns;
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
        return "'" + (*symbols)[v_usym_id(v)].name;
    case TAG_NAMESPACE:
                // TODO: recursively track which objects we've descended into
        res = "<namespace: ";
        ns = v.unamespace();
        for (auto k : ns->contents.keys()) {
            res += v_to_string(as_sym_value(*k),symbols) + " "
                + v_to_string(**ns->contents.get(*k),symbols) + " ";
            if (res.size() >= 69) {
                res += "...";
                break;
            }
        }
        return res + ">";
    }
    return "<unprintable-object>";
}


}
