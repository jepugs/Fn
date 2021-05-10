#include "init.hpp"

#include "base.hpp"
#include "bytes.hpp"
#include "table.hpp"
#include "values.hpp"

#include <cmath>

namespace fn {

#define FN_FUN(name) static value name(local_addr num_args, value* args, virtual_machine* vm)

static value fn_eq(local_addr num_args, value* args, virtual_machine* vm) {
    if (num_args == 0) return V_TRUE;

    auto v1 = args[0];
    for (local_addr i = 1; i < num_args; ++i) {
        if (v1 != args[i])
            return V_FALSE;
    }
    return V_TRUE;
}

FN_FUN(fn_null_q) {
    return as_value(args[0] == V_NULL);
}

FN_FUN(fn_bool) {
    return as_value(v_truthy(args[0]));
}

FN_FUN(fn_bool_q) {
    return as_value(args[0].is_bool());
}

FN_FUN(fn_not) {
    return as_value(!v_truthy(args[0]));
}

// FN_FUN(fn_num) {
//     switch(v_tag(args[0])) {
//     case TAG_NUM:
//         return args[0];
//     case TAG_STR:
//         try {
//             auto d = stod(*v_str(args[0]));
//             return as_value(d);
//         } catch(...) { // t_od_o: this should probably be a runtime warning...
//             vm->runtime_error("string argument to num does not represent a number."); 
//             return as_value(0.0);
//         }
//     case TAG_NULL:
//         return as_value(0.0);
//     case TAG_BOOL:
//         return as_value((f64)v_bool(args[0]));
//     default:
//         vm->runtime_error("num cannot convert value of the given type.");
//         return as_value(0.0);
//     }
// }

FN_FUN(fn_num_q) {
    return as_value(v_tag(args[0]) == TAG_NUM);
}

FN_FUN(fn_int_q) {
    return as_value(v_tag(args[0]) == TAG_NUM
                 && v_num(args[0]) == (u64)v_num(args[0]));
}

static value fn_add(local_addr num_args, value* args, virtual_machine* vm) {
    f64 res = 0;
    for (local_addr i = 0; i < num_args; ++i) {
        res += args[i].vnum();
    }
    return as_value(res);
}

static value fn_sub(local_addr num_args, value* args, virtual_machine* vm) {
    if (num_args == 0)
        return as_value(0);
    f64 res = args[0].vnum();
    if (num_args == 1) {
        return as_value(-res);
    }
    for (local_addr i = 1; i < num_args; ++i) {
        res -= args[i].vnum();
    }
    return as_value(res);
}

static value fn_mul(local_addr num_args, value* args, virtual_machine* vm) {
    f64 res = 1.0;
    for (local_addr i = 0; i < num_args; ++i) {
        res *= args[i].vnum();
    }
    return as_value(res);
}

static value fn_div(local_addr num_args, value* args, virtual_machine* vm) {
    if (num_args == 0)
        return as_value(1.0);

    f64 res = args[0].vnum();
    if (num_args == 1) {
        return as_value(1/res);
    }
    for (local_addr i = 1; i < num_args; ++i) {
        res /= args[i].vnum();
    }
    return as_value(res);
}

FN_FUN(fn_pow) {
    return args[0].pow(args[1]);
}

FN_FUN(fn_mod) {
    if (!args[0].is_int() || !args[1].is_int()) {
        vm->runtime_error("mod arguments must be integers");
    }
    i64 u = (i64)args[0].vnum();
    i64 v = (i64)args[1].vnum();
    return as_value(u % v);
}

FN_FUN(fn_floor) {
    return as_value(std::floor(args[0].vnum()));
}

FN_FUN(fn_ceil) {
    return as_value(std::ceil(args[0].vnum()));
}

FN_FUN(fn_gt) {
    auto v = args[0].vnum();
    for (local_addr i = 1; i < num_args; ++i) {
        auto u = args[i].vnum();
        if (v > u) {
            v = u;
            continue;
        }
        return V_FALSE;
    }
    return V_TRUE;
}

FN_FUN(fn_lt) {
    auto v = args[0].vnum();
    for (local_addr i = 1; i < num_args; ++i) {
        auto u = args[i].vnum();
        if (v < u) {
            v = u;
            continue;
        }
        return V_FALSE;
    }
    return V_TRUE;
}

FN_FUN(fn_ge) {
    auto v = args[0].vnum();
    for (local_addr i = 1; i < num_args; ++i) {
        auto u = args[i].vnum();
        if (v >= u) {
            v = u;
            continue;
        }
        return V_FALSE;
    }
    return V_TRUE;
}

FN_FUN(fn_le) {
    auto v = args[0].vnum();
    for (local_addr i = 1; i < num_args; ++i) {
        auto u = args[i].vnum();
        if (v <= u) {
            v = u;
            continue;
        }
        return V_FALSE;
    }
    return V_TRUE;
}

FN_FUN(fn_table) {
    if (num_args % 2 != 0) {
        vm->runtime_error("Table must have an even number of arguments.");
    }
    auto res = vm->get_alloc()->add_table();
    for (local_addr i = 0; i < num_args; i += 2) {
        v_table(res)->contents.insert(args[i],args[i+1]);
    }
    return res;
}

FN_FUN(fn_table_q) {
    return as_value(args[0].is_table());
}

FN_FUN(fn_list) {
    auto res = V_EMPTY;
    for (local_addr i = num_args; i > 0; --i) {
        res = vm->get_alloc()->add_cons(args[i-1], res);
    }
    return res;
}

FN_FUN(fn_list_q) {
    return as_value(args[0].is_empty() || args[0].is_cons());
}

FN_FUN(fn_has_key) {
    return as_value(args[0].table_has_key(args[1]));
}

FN_FUN(fn_get) {
    auto res = args[0].get(args[1]);
    for (local_addr i = 2; i < num_args; ++i) {
        if (res.has_value()) {
            res = res->get(args[i]);
        } else {
            vm->runtime_error("get failed");
        }
    }
    if (!res.has_value()) {
        vm->runtime_error("get failed");
    }
    return *res;
}

static value fn_print(local_addr num_args, value* args, virtual_machine* vm) {
    std::cout << v_to_string(args[0], vm->get_bytecode()->get_symbol_table());
    return V_NULL;
}

static value fn_println(local_addr num_args, value* args, virtual_machine* vm) {
    std::cout << v_to_string(args[0], vm->get_bytecode()->get_symbol_table()) << "\n";
    return V_NULL;
}


void init(virtual_machine* vm) {
    vm->add_foreign("=", fn_eq, 0, true);
    vm->add_foreign("null?", fn_null_q, 1, false);
    vm->add_foreign("bool", fn_bool, 1, false);
    vm->add_foreign("bool?", fn_bool_q, 1, false);
    vm->add_foreign("not", fn_not, 1, false);
    //vm->add_foreign("num", fn_num, 1, false);
    vm->add_foreign("num?", fn_num_q, 1, false);
    vm->add_foreign("int?", fn_int_q, 1, false);
    vm->add_foreign("+", fn_add, 0, true);
    vm->add_foreign("-", fn_sub, 0, true);
    vm->add_foreign("*", fn_mul, 0, true);
    vm->add_foreign("/", fn_div, 0, true);
    vm->add_foreign("^", fn_pow, 2, false);
    vm->add_foreign("mod", fn_mod, 2, false);
    vm->add_foreign("floor", fn_floor, 1, false);
    vm->add_foreign("ceil", fn_ceil, 1, false);
    vm->add_foreign(">", fn_gt, 2, true);
    vm->add_foreign("<", fn_lt, 2, true);
    vm->add_foreign(">=", fn_ge, 2, true);
    vm->add_foreign("<=", fn_le, 2, true);
    vm->add_foreign("Table", fn_table, 0, true);
    vm->add_foreign("table?", fn_table_q, 1, false);
    vm->add_foreign("has-key", fn_has_key, 2, false);
    vm->add_foreign("get", fn_get, 2, true);
    //vm->add_foreign("get-keys", fn_get_keys, 1, false);
    //vm->add_foreign("get-props", fn_get_props, 1, false);
    //vm->add_foreign("extend", fn_div, 1, true);
    vm->add_foreign("list", fn_list, 0, true);
    vm->add_foreign("list?", fn_list_q, 1, false);
    //vm->add_foreign("empty?", fn_empty_q, 0, false);
    //vm->add_foreign("as-list", fn_lasist, 1, false);
    //vm->add_foreign("cons", fn_div, 0, true);
    //vm->add_foreign("append", fn_div, 0, true);
    //vm->add_foreign("reverse", fn_div, 0, true);
    //vm->add_foreign("string", fn_string, 0, true);
    //vm->add_foreign("string?", fn_string_q, 0, false);
    //vm->add_foreign("substring", fn_substring, 3, false);
    //vm->add_foreign("hash", fn_hash, 1, false);
    vm->add_foreign("print", fn_print, 1, false);
    vm->add_foreign("println", fn_println, 1, false);
    //vm->add_foreign("apply", fn_div, 2, true);
}

}
