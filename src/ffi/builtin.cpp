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

fn_fun(same_q, "same?", "(& args)") {
    if (args[0] == V_EMPTY) {
        return V_TRUE;
    }
    auto x = vhead(args[0]);
    fn_for_list(it, vtail(args[0])) {
        if (!vsame(vhead(it),x)) {
            return V_FALSE;
        }
    }
    return V_TRUE;
}

fn_fun(number_q, "number?", "(x)") {
    return vis_number(args[0]) ? V_TRUE : V_FALSE;
}

fn_fun(string_q, "string?", "(x)") {
    return vis_string(args[0]) ? V_TRUE : V_FALSE;
}

fn_fun(list_q, "list?", "(x)") {
    if (args[0] == V_EMPTY || vis_cons(args[0])) {
        return V_TRUE;
    } else {
        return V_FALSE;
    }
}

fn_fun(table_q, "table?", "(x)") {
    return vis_table(args[0]) ? V_TRUE : V_FALSE;
}
fn_fun(function_q, "function?", "(x)") {
    return vis_function(args[0]) ? V_TRUE : V_FALSE;
}
fn_fun(symbol_q, "symbol?", "(x)") {
    return vis_symbol(args[0]) ? V_TRUE : V_FALSE;
}
fn_fun(bool_q, "bool?", "(x)") {
    return vis_bool(args[0]) ? V_TRUE : V_FALSE;
}

fn_fun(intern, "intern", "(str)") {
    h->assert_type(TAG_STRING, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    return h->intern(vstring(args[0])->data);
}

fn_fun(symname, "symname", "(sym)") {
    h->assert_type(TAG_SYM, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    auto str = ((vm_thread*)h->vm)->get_symtab()->symbol_name(vsymbol(args[0]));
    return h->add_string(str);
}

fn_fun(gensym, "gensym", "()") {
    return h->gensym();
}

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

fn_fun(integer_q, "integer?", "(x)") {
    if (!vis_number(args[0])) {
        return V_FALSE;
    }
    auto num = vnumber(args[0]);
    return (num == (i64) num) ? V_TRUE : V_FALSE;
}

fn_fun(floor, "floor", "(x)") {
    h->assert_type(TAG_NUM, args[0]);
    return vbox_number(floor(vnumber(args[0])));
}

fn_fun(ceil, "ceil", "(x)") {
    h->assert_type(TAG_NUM, args[0]);
    return vbox_number(ceil(vnumber(args[0])));
}

fn_fun(frac_part, "frac-part", "(x)") {
    h->assert_type(TAG_NUM, args[0]);
    return vbox_number(vnumber(args[0]) - floor(vnumber(args[0])));
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

fn_fun (String, "String", "(& args)") {
    if (args[0] == V_EMPTY) {
        return h->add_string("");
    }

    auto res = v_to_string(vhead(args[0]), ((vm_thread*)h->vm)->get_symtab());
    fn_for_list(it, vtail(args[0])) {
        res = res + v_to_string(vhead(it),
                ((vm_thread*)h->vm)->get_symtab());
    }
    return h->add_string(res);
}

fn_fun (substring, "substring", "(str pos len)") {
    h->assert_type(TAG_STRING, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    h->assert_integer(args[1]);
    if (h->failed()) {
        return V_NIL;
    }
    h->assert_integer(args[2]);
    if (h->failed()) {
        return V_NIL;
    }

    auto str = vstring(args[0]);
    auto pos = vnumber(args[1]);
    auto len_arg = vnumber(args[2]);
    auto len = len_arg;
    if (len_arg < 0) {
        auto end = str->len + len_arg;
        len = end - pos + 1;
        if (len < 0) {
            len = 0;
        }
    } else if (len + pos > str->len) {
        len = str->len - pos;
    }
    return h->substr(args[0], pos, len);
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

fn_fun(length, "length", "(x)") {
    auto tag = vtag(args[0]);
    if (tag == TAG_CONS) {
        u64 res = 0;
        fn_for_list(it, args[0]) {
            ++res; 
        }
        return vbox_number(res);
    } else if (tag == TAG_EMPTY) {
        return vbox_number(0);
    } else if (tag == TAG_TABLE) {
        return vbox_number(vtable(args[0])->contents.get_size());
    } else if (tag == TAG_STRING) {
        return vbox_number(vstring(args[0])->len);
    }

    h->error("length argument must be a string or collection.");
    return V_NIL;
}

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
    } else if (vis_table(res)) {
        fn_for_list(it, vtail(args[0])) {
            auto hd = vhead(it);
            res = h->table_concat(res, hd);
        }
    } else {
        h->error("concat arguments must be collections");
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

fn_fun(get, "get", "(table & keys)") {
    value res = args[0];
    fn_for_list(it, args[1]) {
        h->assert_type(TAG_TABLE, res);
        if (h->failed()) {
            return V_NIL;
        }
        auto x = vtable(res)->contents.get(vhead(it));
        if (x.has_value()) {
            res = *x;
        } else {
            res = V_NIL;
        }
    }
    return res;
}

fn_fun(get_default, "get-default", "(table key default)") {
    h->assert_type(TAG_TABLE, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    auto x = vtable(args[0])->contents.get(args[1]);
    if (x.has_value()) {
        return *x;
    } else {
        return args[2];
    }
}

fn_fun(has_key_q, "has-key?", "(table key)") {
    h->assert_type(TAG_TABLE, args[0]);
    if (h->failed()) {
        return V_NIL;
    }
    auto x = vtable(args[0])->contents.get(args[1]);
    if (x.has_value()) {
        return V_TRUE;
    } else {
        return V_FALSE;
    }
}

fn_fun(get_keys, "get-keys", "(table)") {
    h->assert_type(TAG_TABLE, args[0]);
    auto keys = vtable(args[0])->contents.keys();
    auto res = V_EMPTY;
    for (auto k : keys) {
        res = h->add_cons(k, res);
    }
    return res;
}

fn_fun(error, "error", "(message)") {
    h->assert_type(TAG_STRING, args[0]);
    h->error(vstring(args[0])->data);
    return V_NIL;
}


void install_builtin(interpreter& inter) {
    fn_add_builtin(inter, eq);
    fn_add_builtin(inter, same_q);

    fn_add_builtin(inter, number_q);
    fn_add_builtin(inter, string_q);
    fn_add_builtin(inter, list_q);
    fn_add_builtin(inter, table_q);
    fn_add_builtin(inter, function_q);
    fn_add_builtin(inter, symbol_q);
    fn_add_builtin(inter, bool_q);

    // symbol things
    fn_add_builtin(inter, intern);
    fn_add_builtin(inter, symname);
    fn_add_builtin(inter, gensym);

    fn_add_builtin(inter, add);
    fn_add_builtin(inter, sub);
    fn_add_builtin(inter, mul);
    fn_add_builtin(inter, div);
    fn_add_builtin(inter, pow);

    fn_add_builtin(inter, abs);
    fn_add_builtin(inter, exp);
    fn_add_builtin(inter, log);
    fn_add_builtin(inter, mod);

    fn_add_builtin(inter, integer_q);
    fn_add_builtin(inter, floor);
    fn_add_builtin(inter, ceil);
    fn_add_builtin(inter, frac_part);

    fn_add_builtin(inter, gt);
    fn_add_builtin(inter, lt);
    fn_add_builtin(inter, ge);
    fn_add_builtin(inter, le);

    fn_add_builtin(inter, String);
    fn_add_builtin(inter, substring);

    fn_add_builtin(inter, fn_not);

    fn_add_builtin(inter, List);
    fn_add_builtin(inter, cons);
    fn_add_builtin(inter, head);
    fn_add_builtin(inter, tail);
    fn_add_builtin(inter, nth);

    fn_add_builtin(inter, length);
    fn_add_builtin(inter, concat);
    fn_add_builtin(inter, empty_q);

    fn_add_builtin(inter, Table);
    fn_add_builtin(inter, get);
    fn_add_builtin(inter, get_default);
    fn_add_builtin(inter, has_key_q);
    fn_add_builtin(inter, get_keys);

    fn_add_builtin(inter, error);

    // these should be replaced with proper I/O facilities
    fn_add_builtin(inter, print);
    fn_add_builtin(inter, println);


    // import remaining library from a file
    fault err;
    auto ws = inter.get_alloc()->add_working_set();
    inter.import_ns(inter.get_symtab()->intern("fn/builtin"), &ws, &err);
    // FIXME: log error as a warning
}

}
