// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "allocator.hpp"
#include "base.hpp"
#include "table.hpp"
#include "values.hpp"

#include <filesystem>
#include <forward_list>
#include <vector>

namespace fn {

namespace fs = std::filesystem;

// a linked list structure which associates source code locations to bytecode.
struct BytecodeLoc {
    // maximum 
    u32 maxAddr;
    SourceLoc loc;
    BytecodeLoc *next;
};

// A Bytecode object consists of a symbol table, a constant table, and an array of bytes which
// holds the actual instructions. The idea is that Bytecode instances for fn are roughly analogous
// to object files for C.

// In principle this is almost enough information to link two or more bytecode objects together
// (i.e. by doing the necessary symbol translation + combining the constant tables), however we
// still need a way to adjust the addresses of functions (i.e. a table of function stubs). We'll
// cross that bridge much farther down the line, since we'll also certainly need to add other things
// to the Bytecode object, like debugging and module information.
class Bytecode {
private:
    // instruction array
    u32 cap;
    Addr size;
    u8* data;
    // source code locations list
    BytecodeLoc* locs;
    // pointer to the end of the list
    BytecodeLoc* lastLoc;

    // constants and symbols
    vector<Value> constants;
    SymbolTable symbols;
    // constants which need to be freed in the destructor
    std::list<Value> managedConstants;
    // function stubs
    vector<FuncStub*> functions;

    void ensureCapacity(u32 newCap);

public:
    Bytecode();
    ~Bytecode();

    Addr getSize();

    // set the location for writing bytes
    void setLoc(SourceLoc l);
    // get the source code location corresponding to the bytes at addr
    SourceLoc* locationOf(Addr addr);

    // write 1 or two bytes
    void writeByte(u8 b);
    void writeShort(u16 s);
    void writeBytes(const u8* bytes, Addr len);

    // these don't do bounds checking
    u8 readByte(Addr addr);
    u16 readShort(Addr addr);
    void patchShort(Addr addr, u16 s);

    Value getConstant(u16 id);
    u16 numConstants();

    // add a function and set it to start at the current ip
    u16 addFunction(Local arity, bool vararg, Value modId);
    FuncStub* getFunction(u16 id);

    // directly add values to the constants array and return their ID
    ConstId addConst(Value v);
    // create a numerical constant
    ConstId numConst(f64 num);
    // string constants are copied and automatically freed on destruction
    ConstId strConst(const string& str);
    ConstId strConst(const char* str);
    // create a new cons cell and return its 16-bit ID
    ConstId consConst(Value hd, Value tl);
    // equivalent to addConst(symbol(name))
    ConstId symConst(const string& name);

    SymbolTable* getSymbols();
    Value symbol(const string& name);
    u32 symbolID(const string& name);

    inline u8& operator[](Addr addr) {
        return data[addr];
    }

};


// VM stack size limit (per call frame)
constexpr StackAddr STACK_SIZE = 255;

struct OpenUpvalue {
    UpvalueSlot* slot;
    Local pos;
};

struct CallFrame {
    // call frame above this one
    CallFrame *prev;
    // return address
    Addr retAddr;
    // base pointer (i.e. offset from the true bottom of the stack)
    StackAddr bp;
    // The function we're in. nullptr on the top level.
    Function* caller;
    // The number of arguments we need to pop after exiting the current call
    Local numArgs;

    // current stack pointer
    StackAddr sp;
    // currently open upvalues
    forward_list<OpenUpvalue> openUpvals;

    CallFrame(CallFrame* prev, Addr retAddr, StackAddr bp, Function* caller, Local numArgs=0)
        : prev(prev), retAddr(retAddr), bp(bp), caller(caller), numArgs(numArgs),
          sp(numArgs), openUpvals() { }

    // allocate a new call frame as an extension of this one. Assumes the last numArgs values on the
    // stack are arguments for the newly called function.
    CallFrame* extendFrame(Addr retAddr, Local numArgs, Function* caller);

    // open a new upvalue. ptr should point to the stack at pos.
    UpvalueSlot* openUpvalue(Local pos, Value* ptr);
    // decrement the stack pointer and close affected upvalues
    void close(StackAddr n);
    // close all open upvalues regardless of stack position
    void closeAll();
};

// The VM object contains all global state for a single instance of the interpreter.
class VM {
private:
    Bytecode code;
    Obj* module;
    // the namespace hierarchy contains the module hierarchy
    Obj* ns;
    Obj* coreMod;

    Allocator alloc;

    // foreign functions table
    vector<Value> foreignFuncs;

    // working directory
    fs::path wd;

    // instruction pointer and stack
    StackAddr ip;
    CallFrame *frame;
    Value stack[STACK_SIZE];

    // last pop; used to access the result of the last expression
    Value lp;

    // Create and initialize a new module in the ns hierarchy. (This includes setting up the
    // _modinfo and ns variables).
    Obj* initModule(Value moduleId);
    // Search for a module in the ns object. Returns nullptr on failure
    Obj* findModule(Value moduleId);

    // stack operations
    Value pop();
    Value popTimes(StackAddr n);
    void push(Value v);

    // peek relative to the top of the stack
    Value peek(StackAddr offset=0);
    // get a local Value from the current call frame
    Value local(Local l);
    // set a local value
    void setLocal(Local l, Value v);

    // returns the next addr to go to
    Addr call(Local numArgs);
    Addr apply(Local numArgs);

public:
    // initialize the VM with a blank image
    VM();
    ~VM();

    // step a single instruction
    void step();
    // execute instructions (stops if the end of the generated bytecode is reached)
    void execute();
    // get the instruction pointer
    Addr getIp();

    // get the last popped value (null if there isn't any)
    Value lastPop();

    // add a foreign function and bind it to a global variable
    void addForeign(string name, Value (*func)(Local, Value*, VM*), Local minArgs, bool varArgs=false);

    void addGlobal(Value name, Value v);
    Value getGlobal(Value name);

    UpvalueSlot* getUpvalue(Local id);

    // get a pointer to the Bytecode object so the compiler can write its output there
    Bytecode* getBytecode();
    Allocator* getAlloc();

    // raise an exception of type FNError containing the provided message
    void runtimeError(const string& msg);

};




}

#endif
