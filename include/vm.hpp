// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "base.hpp"
#include "table.hpp"

#include <forward_list>
#include <vector>

namespace fn {

using namespace std;

/// value representation

// all values are 64-bits wide. Either 3 or 8 bits are used to hold the tag. The reason for the
// variable tag size is that 3-bit tags can be associated with an 8-byte-aligned pointer (so we only
// need 61 bits to hold the pointer, since the last 3 bits are 0). This requires that none of the
// 3-bit tags occur as prefixes to the 8-bit tags.
union Value {
    u64 raw;
    void* ptr;
    f64 num;
};


// Symbols in fn are represented by a 32-bit unsigned ID
struct Symbol {
    u32 id;
    string name;
};

// The point of the symbol table is to have fast two-way lookup going from a symbol's name to its id
// and vice versa.
class SymbolTable {
private:
    Table<Symbol> byName;
    vector<Symbol> byId;

public:
    SymbolTable();

    const Symbol* intern(string str);
    bool isInternal(string str);
};


// This struct describes an upvalue. Upvalues themselves are simply stored in Value pointers.
struct Upvalue {
    // the stack position or ID of this upvalue in the enclosing function
    Local slot;
    // if true, then this Upvalue refers to a slot in the enclosing function's stack. If false, then
    // it refers to an upvalue in a bigger closure.
    bool direct;
};

// a location holding an upvalue at runtime
struct UpvalueSlot {
    // if true, this upvalue points to an element of the stack
    bool open;
    Value* val;

    //u32 refCount;
};


// A stub describing a function. These go in the bytecode object
struct FuncStub {
    // arity information
    Local positional;   // number of positional arguments, including optional & keyword arguments
    Local required;     // number of required positional arguments (minimum arity)
    bool varargs;          // whether this function has a variadic argument

    // upvalues
    Local numUpvals;
    vector<Upvalue> upvals;

    // bytecode address
    Addr addr;              // bytecode address of the function

    // get an upvalue and return its id. Adds a new upvalue if one doesn't exist
    Local getUpvalue(Local id, bool direct);
};

// An actual function object consists of a function stub plus the relevant a description of its
// upvalues (i.e. the closure)
struct alignas(8) Function {
    FuncStub* stub;
    UpvalueSlot** upvals;
};

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
    BytecodeLoc *locs;
    // pointer to the end of the list
    BytecodeLoc *lastLoc;

    // constants and symbols
    vector<Value> constants;
    SymbolTable symbols;
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

    // add a constant to the table and return its 16-bit ID
    u16 addConstant(Value v);
    Value getConstant(u16 id);
    u16 numConstants();

    // add a function and set it to start
    u16 addFunction(Local arity);
    FuncStub* getFunction(u16 id);

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
    Table<Value> globals;

    // foreign functions table
    vector<Value> foreignFuncs;

    // instruction pointer and stack
    StackAddr ip;
    CallFrame *frame;
    Value stack[STACK_SIZE];

    // last pop; used to access the result of the last expression
    Value lp;

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

public:
    // initialize the VM with a blank image
    VM();
    ~VM();

    // step a single instruction
    void step();
    void execute();
    // get the instruction pointer
    Addr getIp();

    // get the last popped value (null if there isn't any)
    Value lastPop();

    // add a foreign function and bind it to a global variable
    void addForeign(string name, Value (*func)(Local, Value*, VM*), Local minArgs, bool varArgs=false);

    void addGlobal(string name, Value v);
    Value getGlobal(string name);

    UpvalueSlot* getUpvalue(Local id);

    // get a pointer to the Bytecode object so the compiler can write its output there
    Bytecode* getBytecode();
};




}

#endif
