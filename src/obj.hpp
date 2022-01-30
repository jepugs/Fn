// obj.hpp -- representations of Fn objects
#ifndef __FN_OBJ_HPP
#define __FN_OBJ_HPP

#include "array.hpp"
#include "base.hpp"
#include "table.hpp"

namespace fn {

/// value representation

// all values are 64-bits wide. The 4 least significant bits are referred to as
// the tag, and are used to encode the type of the value. All the pointers used
// for Fn objects are 16-byte aligned (in fact 32-). This allows us to store an
// entire 64-bit pointer along with the tag, since we know the 4 least
// significant digits of the pointer address will all be 0.

// tags
constexpr u64 TAG_WIDTH     = 4;
constexpr u64 TAG_MASK      = (1 << TAG_WIDTH) - 1;
// alignment of objects on the heap. This value actually gives us an unused bit
// after the tag.
constexpr u8 OBJ_ALIGN      = 32;

constexpr u64 TAG_NUM       = 0x00;

// NOTE: I want these to line up with the GC_TYPE tags in base.hpp
constexpr u64 TAG_STRING    = 0x01;
constexpr u64 TAG_CONS      = 0x02;
constexpr u64 TAG_TABLE     = 0x03;
constexpr u64 TAG_FUNC      = 0x04;

constexpr u64 TAG_SYM       = 0x06;
constexpr u64 TAG_NIL       = 0x07;
constexpr u64 TAG_TRUE      = 0x08;
constexpr u64 TAG_FALSE     = 0x09;
constexpr u64 TAG_EMPTY     = 0x0a;
constexpr u64 TAG_UNIN      = 0x0b;


// this is the main structure to represent a value
union value {
    u64 raw;
    void* ptr;
    f64 num;

    // implemented in values.cpp
    bool operator==(const value& v) const;
    bool operator!=(const value& v) const;
};

// bits used in the gc_header
constexpr u8 GC_GLOBAL_BIT      = 0x10;
constexpr u8 GC_TYPE_BITMASK    = 0x0f;

// GC Types
// NOTE: I want these five to line up with the type tags in values.hpp
constexpr u8 GC_TYPE_STRING     = 0x01;
constexpr u8 GC_TYPE_CONS       = 0x02;
constexpr u8 GC_TYPE_TABLE      = 0x03;
constexpr u8 GC_TYPE_FUNCTION   = 0x04;
constexpr u8 GC_TYPE_BIGNUM     = 0x05;
constexpr u8 GC_TYPE_UPVALUE    = 0x0a;
// function stubs (hold code, etc)
constexpr u8 GC_TYPE_FUN_STUB   = 0x06;
// vm_state objects
constexpr u8 GC_TYPE_VM_STATE   = 0x08;

// currently unused (intended for use by copying collector)
constexpr u8 GC_TYPE_FORWARD    = 0x0f;

// header contained at the beginning of every object
struct alignas (OBJ_ALIGN) gc_header {
    u8 mark;
    u8 bits;
    // used to include objects in a list
    gc_header* next;
};

// initialize a gc header in place
void init_gc_header(gc_header* dest, u8 gc_type);
// set a header to designate that its object has been moved to ptr
void set_gc_forward(gc_header* dest, gc_header* ptr);

// number of children per node in the table
constexpr u8 FN_TABLE_BREADTH = 32;

// a string of fixed size
struct alignas (OBJ_ALIGN) fn_string {
    gc_header h;
    u32 size;
    u8* data;
    bool operator==(const fn_string& other) const;
};

// a cell in a linked list
struct alignas (OBJ_ALIGN) fn_cons {
    gc_header h;
    value head;
    value tail;
};

// fn tables are represented by hash tries
struct alignas (OBJ_ALIGN) fn_table {
    gc_header h;
    // when mutated, the table is added to this GC list
    gc_header* updated_list;
    value metatable;
    table<value,value> contents;
};

// A location storing a captured variable. These are shared across functions.
struct upvalue_cell {
    gc_header h;
    bool closed;
    union {
        u32 pos;    // position on the stack when open
        value val;  // value when closed
    } datum;
};

struct istate;
struct fn_namespace;

// used to track the providence of the bytecode instructions within a function
struct source_info {
    // lowest program counter value associated to this location
    u32 start_pc;
    u32 line;
    u32 col;
    source_info* prev;
};


// linked list used to associate instructions to source code locations
struct code_info {
    u32 start_addr;
    source_loc loc;
    code_info* prev;
};


// a stub describing a function
struct function_stub {
    // function stubs are managed by the garbage collector
    gc_header h;

    u8 num_params;      // # of parameters
    u8 num_opt;         // # of optional params (i.e. of initforms)
    bool vari;          // variadic parameter
    u8 space;           // stack space required

    // if foreign != nullptr, then all following fields are ignored, and calling
    // this function will be deferred to calling foreign
    void (*foreign)(istate*);

    dyn_array<u8> code;                  // code for the function
    dyn_array<value> const_arr;          // constants
    dyn_array<function_stub*> sub_funs;  // contained functions
    symbol_id ns_id;                     // namespace ID
    fn_namespace* ns;                    // function namespace

    local_address num_upvals;
    // Array of upvalue addresses. These are either stack addresses (for local
    // upvalues)
    dyn_array<u8> upvals;
    // Corresponding array telling whether each upvalue is direct or not
    dyn_array<bool> upvals_direct;
    // An upval is considered direct if it is from the immediately surrounding
    // call frame. Otherwise, it is indirect.

    // metadata
    string* name;
    string* filename;
    code_info* ci_head;
};

void update_code_info(function_stub* to, const source_loc& loc);
code_info* instr_loc(function_stub* stub, u32 pc);

// represents a function value
struct alignas (OBJ_ALIGN) fn_function {
    gc_header h;
    // when an upvalue is mutated, the function is added to this GC list
    gc_header* updated_list;
    function_stub* stub;
    upvalue_cell** upvals;
    value* init_vals;
};

// symbols in fn are represented by a 32-bit unsigned ids
struct symtab_entry {
    symbol_id id;
    // pointer here b/c string itself is not trivially copyable, hence not
    // appropriate for a dynamic array.
    string* name;
};

// the point of the symbol table is to have fast two-way lookup going from a symbol's name to its id
// and vice versa.
class symbol_table {
private:
    table<string,symtab_entry> by_name;
    dyn_array<symtab_entry> by_id;
    symbol_id next_gensym = -1;

public:
    symbol_table() = default;
    ~symbol_table();

    symbol_id intern(const string& str);
    bool is_internal(const string& str) const;
    // if symbol_id does not name a valid symbol, returns the empty string
    string symbol_name(symbol_id sym) const;

    symbol_id gensym();
    bool is_gensym(symbol_id id) const;
    // not a true symbol name, but a useful symbolic name for a gensym
    string gensym_name(symbol_id sym) const;

    // acts like gensym_name for gensyms, symbol_name otherwise
    string nice_name(symbol_id sym) const;

    string operator[](symbol_id id) const {
        return symbol_name(id);
    }
};

// constant values
constexpr value V_NIL  = { .raw = TAG_NIL };
constexpr value V_FALSE = { .raw = TAG_FALSE };
constexpr value V_TRUE  = { .raw = TAG_TRUE };
constexpr value V_EMPTY = { .raw = TAG_EMPTY };
constexpr value V_UNIN = { .raw = TAG_UNIN };


// these are used to compute how much space should be allocated for an object
// for contiguous allocation
u32 string_size(u32 len);
u32 cons_size();
u32 function_size(function_stub stub);
// tables and chunks store additional data (i.e. table entries and code) on the
// normal C heap
u32 table_size();

fn_string* init_string(fn_string* bytes, u32 len);
fn_string* init_string(fn_string* bytes, const string& data);
fn_cons* init_cons(fn_cons* bytes, value hd, value tl);
fn_table* init_table(fn_table* bytes);



};
#endif
