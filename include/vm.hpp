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
    Symbol *byId;
    u32 size;
    u32 cap;

public:
    SymbolTable();
    ~SymbolTable();

    const Symbol* intern(string str);
    const Symbol* isInternal(string str);
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
    // constants and symbols
    vector<Value> constants;
    SymbolTable symbols;

    void ensureCapacity(int newCap);

public:
    Bytecode();
    ~Bytecode();

    void writeByte(u8 byte);
    void writeBytes(const u8* bytes, u32 len);

    Value symbol(const string& name);
    u32 symbolID(const string& name);

    // Get a 16-bit identifier for the given constant.
    u16 getConstant(Value v);
};


// VM stack size limit (per call frame)
constexpr u32 STACK_SIZE = 255;

struct CallFrame {
    u32 retAddr;
    Value stack[STACK_SIZE];
    u32 sp;
    CallFrame *next;
};

// The VM object contains all global state for a single instance of the interpreter.
class VM {
private:
    Bytecode image;
    Table<Value> globals;

    // instruction pointer and stack
    u32 ip;
    CallFrame *stack;

    // stack operations
    Value peek(u32 i=0);
    Value pop();
    void push(Value v);

public:
    // initialize the VM with a blank image
    VM();

    // get a pointer to the image so the compiler can write its output
    Bytecode *getImage();
};




}

#endif
