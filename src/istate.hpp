#ifndef __FN_ISTATE_HPP
#define __FN_ISTATE_HPP

#include "base.hpp"
#include "obj.hpp"
#include "symbols.hpp"
#include "values.hpp"

namespace fn {

// size used for the istate stack
constexpr u32 STACK_SIZE = 512;
// the minimum amount of spare stack space for foreign functions
constexpr u32 FOREIGN_MIN_STACK = 20;

struct allocator;
struct global_env;

struct trace_frame {
    fn_function* callee;
    u32 pc;
};

struct istate {
    allocator* alloc;
    symbol_table* symtab;
    symbol_cache* symcache;
    global_env* G;                           // global definitions
    symbol_id ns_id;                         // current namespace
    u32 pc;                                  // program counter
    u32 bp;                                  // base ptr
    u32 sp;                                  // stack ptr (rel to stack bottom)
    fn_function* callee;                     // current function
    dyn_array<upvalue_cell*> open_upvals;    // open upvalues on the stack
    value stack[STACK_SIZE];
    fn_string* filename;                     // for function metadata
    fn_string* wd;                           // working directory

    // error handling
    bool err_happened;
    // this is nullptr unless err_happened == true, in which case it must be
    // freed after processing the error.
    fn_string* err_msg;
    // used to generate stack traces on error
    dyn_array<trace_frame> stack_trace;
};

// Exception thrown when a type check fails. This is caught internally when it
// occurs in a foreign function.
class type_error : public std::exception {
    string expected_type;
    string actual_type;
public:
    type_error(const string& expected, const string& actual) noexcept
        : expected_type{expected}
        , actual_type{actual} {
    }
    const char* what() const noexcept {
        return ("Expected type " + expected_type + " but got "
                + actual_type + ".").c_str();
    }
};

istate* init_istate();
void free_istate(istate*);

void set_ns(istate* S, symbol_id ns_id);
void set_filename(istate* S, const string& name);
void set_directory(istate* S, const string& pathname);

void ierror(istate* S, const string& message);

void push(istate* S, value v);
void pop(istate* S);
void pop(istate* S, u32 n);
// peek values relative to the top of the stack
value peek(istate* S);
value peek(istate* S, u32 offset);
void set(istate* S, u32 index, value v);

// pget aka "protected get" functions perform type checking and throw type
// exceptions where applicable. All these functions are indexed relative to the
// base pointer, so e.g. 0 corresponds to the first argument.
f64 pget_number(istate* S, u32 i);
const char* pget_string(istate* S, u32 i);
bool pget_bool(istate* S, u32 i);
symbol_id pget_sym(istate* S, u32 i);

// create symbols
symbol_id intern(istate* S, const string& str);
symbol_id gensym(istate* S);
string symname(istate* S, symbol_id sid);
symbol_id cached_sym(istate* S, sc_index i);

// create values on top of the stack
void push_number(istate* S, f64 num);
void push_string(istate* S, u32 size);
void push_string(istate* S, const string& str);
void push_sym(istate* S, symbol_id sym);
void push_symname(istate* S, symbol_id sym);
void push_nil(istate* S);
void push_yes(istate* S);
void push_no(istate* S);

// hd and tl are positions on the stack
void push_cons(istate* S, u32 hd, u32 tl);
void push_table(istate* S);

// create a list from the top n elements of the stack
void pop_to_list(istate* S, u32 n);

// perform a function call of n arguments, (default 0). The calling convention
// is to put the function on the bottom followed by the arguments in order, so
// the last argument is on top of the stack.
void call(istate* S);
void call(istate* S, u32 n);

// push a function with an newly-created, empty function stub. This is used
// during function compilation to ensure the function stub is visible to the
// compiler while the function is being created.
void push_empty_fun(istate* S);
// push a foreign function by that wraps the provided function pointer
void push_foreign_fun(istate* S,
        void (*foreign)(istate*),
        const string& name,
        const string& params);
void push_foreign_fun(istate* S,
        void (*foreign)(istate*),
        const string& name,
        const string& params,
        u8 num_upvals);

// print out the top of the stack to stdout
void print_top(istate* S);

// print out a stack trace to stdout
void print_stack_trace(istate* S);

// functions to trigger code loading
void interpret_stream(istate* S, std::istream* in);
bool require_file(istate* S, const string& pathname);
bool require_package(istate* S, const string& pathname);
bool require(istate* S, const string& spec);

}


#endif
