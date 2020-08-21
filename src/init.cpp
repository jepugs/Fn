#include "init.hpp"

#include "base.hpp"
#include "bytes.hpp"
#include "table.hpp"
#include "values.hpp"

namespace fn {

// add together numbers
static Value fnAdd(u16 numArgs, Value* args, VM* vm) {
    f64 res = 0;
    for (u32 i = 0; i < numArgs; ++i) {
        if(isNum(args[i])) {
            res += valueNum(args[i]);
        } else {
            //throw FNError("runtime");
        }
    }
    return makeNumValue(res);
}
static Value fnSub(u16 numArgs, Value* args, VM* vm) {
    if (numArgs == 0)
        return makeNumValue(0);
    // TODO: check isNum
    f64 res = valueNum(args[0]);
    if (numArgs == 1) {
        return { .num = -res };
    }
    for (u32 i = 1; i < numArgs; ++i) {
        if(isNum(args[i])) {
            res -= valueNum(args[i]);
        }
    }
    return makeNumValue(res);
}
static Value fnMul(u16 numArgs, Value* args, VM* vm) {
    f64 res = 1.0;
    for (u32 i = 0; i < numArgs; ++i) {
        if(isNum(args[i])) {
            res *= valueNum(args[i]);
        }
    }
    return makeNumValue(res);
}
static Value fnDiv(u16 numArgs, Value* args, VM* vm) {
    if (numArgs == 0)
        return makeNumValue(1.0);
    // TODO: check isNum
    f64 res = valueNum(args[0]);
    if (numArgs == 1) {
        return makeNumValue(1/res);
    }
    for (u32 i = 1; i < numArgs; ++i) {
        if(isNum(args[i])) {
            res /= valueNum(args[i]);
        }
    }
    return makeNumValue(res);
}

static Value fnPrintln(u16 numArgs, Value* args, VM* vm) {
    cout << "here";
    cout << showValue(args[0]) << endl;
    return V_NULL;
}

void init(VM* vm) {
    vm->addForeign("+", fnAdd, 0, true);
    vm->addForeign("-", fnSub, 0, true);
    vm->addForeign("*", fnMul, 0, true);
    vm->addForeign("/", fnDiv, 0, true);
    vm->addForeign("println", fnPrintln, 1, false);
}

}
