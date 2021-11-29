#ifndef __FFI_FN_HANDLE_HPP
#define __FFI_FN_HANDLE_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {


struct fn_handle {
    // Shhh! This secretly holds a pointer to a vm_thread object.
    void* vm;
    void* ws;
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


    // value creation

    // create a new string
    value add_string(const char* str);
    value add_string(const string& str);
    // create a string with the specified length and uninitialized data.
    value add_string(u32 len);
    // create a new cons
    value add_cons(value hd, value tl);
    // get a symbol with the specified name
    value intern(const char* str);
    // generate a unique unnamed symbol (mainly for macros)
    value gensym();
    // create a new table
    value add_table();

    // These utility functions ARE NOT TYPE CHECKED. They belong here because
    // they require the symbol table or allocator. If you want type checking, do
    // it yourself.

    // string functions
    value substr(value a, u32 start);
    value substr(value a, u32 start, u32 len);
    string as_string(value a);
    value string_concat(value l, value r);

    // symbol functions
    value symname(value a);

    // list functions
    value list_concat(value l, value r);

    // table functions
    value table_concat(value l, value r);
};


}


extern "C" {

typedef fn::fn_handle fn_handle;

}

#endif
