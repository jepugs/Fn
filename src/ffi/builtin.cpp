#include "ffi/builtin.hpp"

#define fn_fun(name) static value fn__ ## name( \
            interpreter_handle* handle, \
            local_address argc, \
            value* argv)

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
    return handle->vabs(argv[0]);
}

fn_fun(mod) {
    if (argc != 2) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->vmod(argv[0], argv[1]);
}

fn_fun(pow) {
    if (argc != 2) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->vpow(argv[0], argv[1]);
}

fn_fun(exp) {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->vexp(argv[0]);
}

fn_fun(log) {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->vlog(argv[0]);
}

void install_builtin(interpreter& inter) {
    inter.add_builtin_function("+", fn__add);
    inter.add_builtin_function("-", fn__sub);
    inter.add_builtin_function("*", fn__mul);
    inter.add_builtin_function("/", fn__div);
    inter.add_builtin_function("abs", fn__abs);
    inter.add_builtin_function("mod", fn__mod);
    inter.add_builtin_function("pow", fn__pow);
    inter.add_builtin_function("exp", fn__exp);
    inter.add_builtin_function("log", fn__log);


    // import into repl namespace
    auto& st = *inter.get_symtab();
    auto& glob = *inter.get_global_env();
    do_import(st,
            **glob.get_ns(st.intern("fn/repl")),
            **glob.get_ns(st.intern("fn/builtin")),
            "");
}

}
