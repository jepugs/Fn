#include "init.hpp"

#include "base.hpp"
#include "bytes.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm_handle.hpp"

#include <cmath>

namespace fn {

#define fn_fun(name) static value fn_builtin_##name(working_set* ws, \
            local_addr num_args, \
            value* args)

void init_builtin(interpreter* inter) {
}

}
