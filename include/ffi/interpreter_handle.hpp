#ifndef __FFI_INTERPRETER_HANDLE_HPP
#define __FFI_INTERPRETER_HANDLE_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {

struct interpreter_handle {
    // Shhh! This secretly holds a pointer to a vm_thread object.
    void* inter;
    working_set* ws;
    // used in error generation
    string func_name;

    void runtime_error(const string& msg);
    void assert_type(u64 tag, value v);
    // assert that a value is a list
    void assert_list(value v);

    // string functions
    value string_concat(value l, value r);

    // list functions
    value list_concat(value l, value r);

    // table functions
    value table_concat(value l, value r);

    // collection functions
    value v_length(value x);
};

}

#endif
