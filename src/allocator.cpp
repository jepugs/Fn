#include "allocator.hpp"

#include <iostream>

namespace fn {

Allocator::Allocator() : objects(), memUsage(0), count(0) { }

Allocator::~Allocator() {
    for (auto o : objects) {
        dealloc(o->ptr);
    }
}

void Allocator::dealloc(Value v) {
    if (v.isCons()) {
        delete v.ucons();
    } else if (v.isStr()) {
        delete v.ustr();
    } else if (v.isObj()) {
        delete v.uobj();
    } else if (v.isFunc()) {
        delete v.ufunc();
    } else if (v.isForeign()) {
        delete v.uforeign();
    }
}

Value Allocator::cons(Value hd, Value tl) {
    memUsage += sizeof(Cons);
    ++count;

    auto v = new Cons(hd,tl,true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::str(const string& s) {
    memUsage += sizeof(FnString);
    // also add size of the string's payload
    memUsage += s.size();
    ++count;

    auto v = new FnString(s, true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::str(const char* s) {
    return str(string(s));
}

Value Allocator::obj() {
    memUsage += sizeof(Obj);
    ++count;

    auto v = new Obj(true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::func(FuncStub* stub, const std::function<void (UpvalueSlot**)>& populate) {
    memUsage += sizeof(Function);
    ++count;

    auto v = new Function(stub, populate, true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::foreign(Local minArgs, bool varArgs, Value (*func)(Local, Value*, VM*)) {
    memUsage += sizeof(ForeignFunc);
    ++count;

    auto v = new ForeignFunc(minArgs, varArgs, func, true);
    objects.push_front(&v->h);
    return value(v);
}

void Allocator::printStatus() {
    std::cout << "Allocator Information\n";
    std::cout << "=====================\n";
    std::cout << "Memory used (bytes): " << memUsage << '\n';
    std::cout << "Number of objects: " << count << '\n';
}


}
