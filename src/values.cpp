#include "values.hpp"

namespace fn {

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
        return *vStr(v);
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
