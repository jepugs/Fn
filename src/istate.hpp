#ifndef __FN_ISTATE_HPP
#define __FN_ISTATE_HPP

#include "api.hpp"
#include "base.hpp"
#include "obj.hpp"
#include "parse.hpp"
#include "scan.hpp"
#include "symbols.hpp"
#include "values.hpp"

namespace fn {

// size used for the istate stack
constexpr u32 STACK_SIZE = 512;
// the minimum amount of spare stack space for foreign functions
constexpr u32 FOREIGN_MIN_STACK = 20;
// default root search directory
constexpr const char* DEFAULT_PKG_ROOT = PREFIX "/lib/fn/pkg";

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
    u8* code;                                // function code
    dyn_array<upvalue_cell*> open_upvals;    // open upvalues on the stack
    value stack[STACK_SIZE];
    fn_str* filename;                     // for function metadata
    fn_str* wd;                           // working directory

    // package search
    dyn_array<string> include_paths;
    dyn_array<string> loaded_packages;

    // error handling
    error_info err;
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
        return "Actual type does not match expected type";
    }
};

istate* init_istate();
void free_istate(istate*);

void set_ns(istate* S, symbol_id ns_id);
void set_filename(istate* S, const string& name);
void set_directory(istate* S, const string& pathname);

void ierror(istate* S, const string& message);
bool has_error(istate* S);

void push(istate* S, value v);
// peek values relative to the top of the stack
value peek(istate* S);
value peek(istate* S, u32 offset);
void set(istate* S, u32 index, value v);

// eget aka "exception get" functions perform type checking and throw type
// exceptions where applicable. All these functions are indexed relative to the
// base pointer, so e.g. 0 corresponds to the first argument.
f64 eget_number(istate* S, u32 i);
const char* eget_string(istate* S, u32 i);
bool eget_bool(istate* S, u32 i);
symbol_id eget_sym(istate* S, u32 i);

// pget = "protected get". These don't throw exceptions, and do set an
// appropriate istate error on failure.
f64 pget_number(istate* S, u32 i);
const char* pget_string(istate* S, u32 i);
bool pget_bool(istate* S, u32 i);
symbol_id pget_sym(istate* S, u32 i);

// create symbols
symbol_id cached_sym(istate* S, sc_index i);

// create values on top of the stack
// convert an AST to an fn value
void push_quoted(istate* S, const scanner_string_table& sst,
        const ast::node* root);
// convert an Fn value to an ast form
bool pop_syntax(ast::node*& result, istate* S, scanner_string_table& sst);

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

// functions to trigger code loading. These leave the value of the last
// expression on the stack (or nil for files with no expressions).
void interpret_stream(istate* S, std::istream* in);
bool load_file(istate* S, const string& pathname);
// search for a package. This will check include directories, then directories
// in FN_PKG_PATH, then the root package directory.
string find_package(istate* S, const string& spec);
// like require(), but always treats the specifier as a path
bool load_file_or_package(istate* S, const string& pathname);
// perform a full require operation. This decides whether spec is a string or a
// path and loads the associated package/file.
bool require(istate* S, const string& spec);

// compile a function and push the result to the stack
bool compile_next_function(istate* S, scanner& sc);

}


#endif
