#include "ffi/builtin.hpp"

#define fn_fun(name, namestr, args) \
    const char* fn_args__ ## name = args; \
    const char* fn_name__ ## name = namestr; \
    static value fn__ ## name( \
            interpreter_handle* handle, \
            local_address argc, \
            value* argv)
#define fn_add_builtin(inter, name) \
    (inter).add_builtin_function( fn_name__ ## name, fn__ ## name );

#define assert_args_eq(n) if (argc != n) { \
        handle->runtime_error("Wrong number of arguments."); \
    }

namespace fn {

// hypothetical list processing macro for ffi code
#define fn_for_list(var, lst) \
    for (auto var = lst; v_tag(var) != TAG_EMPTY; var = v_tail(var))

fn_fun(add, "+", "(& args)") {
    // // theoretical code for when i update ffi again
    // f64 res = 0;
    // // can always safely iterate over lists like this. Could be a macro?
    // handle->assert_list(args[0]);
    // for (auto tl = args[0]; vtag(tl) != TAG_EMPTY; tl = v_tail(tl)) {
    //     auto hd = v_head(tl);
    //     handle->assert_type(TAG_NUM, hd);
    //     res += vnum(hd);
    // }
    // return as_value(res);

    // same for loop above, but with a macro
    // fn_for_list(x, args[0]) {
    //     auto hd = v_head(tl);
    //     handle->assert_type(TAG_NUM, hd);
    //     res += vnum(hd);
    // }
    
    f64 res = 0;
    for (int i = 0; i < argc; ++i) {
        handle->assert_type(TAG_NUM, argv[i]);
        res += vnumber(argv[i]);
    }
    return as_value(res);
}

fn_fun(sub, "-", "(& args)") {
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

fn_fun(mul, "*", "(& args)") {
    f64 res = 1;
    for (int i = 0; i < argc; ++i) {
        handle->assert_type(TAG_NUM, argv[i]);
        res *= vnumber(argv[i]);
    }
    return as_value(res);
}

fn_fun(div, "/", "(& args)") {
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

fn_fun(abs, "abs", "(x)") {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_abs(argv[0]);
}

fn_fun(mod, "mod", "(x y)") {
    if (argc != 2) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_mod(argv[0], argv[1]);
}

fn_fun(pow, "**", "(x y)") {
    if (argc != 2) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_pow(argv[0], argv[1]);
}

fn_fun(exp, "exp", "(x)") {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_exp(argv[0]);
}

fn_fun(log, "log", "(x)") {
    if (argc != 1) {
        handle->runtime_error("Wrong number of arguments.");
    }
    return handle->v_log(argv[0]);
}

fn_fun(number_q, "number?", "(x)") {
    return argv[0].is_num() ? V_TRUE : V_FALSE;
}

fn_fun(integer_q, "integer?", "(x)") {
    if (!argv[0].is_num()) {
        return V_FALSE;
    }
    auto num = vnumber(argv[0]);
    return (num == (i64) num) ? V_TRUE : V_FALSE;
}

fn_fun(eq, "=", "(& args)") {
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

fn_fun(List, "List", "(& args)") {
    auto res = V_EMPTY;
    for (int i = argc-1; i >= 0; --i) {
        res = handle->ws->add_cons(argv[i], res);
    }
    return res;
}

fn_fun(cons, "cons", "(hd tl)") {
    assert_args_eq(2);
    return handle->v_cons(argv[0], argv[1]);
}

fn_fun(nth, "nth", "(n list)") {
    assert_args_eq(2);
    handle->assert_type(TAG_NUM, argv[0]);
    auto num = vnumber(argv[0]);
    if (num != (i64) num) {
        handle->runtime_error("Argument must be an integer.");
    }
    return handle->v_nth(i64(num), argv[1]);
}

fn_fun(list_q, "list?", "(x)") {
    return argv[0].is_empty() || argv[0].is_cons() ? V_TRUE : V_FALSE;    
}

fn_fun(empty_q, "empty?", "(x)") {
    if (argc != 1) {
        handle->runtime_error("empty? requires exactly one argument");
    }
    switch (v_tag(argv[0])) {
    case TAG_CONS:
        return V_FALSE;
    case TAG_EMPTY:
        return V_TRUE;
    case TAG_TABLE:
        return vtable(argv[0])->contents.get_size() == 0 ? V_TRUE : V_FALSE;
    default:
        handle->runtime_error("empty? argument must be a list or a table.");
    }
}

fn_fun(length, "length", "(x)") {
    if (argc != 1) {
        handle->runtime_error("length requires exactly one argument");
    }
    return handle->v_length(argv[0]);
}

fn_fun(Table, "Table", "(& args)") {
    if (argc % 2 != 0) {
        handle->runtime_error("Table requires an even number of arguments.");
    }
    auto res = handle->ws->add_table();
    for (u32 i = 0; i < argc; i += 2) {
        vtable(res)->contents.insert(argv[i], argv[i+1]);
    }
    return res;
}

fn_fun(get, "get", "(obj & keys)") {
    if (argc < 1) {
        handle->runtime_error("get requires at least one argument");
    }

    value res = argv[0];
    for (u32 i = 1; i < argc; ++i) {
        handle->assert_type(TAG_TABLE, res);
        auto x = vtable(res)->contents.get(argv[i]);
        if (!x.has_value()) {
            res = V_NIL;
        } else {
            res = *x;
        }
    }
    return res;
}

fn_fun(table_keys, "table-keys", "(table)") {
    if (argc != 1) {
        handle->runtime_error("table-keys requires exactly one argument");
    }
    handle->assert_type(TAG_TABLE, argv[0]);
    auto keys = vtable(argv[0])->contents.keys();
    auto res = V_EMPTY;
    for (auto k : keys) {
        res = handle->ws->add_cons(k, res);
    }
    return res;
}

void install_builtin(interpreter& inter) {
    fn_add_builtin(inter, add);
    fn_add_builtin(inter, sub);
    fn_add_builtin(inter, mul);
    fn_add_builtin(inter, div);
    fn_add_builtin(inter, abs);
    fn_add_builtin(inter, mod);
    fn_add_builtin(inter, pow);
    fn_add_builtin(inter, exp);
    fn_add_builtin(inter, log);
    fn_add_builtin(inter, number_q);
    fn_add_builtin(inter, integer_q);

    // fn_add_builtin(inter, boolean_q);
    // fn_add_builtin(inter, boolean_q);

    fn_add_builtin(inter, eq);
    // fn_add_builtin(inter, gt);
    // fn_add_builtin(inter, lt);
    // fn_add_builtin(inter, geq);
    // fn_add_builtin(inter, leq);

    fn_add_builtin(inter, List);
    fn_add_builtin(inter, list_q);
    fn_add_builtin(inter, cons);
    fn_add_builtin(inter, nth);
    // fn_add_builtin(inter, concat);

    fn_add_builtin(inter, Table);
    fn_add_builtin(inter, get);
    fn_add_builtin(inter, table_keys);

    // fn_add_builtin(inter, String);
    // fn_add_builtin(inter, String);
    // fn_add_builtin(inter, substring);

    fn_add_builtin(inter, empty_q);
    fn_add_builtin(inter, length);

    // import into repl namespace
    auto& st = *inter.get_symtab();
    auto& glob = *inter.get_global_env();
    do_import(st,
            **glob.get_ns(st.intern("fn/user")),
            **glob.get_ns(st.intern("fn/builtin")),
            "");
}

}
