#include "values.hpp"

namespace fn {

ObjHeader::ObjHeader(Value ptr, bool gc) : ptr(ptr), gc(gc), dirty(false) { }

Cons::Cons(Value head, Value tail, bool gc) : h(value(this),gc), head(head), tail(tail) { }

FnString::FnString(const string& src, bool gc) : h(value(this),gc), len(src.size()) {
    data = new char[len+1];
    data[len] = '\0';
    std::memcpy(data, src.c_str(), len);
}
FnString::FnString(const char* src, bool gc) : h(value(this),gc) {
    string s(src);
    len = s.size();
    data = new char[len+1];
    data[len] = '\0';
    std::memcpy(data, s.c_str(), len);
}
FnString::~FnString() {
    delete data;
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

Function::Function(FuncStub* stub, const std::function<void (UpvalueSlot**)>& populate, bool gc)
    : h(value(this),gc), stub(stub) {
    upvals = new UpvalueSlot*[stub->numUpvals];
    populate(upvals);
}
// TODO: use refcount on upvalues
Function::~Function() {
    // for (u32 i = 0; i < stub->upvals; ++i) {
    //     if(--upvals[i]->refCount == 0) {
    //         delete upvals[i];
    //     }
    // }
    delete upvals;
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
