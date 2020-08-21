// vm.hpp -- value representation and virtual machine internals

#ifndef __FN_VM_HPP
#define __FN_VM_HPP

#include "base.hpp"
#include "table.hpp"

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
    u8 slot;
    // if true, then this Upvalue refers to a slot in the enclosing function's stack. If false, then
    // it refers to an upvalue in a bigger closure.
    bool direct;
};

// a location holding an upvalue at runtime
struct UpvalueSlot {
    bool open;
    u32 refCount;
    Value** ptr;
};


// A stub describing a function. These go in the bytecode object
struct FuncStub {
    // arity information
    u8 positional;         // number of positional arguments, including optional & keyword arguments
    u8 required;           // number of required positional arguments (minimum arity)
    bool varargs;          // whether this function has a variadic argument

    // upvalues
    u8 numUpvals;
    vector<Upvalue> upvals;

    // bytecode address
    u32 addr;              // bytecode address of the function

    // get an upvalue and return its id. Adds a new upvalue if one doesn't exist
    u8 getUpvalue(u8 id, bool direct);
};

// An actual function object consists of a function stub plus the relevant a description of its
// upvalues (i.e. the closure)
struct alignas(8) Function {
    FuncStub* stub;
    UpvalueSlot* upvals;
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
    u32 size;
    u8 *data;
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

    u32 getSize();

    // set the location for writing bytes
    void setLoc(SourceLoc l);
    // get the source code location corresponding to the bytes at addr
    SourceLoc* locationOf(u32 addr);

    // write 1 or two bytes
    void writeByte(u8 b);
    void writeShort(u16 s);
    void writeBytes(const u8* bytes, u32 len);

    // these don't do bounds checking
    u8 readByte(u32 addr);
    u16 readShort(u32 addr);
    void patchShort(u32 addr, u16 s);

    // add a constant to the table and return its 16-bit ID
    u16 addConstant(Value v);
    Value getConstant(u16 id);
    u16 numConstants();

    // add a function and set it to start
    u16 addFunction(u8 arity);
    FuncStub* getFunction(u16 id);

    Value symbol(const string& name);
    u32 symbolID(const string& name);

    inline u8& operator[](u32 addr) {
        return data[addr];
    }

};


// VM stack size limit (per call frame)
constexpr u32 STACK_SIZE = 255;

struct CallFrame {
    u32 retAddr;
    u32 sp;
    Value v[STACK_SIZE];
    CallFrame *next;

    // The function we're in. nullptr on the top level.
    Function* caller;
    // upvalues to close off when return/close is called
    vector<UpvalueSlot> openUpvals;

    CallFrame(u32 ret, CallFrame* parent=nullptr)
        : retAddr(ret), sp(0), next(parent) { }
};

// The VM object contains all global state for a single instance of the interpreter.
class VM {
private:
    Bytecode code;
    Table<Value> globals;

    // foreign functions table
    vector<Value> foreignFuncs;

    // instruction pointer and stack
    u32 ip;
    CallFrame *stack;

    // last pop; used to access the result of the last expression
    Value lp;

    // stack operations
    // peek relative to the top of the stack
    Value peek(u32 offset=0);
    // peek relative to the bottom
    Value peekBot(u32 i);
    Value pop();
    void push(Value v);

public:
    // initialize the VM with a blank image
    VM();
    ~VM();

    // step a single instruction
    void step();
    void execute();
    // get the stack
    CallFrame* getStack();
    // get the instruction pointer
    u32 getIp();

    // get the last popped value (null if there isn't any)
    Value lastPop();

    // add a foreign function and bind it to a global variable
    void addForeign(string name, Value (*func)(u16, Value*, VM*), u8 minArgs, bool varArgs=false);

    void addGlobal(string name, Value v);
    Value getGlobal(string name);

    // get a pointer to the Bytecode object so the compiler can write its output there
    Bytecode* getBytecode();
};




}

#endif
