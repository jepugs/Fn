#include "allocator.hpp"

#include <algorithm>
#include <iostream>

namespace fn {

#define COLLECT_TH 4096

Allocator::Allocator()
    : objects(),
      gcEnabled(false),
      toCollect(false),
      memUsage(0),
      collectThreshold(COLLECT_TH),
      count(0),
      getRoots([] { return Generator<Value>(); })
{ }

Allocator::Allocator(std::function<Generator<Value>()> getRoots)
    : objects(),
      gcEnabled(false),
      toCollect(false),
      memUsage(0),
      collectThreshold(COLLECT_TH),
      count(0),
      getRoots(getRoots)
{ }

Allocator::Allocator(Generator<Value> (*getRoots)())
    : objects(),
      gcEnabled(false),
      toCollect(false),
      memUsage(0),
      collectThreshold(COLLECT_TH),
      count(0),
      getRoots([getRoots] { return getRoots(); })
{ }

Allocator::~Allocator() {
    for (auto o : objects) {
        dealloc(o->ptr);
    }
}

void Allocator::dealloc(Value v) {
    if (v.isCons()) {
        memUsage -= sizeof(Cons);
        delete v.ucons();
    } else if (v.isStr()) {
        auto s = v.ustr();
        memUsage -= s->len;
        memUsage -= sizeof(FnString);
        delete s;
    } else if (v.isObj()) {
        memUsage -= sizeof(Obj);
        delete v.uobj();
    } else if (v.isFunc()) {
        memUsage -= sizeof(Function);
        delete v.ufunc();
    } else if (v.isForeign()) {
        memUsage -= sizeof(ForeignFunc);
        delete v.uforeign();
    }
    --count;
}

Generator<Value> Allocator::accessible(Value v) {
    if (v.isCons()) {
        return generate1(v.rhead()) + generate1(v.rtail());
    } else if (v.isObj()) {
        Generator<Value> res;
        for (auto k : v.objKeys()) {
            res += generate1(k);
            res += generate1(v.get(k));
        }
        return res;
    } else if (v.isFunc()) {
        Generator<Value> res;
        auto f = v.ufunc();
        // just need to add the upvalues
        auto m = f->stub->numUpvals;
        for (Local i = 0; i < m; ++i) {
            if (f->upvals[i].refCount == nullptr) continue;
            res += generate1(**f->upvals[i].val);
        }
        return res;
    } else {
        return Generator<Value>();
    }
}

void Allocator::markDescend(ObjHeader* o) {
    if (o->mark || !o->gc)
        // already been here or not managed
        return;
    o->mark = true;
    for (auto q : accessible(o->ptr)) {
        auto h = q.header();
        if (h.has_value()) {
            markDescend(*h);
        }
    }
}

void Allocator::mark() {
    for (auto v : getRoots()) {
        auto h = v.header();
        if (h.has_value()) {
            markDescend(*h);
        }
    }
}

void Allocator::sweep() {
#ifdef GC_DEBUG
    auto origCt = count;
    auto origSz = memUsage;
#endif
    // delete unmarked objects
    std::list<ObjHeader*> moreObjs;
    for (auto h : objects) {
        if (h->mark) {
            moreObjs.push_back(h);
        } else {
            dealloc(h->ptr);
        }
    }
    objects.swap(moreObjs);
    // unmark remaining objects
    for (auto h : objects) {
        h->mark = false;
    }
#ifdef GC_DEBUG
    auto ct = origCt - count;
    auto sz = origSz - memUsage;
    std::cout << "Swept " << ct << " objects ( " << sz << " bytes)\n";
#endif
}

bool Allocator::gcIsEnabled() {
    return gcEnabled;
}

void Allocator::enableGc() {
    gcEnabled = true;
    if (toCollect) {
        forceCollect();
    }
}

void Allocator::disableGc() {
    gcEnabled = false;
}

void Allocator::collect() {
#ifdef GC_DEBUG
    if (gcEnabled) {
        forceCollect();
    } else {
        toCollect = true;
    }
#else
    if (memUsage >= collectThreshold) {
        if (gcEnabled) {
            forceCollect();
            if (memUsage >= 0.5*collectThreshold) {
                // increase the threshold
                collectThreshold *= 2;
            }
        } else {
            toCollect = true;
        }
    }
#endif
}

void Allocator::forceCollect() {
    // note: assume that objects begin unmarked
#ifdef GC_DEBUG
    std::cout << "Garbage collection beginning (memUsage = "
              << memUsage
              << ", numObjects() = "
              << count
              << " ):\n";
#endif

    mark();
    sweep();
    toCollect = false;
}

Value Allocator::cons(Value hd, Value tl) {
    collect();
    memUsage += sizeof(Cons);
    ++count;

    auto v = new Cons(hd,tl,true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::str(const string& s) {
    collect();
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
    collect();
    memUsage += sizeof(Obj);
    ++count;

    auto v = new Obj(true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::func(FuncStub* stub, const std::function<void (UpvalueSlot*)>& populate) {
    collect();
    memUsage += sizeof(Function);
    ++count;

    auto v = new Function(stub, populate, true);
    objects.push_front(&v->h);
    return value(v);
}

Value Allocator::foreign(Local minArgs, bool varArgs, Value (*func)(Local, Value*, VM*)) {
    collect();
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
    // descend into values
}


}
