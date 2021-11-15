#include "ffi/builtin.hpp"

#define fn_fun(name) static value fn__ ## name( \
            interpreter_handle* handle, \
            local_address argc, \
            value* argv)

#define assert_args_eq(n) if (argc != n) { \
        handle->runtime_error("Wrong number of arguments."); \
    }

namespace fn {

fn_fun(add) {
    f64 res = 0;
    for (int i = 0; i < argc; ++i) {
        handle->assert_type(TAG_NUM, argv[i]);
        res += vnumber(argv[i]);
    }
    return as_value(res);
}

fn_fun(sub) {
    f64 res = 0;
    if (argc == 0) {
        return as_value(res);
    }
    handle->assert_type(TAG_NUM, argv[0]);
    res = vnumber(argv[0]);
    for (int i = 1; i < argc; ++i) {
        handle->assert_type(TAG_NUM, argv[i]);
        res -= vnumber(argv[i]);
    }
    return as_value(res);
}

fn_fun(mul) {
    f64 res = 1;
    for (int i = 0; i < argc; ++i) {
        handle->assert_type(TAG_NUM, argv[i]);
        res *= vnumber(argv[i]);
    }
    return as_value(res);
}

fn_fun(div) {
    f64 res = 0;
    if (argc == 0) {
        return as_value(res);
    }
    handle->assert_type(TAG_NUM, argv[0]);
    res = vnumber(argv[0]);
    for (int i = 0; i < argc; ++i) {
        handle->assert_type(TAG_NUM, argv[i]);
        res /= vnumber(argv[i]);
    }
    return as_value(res);
}

fn_fun(abs) {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_abs(argv[0]);
}

fn_fun(mod) {
    if (argc != 2) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_mod(argv[0], argv[1]);
}

fn_fun(pow) {
    if (argc != 2) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_pow(argv[0], argv[1]);
}

fn_fun(exp) {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_exp(argv[0]);
}

fn_fun(log) {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_log(argv[0]);
}

fn_fun(number_q) {
    return argv[0].is_num() ? V_TRUE : V_FALSE;
}

fn_fun(integer_q) {
    if (!argv[0].is_num()) {
        return V_FALSE;
    }
    auto num = vnumber(argv[0]);
    return (num == (i64) num) ? V_TRUE : V_FALSE;
}

fn_fun(eq) {
    if (argc <= 1) {
        return V_TRUE;
    }
    auto obj = argv[0];
    for (int i = 1; i < argc; ++i) {
        if (obj != argv[i]) {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun(List) {
    auto res = V_EMPTY;
    for (int i = argc-1; i >= 0; --i) {
        res = handle->ws->add_cons(argv[i], res);
    }
    return res;
}

fn_fun(cons) {
    assert_args_eq(2);
    return handle->v_cons(argv[0], argv[1]);
}

fn_fun(nth) {
    assert_args_eq(2);
    handle->assert_type(TAG_NUM, argv[0]);
    auto num = vnumber(argv[0]);
    if (num != (i64) num) {
        handle->runtime_error("Argument must be an integer.");
    }
    return handle->v_nth(i64(num), argv[1]);
}

fn_fun(list_q) {
    return argv[0].is_empty() || argv[0].is_cons() ? V_TRUE : V_FALSE;    
}

// fn_fun(empty_q) {
//     return argv[0].is_empty() ? V_TRUE : V_FALSE;    
// }

void install_builtin(interpreter& inter) {
    inter.add_builtin_function("+", fn__add);
    inter.add_builtin_function("-", fn__sub);
    inter.add_builtin_function("*", fn__mul);
    inter.add_builtin_function("/", fn__div);
    inter.add_builtin_function("abs", fn__abs);
    inter.add_builtin_function("mod", fn__mod);
    inter.add_builtin_function("**", fn__pow);
    inter.add_builtin_function("exp", fn__exp);
    inter.add_builtin_function("log", fn__log);
    inter.add_builtin_function("number?", fn__number_q);
    inter.add_builtin_function("integer?", fn__integer_q);

    // inter.add_builtin_function("boolean?", fn__boolean_q);
    // inter.add_builtin_function("symbol?", fn__boolean_q);

    inter.add_builtin_function("=", fn__eq);
    // inter.add_builtin_function(">", fn__gt);
    // inter.add_builtin_function("<", fn__lt);
    // inter.add_builtin_function(">=", fn__geq);
    // inter.add_builtin_function("<=", fn__leq);

    inter.add_builtin_function("List", fn__List);
    inter.add_builtin_function("list?", fn__list_q);
    inter.add_builtin_function("cons", fn__cons);
    inter.add_builtin_function("nth", fn__nth);
    // inter.add_builtin_function("concat", fn__concat);

    // inter.add_builtin_function("Table", fn__Table);
    // inter.add_builtin_function("table?", fn__Table);
    // inter.add_builtin_function("get", fn__get);
    // inter.add_builtin_function("table-keys", fn__get);

    // inter.add_builtin_function("String", fn__String);
    // inter.add_builtin_function("string?", fn__String);
    // inter.add_builtin_function("substring", fn__substring);

    // inter.add_builtin_function("empty?", fn__empty_q);
    // inter.add_builtin_function("length", fn__length);

    // import into repl namespace
    auto& st = *inter.get_symtab();
    auto& glob = *inter.get_global_env();
    do_import(st,
            **glob.get_ns(st.intern("fn/repl")),
            **glob.get_ns(st.intern("fn/builtin")),
            "");
}

}
