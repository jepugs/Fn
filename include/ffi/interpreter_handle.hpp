#ifndef __FFI_INTERPRETER_HANDLE_HPP
#define __FFI_INTERPRETER_HANDLE_HPP

#include "base.hpp"
#include "values.hpp"

namespace fn {

struct interpreter_handle {
    // Shhh! This secretly holds a pointer to a vm_thread object.
    void* inter;
    // used in error generation
    string func_name;

    void runtime_error(const string& msg);
    void assert_type(u64 tag, value v);

    // Type-checked utility functions. These emit runtime errors when
    // appropriate.

    // arithmetic
    value vadd(value a, value b);
    value vsub(value a, value b);
    value vmul(value a, value b);
    value vdiv(value a, value b);
    value vabs(value a);
    value vmod(value a, value b);
    value vpow(value a, value b);
    value vexp(value a);
    value vlog(value a);

    // string functions
    value vstrlen(value a);
    value vsubstr(value a, u32 start);
    value vsubstr(value a, u32 start, u32 len);
    string vstring_as_string(value a);

    // symbol functions
    value vsymname(value a);

    // list functions
    // only works on cons
    value vhead(value a);
    // works on cons and empty
    value vtail(value a);
    // tl must be a list
    value vcons(value hd, value tl);

    // table functions
    value vget(value tab, value k);
    value vset(value tab, value k, value v);
};

}

#endif
