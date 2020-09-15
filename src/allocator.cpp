#include "allocator.hpp"

#include <iostream>

namespace fn {

Allocator::Allocator() : objects(), memUsage(0), count(0) { }

Allocator::~Allocator() {
    for (auto o : objects) {
        switch (vTag(o->ptr)) {
        case TAG_CONS:
            dealloc(vCons(o->ptr));
            break;
        case TAG_STR:
            //dealloc(vString(o->ptr));
            break;
        case TAG_OBJ:
            break;
        case TAG_FUNC:
            break;
        case TAG_FOREIGN:
            break;
        default:
            // do nothing
            break;
        }
    }
}

void Allocator::dealloc(Cons* v) {
    delete v;
}

void Allocator::dealloc(FnString* v) {
    delete v;
}

void Allocator::dealloc(Obj* v) {
    delete v;
}

void Allocator::dealloc(Function* v) {
    // TODO: handle upvalues
    delete v;
}

void Allocator::dealloc(ForeignFunc* v) {
    delete v;
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


void Allocator::printStatus() {
    std::cout << "Allocator Information\n";
    std::cout << "=====================\n";
    std::cout << "Memory used (bytes): " << memUsage << '\n';
    std::cout << "Number of objects: " << count << '\n';
}


}
