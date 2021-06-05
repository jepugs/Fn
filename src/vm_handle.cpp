#include "vm_handle.hpp"

#include "vm.hpp"

namespace fn {

void runtime_error(vm_handle vm, error_code code, const string& message) {
    string s = message + " (Error code: ";
    switch (code) {
    case err_undef_op:
        s += "undefined operation).";
        break;
    case err_type:
        s += "type error).";
        break;
    case err_unavail:
        s += "resource unavailable).";
        break;
    case err_other:
        s += "other).";
        break;
    case err_unspec:
        s += "unspecified).";
        break;
    }
    vm->runtime_error(s);
}

void runtime_error(vm_handle vm, error_code code, const char* message) {
    runtime_error(vm, code, string{message});
}

}
