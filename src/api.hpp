#ifndef __FN_API_HPP
#define __FN_API_HPP

// api.hpp -- API for interacting with the Fn interpreter
#include "base.hpp"

namespace fn {

struct istate;

// NOTES ON SAFETY: Stack bounds are never checked when using these functions.
// As when programming in C, it's the responsibility of the application
// developer to check stack bounds.

// Functions whose names begin with an extra 'p' (e.g. ppush_head()) will
// perform type checking (still no bounds checking). If one of these functions
// fails, it returns false and sets an interpreter error.

// FIXME: future versions should guarantee that there's at least some fixed
// amount of stack space available for foreign functions.

// return an 8-bit index indicating the stack pointer position relative to the
// current base pointer.
u8 get_frame_pointer(istate* S);
// how much space remains on the stack for local variables
u8 stack_space(istate* S);

// manipulate the stack

// push a copy of the value at index i
void push_copy(istate* S, u8 i);
// decrement the stack pointer
void pop(istate* S, u8 times=1);
// set the specified stack position to the value at the top of the stack, then
// decrement the stack pointer.
void pop_to_local(istate* S, u8 dest);

// create values on top of the stack
void push_num(istate* S, f64 num);
void push_str(istate* S, u32 size);
void push_str(istate* S, const string& str);
void push_sym(istate* S, symbol_id sym);
// create a symbol from the given string
void push_intern(istate* S, const string& str);
// push the name of a symbol, given its symbol id
void push_symname(istate* S, symbol_id sym);
void push_nil(istate* S);
void push_yes(istate* S);
void push_no(istate* S);
void push_empty_list(istate* S);
void pop_to_list(istate* S, u32 num);
void pop_to_vec(istate* S, u32 num);
// Warning: This does not check the type of the tail, but it *must* be a list.
// If you don't want to check, use ppush_cons() for a type-checked version
void push_cons(istate* S, u8 head_index, u8 tail_index);
bool ppush_cons(istate* S, u8 head_index, u8 tail_index);
constexpr u8 FN_TABLE_INIT_CAP = 16;
void push_empty_table(istate* S, u32 init_cap=FN_TABLE_INIT_CAP);
// num_args must be even. Arguments are used as key-value pairs to populate the
// table.
void push_table(istate* S, u8 num_args);
// push a foreign function built from a C function pointer. num_args is the
// minimum number of arguments the function accepts, vari is whether more
// arguments are accepted, and name is the name used for debugging information.
void push_foreign_function(istate* S, void (*foreign) (istate*), u8 num_args,
        bool vari, const string& name);


// unboxing functions. The protected versions return false on type errors.

void get_number(f64& out, const istate* S, u8 i);
bool pget_number(f64& out, istate* S, u8 i);
void get_str(string& out, const istate* S, u8 i);
bool pget_str(string& out, istate* S, u8 i);
void get_symbol_id(symbol_id& out, const istate* S, u8 i);
bool pget_symbol_id(symbol_id& out, istate* S, u8 i);
// Any value other than a boolean will be implicitly cast to a boolean by
// get_bool(). Thus getting a boolean is always safe (out-of-bounds errors
// notwithstanding) and a protected version is not needed
void get_bool(bool& out, const istate* S, u8 i);

// type checking

bool is_number(istate* S, u8 i);
bool is_str(istate* S, u8 i);
bool is_symbol(istate* S, u8 i);
bool is_bool(istate* S, u8 i);
bool is_nil(istate* S, u8 i);
bool is_cons(istate* S, u8 i);
bool is_list(istate* S, u8 i);
bool is_vec(istate* S, u8 i);
bool is_empty_list(istate* S, u8 i);
bool is_table(istate* S, u8 i);
bool is_function(istate* S, u8 i);

// functions on general objects

bool values_are_equal(istate* S, u8 index1, u8 index2);
bool values_are_same(istate* S, u8 index1, u8 index2);
// on failure, returns false and pushes nothing (does not set an error).
bool push_method(istate* S, u8 obj_index, symbol_id name);

// get the metatable of an object. Always pushes a value; may push nil.
void push_metatable(istate* S, u8 i);
// pop a value and set it as the metatable for a table at index i.
void pop_set_table_metatable(istate* S, u8 i);
bool ppop_set_table_metatable(istate* S, u8 i);

// list functions

void push_head(istate* S, u8 i);
bool ppush_head(istate* S, u8 i);
void push_tail(istate* S, u8 i);
bool ppush_tail(istate* S, u8 i);
// TODO: provide these functions
void concat_lists(istate* S, u8 n);
bool pconcat_lists(istate* S, u8 n);

// vector functions

void get_vec_length(istate* S, u32 num);
bool push_from_vec(istate* S, u8 i, u64 index);

// string functions

void get_str_length(u32& out, const istate* S, u8 i);
bool pget_str_length(u32& out, istate* S, u8 i);
// concatenate n strings on top of the stack. This will pop the strings and push
// the new one in their place.
void concat_strs(istate* S, u8 n);
bool pconcat_strs(istate* S, u8 n);
// get a substring
void push_substr(istate* S, u8 i, u32 start, u32 stop=-1);
bool ppush_substr(istate* S, u8 i, u32 start, u32 stop=-1);


// table functions

void push_table_entry(istate* S, u8 table_index, u8 key_index);
bool ppush_table_entry(istate* S, u8 table_index, u8 key_index);
// insert a value into a table
void pop_insert(istate* S, u8 table_index, u8 key_index);
bool ppop_insert(istate* S, u8 table_index, u8 key_index);
// clone a table (i.e. make a shallow copy), and push the clone
void push_table_clone(istate* S, u8 i);
// concatenate table. The table that comes last on the stack gets priority when
// keys collide. This pops n arguments and then pushes the result
void concat_tables(istate* S, u8 n);
bool pconcat_tables(istate* S, u8 n);

// symbol functions

// internalize a symbol
symbol_id intern_id(istate* S, const string& name);
// generate an uninterned symbol
symbol_id gensym_id(istate* S);
// resolve a symbol to its fully qualified name using the current interpreter
// namespace
symbol_id resolve_symbol(istate* S, symbol_id id);
// like resolve_symbol() but resolves in the provided namespace
symbol_id resolve_in_ns(istate* S, symbol_id id, symbol_id ns);
string symname(istate* S, symbol_id sym);

// function functions

void call(istate* S, u8 num_args);  // defined in vm.cpp
bool pcall(istate* S, u8 num_args);

// errors

void set_error(istate* S, const string& message);
// reset after an error. This will also clear the stack
void clear_error(istate* S);

// namespaces and imports

void set_namespace_id(istate* S, symbol_id new_ns_id);
void set_namespace_name(istate* S, const string& name);


// functions to create and access global variables.

// resolve symbols to their fully qualified equivalents. These are symbols whose
// names begin with #:, one of which uniquely represents each global variable.
// (Defined in namespace.cpp)
symbol_id resolve_symbol(istate* S, symbol_id name);
// like resolve_symbol(), but allows the resolution namespace to be specified.
// (Defined in namespace.cpp)
symbol_id resolve_in_namespace(istate* S, symbol_id name, symbol_id ns_id);

// pop the top of the stack and use it to set the named global variable. The
// variable is resolved in the current namespace
void pop_to_global(istate* S, symbol_id name);
// like pop_to_global(), but doesn't do name resolution (so fqn must be a fully
// qualified name).
void pop_to_fqn(istate* S, symbol_id fqn);
// access a global in the local namespace, after resolving it
bool push_global(istate* S, symbol_id name);
// like push_global(), but doesn't do name resolution (so fqn must be a fully
// qualified name).
bool push_by_fqn(istate* S, symbol_id fqn);

// pop a function off the stack and use it to set the value of a macro
void pop_to_macro(istate* S, symbol_id name);
void ppop_to_macro(istate* S, symbol_id name);
bool push_macro(istate* S, symbol_id name);
bool push_macro_by_fqn(istate* S, symbol_id fqn);


};

#endif
