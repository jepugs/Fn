#ifndef __FN_VM_HANDLE_HPP
#define __FN_VM_HANDLE_HPP

#include "base.hpp"

namespace fn {

class virtual_machine;
typedef virtual_machine* vm_handle;

enum error_code {
    // Undefined operation, e.g. attempt to divide by 0
    err_undef_op = 1,
    // Indicates that a type failed to return
    err_type = 2,
    // Resource unavailable. Indicates a required system resource is no longer
    // available. E.g. this may be used if an attempt is made to read from a
    // closed file.
    err_unavail = 3,
    // No of other code adequately describes the error
    err_other = -2,
    // Unspecified error
    err_unspec = -1
};

void runtime_error(vm_handle vm, error_code code, const string& message);
void runtime_error(vm_handle vm, error_code code, const char* message);

void evaluate_string(vm_handle vm, const string& str);
void evaluate_string(vm_handle vm, const char* str);

}

#endif
