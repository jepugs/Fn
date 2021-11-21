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

    // Type-checked utility functions. These emit runtime errors when
    // appropriate.

    // arithmetic
    value v_add(value a, value b);
    value v_sub(value a, value b);
    value v_mul(value a, value b);
    value v_div(value a, value b);
    value v_abs(value a);
    value v_mod(value a, value b);
    value v_pow(value a, value b);
    value v_exp(value a);
    value v_log(value a);

    // string functions
    value v_strlen(value a);
    value v_substr(value a, u32 start);
    value v_substr(value a, u32 start, u32 len);
    string v_string_as_string(value a);

    // symbol functions
    value v_symname(value a);

    // list functions
    // only works on cons
    value v_head(value a);
    // works on cons and empty
    value v_tail(value a);
    // tl must be a list
    value v_cons(value hd, value tl);
    // lst must be a list and n must be in bounds
    value v_nth(i64 n, value lst);

    // table functions
    value v_get(value tab, value k);
    value v_set(value tab, value k, value v);

    // collection functions
    value v_length(value x);
};

}

#endif
