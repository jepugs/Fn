#ifndef __FFI_FN_HANDLE_HPP
#define __FFI_FN_HANDLE_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {

struct vm_thread;
struct root_stack;

struct fn_handle {
    vm_thread* vm;
    root_stack* stack;

    // used in error generation
    string func_name;
    source_loc origin;
    fault* err;

    // used to check errors
    inline bool failed() {
        return err->happened;
    }
    // used to set an error
    void error(const string& message);

    // these asserts don't actually raise exceptions; the caller must check
    // failed() afterwards.

    // assert a value has the actual time
    void assert_type(u64 tag, value v);
    // assert that a value is a list
    void assert_list(value v);
    // assert that a value is an integer
    void assert_integer(value v);

    // symbol creation
    // get a symbol with the specified name
    value intern(const char* str);
    // generate a unique unnamed symbol (mainly for macros)
    value gensym();


    // creating values on the stack

    // objects that need to be seen by the GC get created directly on the stack.
    // You can choose to create them by pushing to the top of the stack, or by
    // overwriting an existing position on the stack to the new object. The
    // stack is indexed from the top in these functions.

    // create a new string
    value push_string(const string& str);
    value make_string(local_address offset, const string& str);

    // create a new cons
    value push_cons(value hd, value tl);
    value make_cons(local_address offset, value hd, value tl);

    // create a new, empty table
    value push_table();
    value make_table(local_address offset);

    // basic stack manipulation
    value peek(local_address offset=0);
    void push(value v);
    void pop();
    void set(local_address offset, value v);

    // additional functions which create new values on top of the stack:

    // additional functions that create strings:
    value substr(local_address offset, value a, u32 start);
    value substr(local_address offset, value a, u32 start, u32 len);
    value symname(local_address offset, value a);

    // concatenation. This creates a new structure and pushes it to the top of
    // the list.
    value string_concat(local_address offset, value l, value r);
    value list_concat(local_address offset, value l, value r);
    value table_concat(local_address offset, value l, value r);

    // format a value as a C++ string
    string as_string(value a);
};


}


extern "C" {

typedef fn::fn_handle fn_handle;

}

#endif
