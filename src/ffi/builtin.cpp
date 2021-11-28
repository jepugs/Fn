#include "ffi/builtin.hpp"
#include "ffi/fn_handle.hpp"
#include "vm.hpp"

#include <cmath>

#define fn_fun(name, namestr, params) \
    const char* fn_params__ ## name = params; \
    const char* fn_name__ ## name = namestr; \
    static value fn__ ## name(fn_handle* h, value* args)
#define fn_add_builtin(inter, name) \
    (inter).add_builtin_function( fn_name__ ## name, fn_params__ ## name, \
            fn__ ## name );
#define fn_for_list(var, lst) \
    for (auto var = lst; v_tag(var) != TAG_EMPTY; var = vtail(var))


namespace fn {

fn_fun(add, "+", "(& args)") {
    f64 res = 0;
    // don't need to check if args is a list b/c of param list
    fn_for_list(it, args[0]) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
        res += vnumber(hd);
    }
    return vbox_number(res);
}

fn_fun(sub, "-", "(& args)") {
    if (args[0] == V_EMPTY) {
        return vbox_number(0.0);
    } else if (vtail(args[0]) == V_EMPTY) {
        auto x = vhead(args[0]);
        h->assert_type(TAG_NUM, x);
        if (h->failed()) {
            return V_NIL;
        }
        return vbox_number(-vnumber(x));
    }

    auto x = vhead(args[0]);
    h->assert_type(TAG_NUM, x);
    if (h->failed()) {
        return V_NIL;
    }
    auto res = vnumber(x);
    fn_for_list(it, vtail(args[0])) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
        res -= vnumber(hd);
    }
    return vbox_number(res);
}

fn_fun(mul, "*", "(& args)") {
    f64 res = 1;
    fn_for_list(it, args[0]) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
        res *= vnumber(hd);
    }
    return vbox_number(res);
}

fn_fun(div, "/", "(& args)") {
    if (args[0] == V_EMPTY) {
        return vbox_number(1.0);
    } else if (vtail(args[0]) == V_EMPTY) {
        auto x = vhead(args[0]);
        h->assert_type(TAG_NUM, x);
        if (h->failed()) {
            return V_NIL;
        }
        return vbox_number(1/vnumber(x));
    }

    auto x = vhead(args[0]);
    h->assert_type(TAG_NUM, x);
    if (h->failed()) {
        return V_NIL;
    }
    auto res = vnumber(x);
    fn_for_list(it, vtail(args[0])) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
        res /= vnumber(hd);
    }
    return vbox_number(res);
}

fn_fun(abs, "abs", "(x)") {
    h->assert_type(TAG_NUM, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    if (h->failed()) {
        return V_NIL;
    }
    return vbox_number(fabs(vnumber(args[0])));
}

fn_fun(mod, "mod", "(x y)") {
    h->assert_type(TAG_NUM, args[0]);
    h->assert_type(TAG_NUM, args[1]);
    if (h->failed()) {
        return V_NIL;
    }
    auto x = vnumber(args[0]);
    auto y = vnumber(args[1]);
    u64 x_int = (u64)x;
    u64 y_int = (u64)y;
    if (y != y_int) {
        h->error("modulus must be an integer");
    }
    return vbox_number((f64)(x_int % y_int) + (x - x_int));
}

fn_fun(pow, "**", "(x y)") {
    h->assert_type(TAG_NUM, args[0]);
    h->assert_type(TAG_NUM, args[1]);
    if (h->failed()) {
        return V_NIL;
    }
    return vbox_number(pow(vnumber(args[0]), vnumber(args[1])));
}

fn_fun(exp, "exp", "(x)") {
    h->assert_type(TAG_NUM, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    return vbox_number(exp(vnumber(args[0])));
}

fn_fun(log, "log", "(x)") {
    h->assert_type(TAG_NUM, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    return vbox_number(log(vnumber(args[0])));
}

fn_fun(number_q, "number?", "(x)") {
    return vis_number(args[0]) ? V_TRUE : V_FALSE;
}

fn_fun(integer_q, "integer?", "(x)") {
    if (!vis_number(args[0])) {
        return V_FALSE;
    }
    auto num = vnumber(args[0]);
    return (num == (i64) num) ? V_TRUE : V_FALSE;
}

fn_fun(eq, "=", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto x = vhead(args[0]);
    fn_for_list(it, vtail(args[0])) {
        if (vhead(it) != x) {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun(lt, "<", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }

    auto prev = vhead(args[0]);
    h->assert_type(TAG_NUM, prev);
    if (h->failed()) {
        return V_NIL;
    }

    fn_for_list(it, vtail(args[0])) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
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

    auto prev = vhead(args[0]);
    h->assert_type(TAG_NUM, prev);
    if (h->failed()) {
        return V_NIL;
    }

    fn_for_list(it, vtail(args[0])) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
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
    h->assert_type(TAG_NUM, prev);
    if (h->failed()) {
        return V_NIL;
    }

    fn_for_list(it, vtail(args[0])) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
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

    auto prev = vhead(args[0]);
    h->assert_type(TAG_NUM, prev);
    if (h->failed()) {
        return V_NIL;
    }

    fn_for_list(it, vtail(args[0])) {
        auto hd = vhead(it);
        h->assert_type(TAG_NUM, hd);
        if (h->failed()) {
            return V_NIL;
        }
        if (vnumber(prev) >= vnumber(hd)) {
            prev = hd;
        } else {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun (fn_not, "not", "(x)") {
    return vtruth(args[0]) ? V_FALSE : V_TRUE;
        
}


fn_fun(List, "List", "(& args)") {
    return args[0];
}

fn_fun(cons, "cons", "(hd tl)") {
    h->assert_list(args[1]);
    if (h->failed()) {
        return V_NIL;
    }
    if (h->failed()) {
        return V_NIL;
    }
    return h->add_cons(args[0], args[1]);
}

fn_fun(head, "head", "(x)") {
    if (args[0] == V_EMPTY) {
        return V_NIL;
    } else {
        h->assert_type(TAG_CONS, args[0]);
        if (h->failed()) {
            return V_NIL;
        }
        return vcons(args[0])->head;
    }
}

fn_fun(tail, "tail", "(x)") {
    if (args[0] == V_EMPTY) {
        return V_NIL;
    } else {
        h->assert_type(TAG_CONS, args[0]);
        if (h->failed()) {
            return V_NIL;
        }
        return vcons(args[0])->tail;
    }
}

fn_fun(nth, "nth", "(n list)") {
    h->assert_type(TAG_NUM, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    auto num = vnumber(args[0]);
    if (num != (i64) num) {
        h->error("Argument must be an integer.");
    }
    auto n = (i64)num;

    h->assert_type(TAG_CONS, args[1]);
    if (h->failed()) {
        return V_NIL;
    }
    auto lst = args[1];
    while (n != 0) {
        if (lst == V_EMPTY) {
            return V_NIL;
        }
        lst = vtail(lst);
        --n;
    }
    return vhead(lst);
}

fn_fun(list_q, "list?", "(x)") {
    if (args[0] == V_EMPTY || vis_cons(args[0])) {
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
        h->error("empty? argument must be a list or a table.");
        return V_NIL;
    }
}

// fn_fun(length, "length", "(x)") {
//     return h->v_length(args[0]);
// }

fn_fun(concat, "concat", "(& colls)") {
    if (args[0] == V_EMPTY) {
        return V_EMPTY;
    }
    auto res = vhead(args[0]);
    if (res == V_EMPTY || vis_cons(res)) {
        fn_for_list(it, vtail(args[0])) {
            auto hd = vhead(it);
            h->assert_list(hd);
            if (h->failed()) {
                return V_NIL;
            }
            res = h->list_concat(res, hd);
        }
    } else if (vis_string(res)) {
        fn_for_list(it, vtail(args[0])) {
            auto hd = vhead(it);
            h->assert_type(TAG_STRING, hd);
            if (h->failed()) {
                return V_NIL;
            }
            res = h->string_concat(res, hd);
        }
    // } else if (res.is_table()) {
    //     fn_for_list(it, vtail(args[0])) {
    //         auto hd = vhead(it);
    //         res = h->table_join(res, hd);
    //     }
    } else {
        h->error("join arguments must be collections");
    }
    return res;
}

fn_fun(print, "print", "(x)") {
    std::cout << h->as_string(args[0]);
    return V_NIL;
}

fn_fun(println, "println", "(x)") {
    std::cout << h->as_string(args[0])
              << '\n';
    return V_NIL;
}

fn_fun(Table, "Table", "(& args)") {
    auto res = h->add_table();
    fn_for_list(it, args[0]) {
        auto tl = vtail(it);
        if (tl == V_EMPTY) {
            h->error("Table requires an even number of arguments.");
            return V_NIL;
        }
        vtable(res)->contents.insert(vhead(it), vhead(tl));
        it = tl;
    }
    return res;
}

// fn_fun(get, "get", "(obj & keys)") {
//     value res = args[0];
//     fn_for_list(it, args[1]) {
//         h->assert_type(TAG_TABLE, res);
//         auto x = vtable(res)->contents.get(vhead(it));
//         if (x.has_value()) {
//             res = *x;
//         } else {
//             res = V_NIL;
//         }
//     }
//     return res;
// }

// fn_fun(table_keys, "table-keys", "(table)") {
//     h->assert_type(TAG_TABLE, args[0]);
//     auto keys = vtable(args[0])->contents.keys();
//     auto res = V_EMPTY;
//     for (auto k : keys) {
//         res = h->ws->add_cons(k, res);
//     }
//     return res;
// }

fn_fun(gensym, "gensym", "()") {
    return h->gensym();
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

    //fn_add_builtin(inter, bool_q);
    //fn_add_builtin(inter, symbol_q);

    fn_add_builtin(inter, fn_not);

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
    // fn_add_builtin(inter, get);
    // fn_add_builtin(inter, table_keys);

    // fn_add_builtin(inter, String);
    // fn_add_builtin(inter, substring);

    fn_add_builtin(inter, empty_q);
    // fn_add_builtin(inter, length);
    fn_add_builtin(inter, concat);

    // these should be replaced
    fn_add_builtin(inter, print);
    fn_add_builtin(inter, println);

    // symbol things
    //fn_add_builtin(inter, intern);
    //fn_add_builtin(inter, symbol_name);
    fn_add_builtin(inter, gensym);

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
