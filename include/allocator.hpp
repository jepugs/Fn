#ifndef __FN_ALLOCATOR_HPP
#define __FN_ALLOCATOR_HPP

// TODO: move this into the cmake file
#define GC_DEBUG

#include "base.hpp"
#include "generator.hpp"
#include "values.hpp"

#include <functional>
#include <list>

namespace fn {


class Allocator {
private:
    // note: we guarantee that every pointer in this array is actually memory managed by this
    // garbage collector
    std::list<ObjHeader*> objects;
    // flag used to determine garbage collector behavior. Starts out false to allow initialization
    bool gcEnabled;
    // if true, garbage collection will automatically run when next enabled
    bool toCollect;
    u64 memUsage;
    // gc is invoked when memUsage > collectThreshold. collectThreshold is increased if memUsage >
    // 0.5*collectThreshold after a collection.
    u64 collectThreshold;
    // number of objects
    u32 count;

    std::function<Generator<Value>()> getRoots;

    void dealloc(Value v);

    // get a list of objects accessible from the given value
    //forward_list<ObjHeader*> accessible(Value v);
    Generator<Value> accessible(Value v);

    void markDescend(ObjHeader* o);

    void mark();
    void sweep();

public:
    Allocator();
    Allocator(std::function<Generator<Value>()> getRoots);
    Allocator(Generator<Value> (*getRoots)());
    ~Allocator();

    u64 memoryUsed();
    u32 numObjects();

    bool gcIsEnabled();
    // enable/disable the garbage collector
    void enableGc();
    void disableGc();
    // invoke the gc if enough memory is used
    void collect();
    // invoke the gc no matter what
    void forceCollect();

    Value cons(Value hd, Value tl);
    Value str(const string& s);
    Value str(const char* s);
    Value obj();
    Value func(FuncStub* stub, const std::function<void (UpvalueSlot*)>& populate);
    Value foreign(Local minArgs, bool varArgs, Value (*func)(Local, Value*, VM*));

    void printStatus();
};

}

#endif
