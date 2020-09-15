#include "init.hpp"

#include "base.hpp"
#include "bytes.hpp"
#include "table.hpp"
#include "values.hpp"

namespace fn {

#define FN_FUN(name) static Value name(Local numArgs, Value* args, VM* vm)

static Value fnEq(Local numArgs, Value* args, VM* vm) {
    if (numArgs == 0) return V_TRUE;

    auto v1 = args[0];
    for (Local i = 1; i < numArgs; ++i) {
        if (v1 != args[i])
            return V_FALSE;
    }
    return V_TRUE;
}

FN_FUN(fnNullQ) {
    return value(args[0] == V_NULL);
}

FN_FUN(fnBool) {
    return value(vTruthy(args[0]));
}

FN_FUN(fnBoolQ) {
    return value(vTag(args[0]) == TAG_BOOL);
}

FN_FUN(fnNot) {
    return value(!vTruthy(args[0]));
}

// FN_FUN(fnNum) {
//     switch(vTag(args[0])) {
//     case TAG_NUM:
//         return args[0];
//     case TAG_STR:
//         try {
//             auto d = stod(*vStr(args[0]));
//             return value(d);
//         } catch(...) { // TODO: this should probably be a runtime warning...
//             vm->runtimeError("String argument to Num does not represent a number."); 
//             return value(0.0);
//         }
//     case TAG_NULL:
//         return value(0.0);
//     case TAG_BOOL:
//         return value((f64)vBool(args[0]));
//     default:
//         vm->runtimeError("Num cannot convert value of the given type.");
//         return value(0.0);
//     }
// }

FN_FUN(fnNumQ) {
    return value(vShortTag(args[0]) == TAG_NUM);
}

FN_FUN(fnIntQ) {
    return value(vShortTag(args[0]) == TAG_NUM
                 && vNum(args[0]) == (u64)vNum(args[0]));
}

static Value fnAdd(Local numArgs, Value* args, VM* vm) {
    f64 res = 0;
    for (Local i = 0; i < numArgs; ++i) {
        if(vShortTag(args[i]) == TAG_NUM) {
            res += vNum(args[i]);
        } else {
            vm->runtimeError("Argument to + not a number: " +
                             vToString(args[i], vm->getBytecode()->getSymbols()));
        }
    }
    return value(res);
}
static Value fnSub(Local numArgs, Value* args, VM* vm) {
    if (numArgs == 0)
        return value(0);
    // TODO: check isNum
    f64 res = vNum(args[0]);
    if (numArgs == 1) {
        return { .num = -res };
    }
    for (Local i = 1; i < numArgs; ++i) {
        if(vShortTag(args[i]) == TAG_NUM) {
            res -= vNum(args[i]);
        } else {
            vm->runtimeError("Argument to - not a number: " +
                             vToString(args[i], vm->getBytecode()->getSymbols()));
        }
    }
    return value(res);
}
static Value fnMul(Local numArgs, Value* args, VM* vm) {
    f64 res = 1.0;
    for (Local i = 0; i < numArgs; ++i) {
        if(vShortTag(args[i]) == TAG_NUM) {
            res *= vNum(args[i]);
        } else {
            vm->runtimeError("Argument to * not a number: " +
                             vToString(args[i], vm->getBytecode()->getSymbols()));
        }
    }
    return value(res);
}
static Value fnDiv(Local numArgs, Value* args, VM* vm) {
    if (numArgs == 0)
        return value(1.0);
    // TODO: check for 0
    f64 res = vNum(args[0]);
    if (numArgs == 1) {
        return value(1/res);
    }
    for (Local i = 1; i < numArgs; ++i) {
        if(vShortTag(args[i]) == TAG_NUM) {
            res /= vNum(args[i]);
        } else {
            vm->runtimeError("Argument to / not a number: " +
                             vToString(args[i], vm->getBytecode()->getSymbols()));
        }
    }
    return value(res);
}

FN_FUN(fnObject) {
    if (numArgs % 2 != 0) {
        vm->runtimeError("Object must have an even number of arguments.");
    }
    // TODO: use allocator
    auto res = vm->getAlloc()->obj();
    for (Local i = 0; i < numArgs; i += 2) {
        vObj(res)->contents.insert(args[i],args[i+1]);
    }
    return res;
}

FN_FUN(fnObjectQ) {
    return value(vShortTag(args[0])==TAG_OBJ);
}

static Value fnPrint(Local numArgs, Value* args, VM* vm) {
    std::cout << vToString(args[0], vm->getBytecode()->getSymbols());
    return V_NULL;
}

static Value fnPrintln(Local numArgs, Value* args, VM* vm) {
    std::cout << vToString(args[0], vm->getBytecode()->getSymbols()) << "\n";
    return V_NULL;
}


void init(VM* vm) {
    vm->addForeign("=", fnEq, 0, true);
    vm->addForeign("null?", fnNullQ, 1, false);
    vm->addForeign("Bool", fnBool, 1, false);
    vm->addForeign("bool?", fnBoolQ, 1, false);
    vm->addForeign("not", fnNot, 1, false);
    //vm->addForeign("Num", fnNum, 1, false);
    vm->addForeign("num?", fnNumQ, 1, false);
    vm->addForeign("int?", fnIntQ, 1, false);
    vm->addForeign("+", fnAdd, 0, true);
    vm->addForeign("-", fnSub, 0, true);
    vm->addForeign("*", fnMul, 0, true);
    vm->addForeign("/", fnDiv, 0, true);
    //vm->addForeign("^", fnPow, 2, false);
    //vm->addForeign("mod", fnMod, 2, false);
    //vm->addForeign("floor", fnFloor, 2, false);
    //vm->addForeign(">", fnGt, 2, true);
    //vm->addForeign("<", fnLt, 2, true);
    //vm->addForeign(">=", fnGe, 2, true);
    //vm->addForeign("<=", fnLe, 2, true);
    vm->addForeign("Object", fnObject, 0, true);
    vm->addForeign("object?", fnObjectQ, 1, false);
    // vm->addForeign("has-key", fnHasKey, 2, false);
    // vm->addForeign("get", fnGet, 2, true);
    //vm->addForeign("get-keys", fnGetKeys, 1, false);
    //vm->addForeign("get-props", fnGetProps, 1, false);
    //vm->addForeign("extend", fnDiv, 1, true);
    //vm->addForeign("List", fnList, 0, true);
    //vm->addForeign("list?", fnListQ, 0, false);
    //vm->addForeign("empty?", fnEmptyQ, 0, false);
    //vm->addForeign("as-list", fnLAsist, 1, false);
    //vm->addForeign("cons", fnDiv, 0, true);
    //vm->addForeign("append", fnDiv, 0, true);
    //vm->addForeign("reverse", fnDiv, 0, true);
    //vm->addForeign("String", fnString, 0, true);
    //vm->addForeign("string?", fnStringQ, 0, false);
    //vm->addForeign("substring", fnSubstring, 3, false);
    //vm->addForeign("hash", fnHash, 1, false);
    vm->addForeign("print", fnPrint, 1, false);
    vm->addForeign("println", fnPrintln, 1, false);
    //vm->addForeign("apply", fnDiv, 2, true);
}

}
