#include "values.hpp"

#include <cmath>

namespace fn {

void Value::error(u64 expected) const {
    throw ValueError(expected, *this);
}

Value& Value::operator+=(const Value& v) {
    return *this = *this + v;
}

bool Value::isInt() const {
    if (!isNum()) {
        return false;
    }
    auto f = unum();
    return f == i64(f);
}

f64 Value::num() const {
    if (!isNum()) {
        error(TAG_NUM);
    }
    return this->unum();
}

Cons* Value::cons() const {
    if (!isCons()) {
        error(TAG_CONS);
    }
    return this->ucons();
}

FnString* Value::str() const {
    if (!isStr()) {
        error(TAG_STR);
    }
    return this->ustr();
}
Obj* Value::obj() const {
    if (!isObj()) {
        error(TAG_OBJ);
    }
    return this->uobj();
}
Function* Value::func() const {
    if (!isFunc()) {
        error(TAG_FUNC);
    }
    return this->ufunc();
}
ForeignFunc* Value::foreign() const {
    if (!isForeign()) {
        error(TAG_FOREIGN);
    }
    return this->uforeign();
}

Value Value::operator+(const Value& v) const {
    if (!isNum() || !v.isNum()) {
        error(TAG_NUM);
    }
    return value(unum() + v.unum());
}
Value Value::operator-(const Value& v) const {
    if (!isNum() || !v.isNum()) {
        error(TAG_NUM);
    }
    return value(unum() - v.unum());
}
Value Value::operator*(const Value& v) const {
    if (!isNum() || !v.isNum()) {
        error(TAG_NUM);
    }
    return value(unum() * v.unum());
}
Value Value::operator/(const Value& v) const {
    if (!isNum() || !v.isNum()) {
        error(TAG_NUM);
    }
    return value(unum() / v.unum());
}

Value Value::pow(const Value& expt) const {
    if (!isNum() || !expt.isNum()) {
        error(TAG_NUM);
    }
    return value(std::pow(unum(),expt.unum()));
}

// cons functions
Value& Value::rhead() const {
    if (!isCons()){
        error(TAG_CONS);
    }
    return ucons()->head;
}
Value& Value::rtail() const {
    if (!isCons()) {
        error(TAG_CONS);
    }
    return ucons()->tail;
}

// str functions
u32 Value::strLen() const {
    if (!isStr()) {
        error(TAG_STR);
    }
    return ustr()->len;
}

// obj functions
Value& Value::get(const Value& key) const {
    if (!isObj()) {
        error(TAG_OBJ);
    }
    auto v = uobj()->contents.get(key);
    if (v.has_value()) {
        return **v;
    }
    return uobj()->contents.insert(key, V_NULL);
}
void Value::set(const Value& key, const Value& val) const {
    if (!isObj()) {
        error(TAG_OBJ);
    }
    uobj()->contents.insert(key, val);
}
bool Value::hasKey(const Value& key) const {
    if (!isObj()) {
        error(TAG_OBJ);
    }
    return uobj()->contents.hasKey(key);
}


ObjHeader::ObjHeader(Value ptr, bool gc) : ptr(ptr), gc(gc), dirty(false) { }

Cons::Cons(Value head, Value tail, bool gc) : h(value(this),gc), head(head), tail(tail) { }

FnString::FnString(const string& src, bool gc) : h(value(this),gc), len(src.size()) {
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, src.c_str(), len);
    data = v;
}
FnString::FnString(const char* src, bool gc) : h(value(this),gc), len(string(src).size()) {
    string s(src);
    auto v = new char[len+1];
    v[len] = '\0';
    std::memcpy(v, s.c_str(), len);
    data = v;
}
FnString::~FnString() {
    delete[] data;
}

bool FnString::operator==(const FnString& s) const {
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

Obj::Obj(bool gc) : h(value(this),gc), contents() { }

Function::Function(FuncStub* stub, const std::function<void (UpvalueSlot*)>& populate, bool gc)
    : h(value(this),gc), stub(stub) {
    upvals = new UpvalueSlot[stub->numUpvals];
    populate(upvals);
}

// TODO: use refcount on upvalues
Function::~Function() {
    delete[] upvals;
}

ForeignFunc::ForeignFunc(Local minArgs, bool varArgs, Value (*func)(Local, Value*, VM*), bool gc)
    : h(value(this),gc), minArgs(minArgs), varArgs(varArgs), func(func) { }

bool Value::operator==(const Value& v) const {
    if (vSame(*this,v)) {
        return true;
    }

    auto tag = vTag(*this);
    if (vTag(v) != tag) {
        return false;
    }
    switch (vTag(*this)) {
    case TAG_STR:
        return *vStr(*this) == *vStr(v);
    case TAG_OBJ:
        return vObj(*this)->contents == vObj(v)->contents;
    case TAG_CONS:
        // TODO: write these
        return false;

    // Default behavior when raw values are inequal is to return false.
    // note: this default case accounts for numbers, symbols, true, false, null, empty, and
    // functions (both foreign and native).
    default:
        return false;
    }
    
}

// FIXME: should probably pick a better hash function
template<> u32 hash<Value>(const Value& v) {
    auto tag = vTag(v);
    switch (tag) {
    case TAG_NUM:
    case TAG_STR:
    case TAG_NULL:
    case TAG_BOOL:
    case TAG_EMPTY:
        return hash(vToString(v, nullptr)) + tag;
    case TAG_SYM:
        return vSymId(v) + tag;
    case TAG_OBJ:
    case TAG_CONS:
        // TODO: write these
        return 0;
    case TAG_FUNC:
    case TAG_FOREIGN:
    default:
        return 0;
    }
}

string vToString(Value v, SymbolTable* symbols) {
    auto tag = vTag(v);
    string res;
    Obj* o;
    // TODO: add escaping to strings/characters
    switch(tag) {
    case TAG_NUM:
        return std::to_string(vNum(v));
    case TAG_CONS:
        res = "[ ";
        for (Value x = v; vTag(x) == TAG_CONS; x = vTail(x)) {
            res += vToString(vHead(x),symbols) + " ";
        }
        return res + "]";
    case TAG_STR:
        return string(vStr(v)->data);
    case TAG_OBJ:
        // TODO: recursively track which objects we've descended into
        res = "{ ";
        o = vObj(v);
        for (auto k : o->contents.keys()) {
            res += vToString(*k,symbols) + " " + vToString(**o->contents.get(*k),symbols) + " ";
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
        return vBool(v) ? "true" : "false";
    case TAG_EMPTY:
        return "[]";
    case TAG_SYM:
        return "'" + (*symbols)[vSymId(v)].name;
    }
    return "<unprintable-object>";
}


}
