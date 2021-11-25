#include "ffi/builtin.hpp"

#define fn_fun(name, namestr, params) \
    const char* fn_params__ ## name = params; \
    const char* fn_name__ ## name = namestr; \
    static value fn__ ## name(interpreter_handle* handle, value* args)
#define fn_add_builtin(inter, name) \
    (inter).add_builtin_function( fn_name__ ## name, fn_params__ ## name, \
            fn__ ## name );
#define fn_for_list(var, lst) \
    for (auto var = lst; v_tag(var) != TAG_EMPTY; var = v_tail(var))


namespace fn {

fn_fun(add, "+", "(& args)") {
    f64 res = 0;
    // don't need to check if args is a list b/c of param list
    fn_for_list(it, args[0]) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        res += vnumber(hd);
    }
    return as_value(res);
}

fn_fun(sub, "-", "(& args)") {
    if (args[0] == V_EMPTY) {
        return as_value(0.0);
    } else if (v_tail(args[0]) == V_EMPTY) {
        auto x = v_head(args[0]);
        handle->assert_type(TAG_NUM, x);
        return as_value(-vnumber(x));
    }

    auto x = v_head(args[0]);
    handle->assert_type(TAG_NUM, x);
    auto res = vnumber(x);
    fn_for_list(it, v_tail(args[0])) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        res -= vnumber(hd);
    }
    return as_value(res);
}

fn_fun(mul, "*", "(& args)") {
    f64 res = 1;
    fn_for_list(it, args[0]) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        res *= vnumber(hd);
    }
    return as_value(res);
}

fn_fun(div, "/", "(& args)") {
    if (args[0] == V_EMPTY) {
        return as_value(1.0);
    } else if (v_tail(args[0]) == V_EMPTY) {
        auto x = v_head(args[0]);
        handle->assert_type(TAG_NUM, x);
        return as_value(1/vnumber(x));
    }

    auto x = v_head(args[0]);
    handle->assert_type(TAG_NUM, x);
    auto res = vnumber(x);
    fn_for_list(it, v_tail(args[0])) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        res /= vnumber(hd);
    }
    return as_value(res);
}

fn_fun(abs, "abs", "(x)") {
    return handle->v_abs(args[0]);
}

fn_fun(mod, "mod", "(x y)") {
    return handle->v_mod(args[0], args[1]);
}

fn_fun(pow, "**", "(x y)") {
    return handle->v_pow(args[0], args[1]);
}

fn_fun(exp, "exp", "(x)") {
    return handle->v_exp(args[0]);
}

fn_fun(log, "log", "(x)") {
    return handle->v_log(args[0]);
}

fn_fun(number_q, "number?", "(x)") {
    return args[0].is_num() ? V_TRUE : V_FALSE;
}

fn_fun(integer_q, "integer?", "(x)") {
    if (!args[0].is_num()) {
        return V_FALSE;
    }
    auto num = vnumber(args[0]);
    return (num == (i64) num) ? V_TRUE : V_FALSE;
}

fn_fun(eq, "=", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto x = args[0];
    fn_for_list(it, v_tail(args[0])) {
        if (v_head(it) != x) {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun(lt, "<", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto prev = v_head(args[0]);
    handle->assert_type(TAG_NUM, prev);

    fn_for_list(it, v_tail(args[0])) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        if (vnumber(prev) < vnumber(hd)) {
            prev = hd;
        } else {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun(gt, ">", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto prev = v_head(args[0]);
    handle->assert_type(TAG_NUM, prev);

    fn_for_list(it, v_tail(args[0])) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        if (vnumber(prev) > vnumber(hd)) {
            prev = hd;
        } else {
            return V_FALSE;
        }
    }
    return V_TRUE;
}


fn_fun(le, "<=", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto prev = args[0];
    handle->assert_type(TAG_NUM, prev);

    fn_for_list(it, v_tail(args[0])) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        if (vnumber(prev) <= vnumber(hd)) {
            prev = hd;
        } else {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun(ge, ">=", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto prev = args[0];
    handle->assert_type(TAG_NUM, prev);

    fn_for_list(it, v_tail(args[0])) {
        auto hd = v_head(it);
        handle->assert_type(TAG_NUM, hd);
        if (vnumber(prev) < vnumber(hd)) {
            prev = hd;
        } else {
            return V_FALSE;
        }
    }
    return V_TRUE;
}


fn_fun(List, "List", "(& args)") {
    return args[0];
}

fn_fun(cons, "cons", "(hd tl)") {
    return handle->v_cons(args[0], args[1]);
}

fn_fun(head, "head", "(x)") {
    if (args[0] == V_EMPTY) {
        return V_NIL;
    } else {
        handle->assert_type(TAG_CONS, args[0]);
        return vcons(args[0])->head;
    }
}

fn_fun(tail, "tail", "(x)") {
    if (args[0] == V_EMPTY) {
        return V_NIL;
    } else {
        handle->assert_type(TAG_CONS, args[0]);
        return vcons(args[0])->tail;
    }
}

fn_fun(nth, "nth", "(n list)") {
    handle->assert_type(TAG_NUM, args[0]);
    auto num = vnumber(args[0]);
    if (num != (i64) num) {
        handle->runtime_error("Argument must be an integer.");
    }
    return handle->v_nth(i64(num), args[1]);
}

fn_fun(list_q, "list?", "(x)") {
    if (args[0].is_empty() || args[0].is_cons()) {
        return V_TRUE;
    } else {
        return V_FALSE;
    }
}

fn_fun(empty_q, "empty?", "(x)") {
    switch (v_tag(args[0])) {
    case TAG_CONS:
        return V_FALSE;
    case TAG_EMPTY:
        return V_TRUE;
    case TAG_TABLE:
        return vtable(args[0])->contents.get_size() == 0 ? V_TRUE : V_FALSE;
    default:
        handle->runtime_error("empty? argument must be a list or a table.");
        return V_NIL;
    }
}

fn_fun(length, "length", "(x)") {
    return handle->v_length(args[0]);
}

fn_fun(concat, "concat", "(& colls)") {
    if (args[0] == V_EMPTY) {
        return V_EMPTY;
    }
    auto res = v_head(args[0]);
    if (res.is_empty() || res.is_cons()) {
        fn_for_list(it, v_tail(args[0])) {
            auto hd = v_head(it);
            res = handle->list_concat(res, hd);
        }
    } else if (res.is_string()) {
        fn_for_list(it, v_tail(args[0])) {
            auto hd = v_head(it);
            res = handle->string_concat(res, hd);
        }
    // } else if (v_tag(res)->is_table()) {
    //     fn_for_list(it, v_tail(args[0])) {
    //         auto hd = v_head(it);
    //         res = handle->table_join(res, hd);
    //     }
    } else {
        handle->runtime_error("join arguments must be collections");
    }
    return res;
}

fn_fun(Table, "Table", "(& args)") {
    auto res = handle->ws->add_table();
    fn_for_list(it, args[0]) {
        auto tl = v_tail(it);
        if (tl == V_EMPTY) {
            handle->runtime_error("Table requires an even number of arguments.");
        }
        vtable(res)->contents.insert(v_head(it), v_head(tl));
        it = tl;
    }
    return res;
}

fn_fun(get, "get", "(obj & keys)") {
    value res = args[0];
    fn_for_list(it, args[1]) {
        handle->assert_type(TAG_TABLE, res);
        auto x = vtable(res)->contents.get(v_head(it));
        if (x.has_value()) {
            res = *x;
        } else {
            res = V_NIL;
        }
    }
    return res;
}

fn_fun(table_keys, "table-keys", "(table)") {
    handle->assert_type(TAG_TABLE, args[0]);
    auto keys = vtable(args[0])->contents.keys();
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

    fn_add_builtin(inter, eq);
    fn_add_builtin(inter, gt);
    fn_add_builtin(inter, lt);
    fn_add_builtin(inter, ge);
    fn_add_builtin(inter, le);

    fn_add_builtin(inter, List);
    fn_add_builtin(inter, list_q);
    fn_add_builtin(inter, cons);
    fn_add_builtin(inter, head);
    fn_add_builtin(inter, tail);
    fn_add_builtin(inter, nth);

    fn_add_builtin(inter, Table);
    fn_add_builtin(inter, get);
    fn_add_builtin(inter, table_keys);

    // fn_add_builtin(inter, String);
    // fn_add_builtin(inter, substring);

    fn_add_builtin(inter, empty_q);
    fn_add_builtin(inter, length);
    fn_add_builtin(inter, concat);

    // at the very least, map should get a native implementation
    // fn_add_builtin(inter, map);
    // fn_add_builtin(inter, filter);
    // fn_add_builtin(inter, foldl);
    // fn_add_builtin(inter, reverse);

    // import into repl namespace
    auto& st = *inter.get_symtab();
    auto& glob = *inter.get_global_env();
    do_import(st,
            **glob.get_ns(st.intern("fn/user")),
            **glob.get_ns(st.intern("fn/builtin")),
            "");
}

}
