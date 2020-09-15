#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

#include "base.hpp"
#include "values.hpp"

#include <list>

namespace fn {


class Allocator {
private:
    // note: we guarantee that every pointer in this array is actually
    std::list<ObjHeader*> objects;
    u64 memUsage;
    u32 count;

    void dealloc(Cons* v);
    void dealloc(FnString* v);
    void dealloc(Obj* v);
    void dealloc(Function* v);
    void dealloc(ForeignFunc* v);

public:
    Allocator();
    ~Allocator();

    u64 memoryUsed();
    u32 numObjects();

    Value cons(Value hd, Value tl);
    Value str(const string& s);
    Value str(const char* s);
    Value obj();
    Value func(FuncStub* stub, const std::function<void (UpvalueSlot**)>& populate);
    Value foreign(Local minArgs, bool varArgs, Value (*func)(Local, Value*, VM*));

    void printStatus();
};

}

#endif
