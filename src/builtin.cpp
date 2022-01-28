#include "builtin.hpp"

#include "vm.hpp"
#include "obj.hpp"
#include <cmath>

namespace fn {

#define fn_fun(name, namestr, params) \
    const char* fn_params__ ## name = params; \
    const char* fn_name__ ## name = namestr; \
    static void fn__ ## name(istate* S)
#define fn_add_builtin(S, name) \
    def_foreign_fun(S, fn_name__ ## name, fn_params__ ## name, \
            fn__ ## name );
#define fn_for_list(var, lst) \
    for (auto var = lst; var != V_EMPTY; var = vtail(var))

static void def_foreign_fun(istate* S, const string& name, const string& params,
        void (*foreign)(istate*)) {
    push_foreign_fun(S, foreign, params);
    mutate_global(S, intern(S, name), peek(S));
    pop(S);
}

fn_fun(eq, "=", "(x0 & args)") {
    auto x0 = get(S, 0);
    auto lst = get(S, 1);
    while (lst != V_EMPTY) {
        // FIXME: check type
        if (vcons(lst)->head != x0) {
            push(S, V_FALSE);
            return;
        }
        lst = vcons(lst)->tail;
    }
    push(S, V_TRUE);
}

fn_fun(list_q, "list?", "(x)") {
    push(S, vis_cons(peek(S)) || peek(S) == V_EMPTY ? V_TRUE : V_FALSE);
}

fn_fun(le, "<=", "(x0 & args)") {
    auto x0 = get(S, 0);
    if (!vis_number(x0)) {
        ierror(S, "Arguments to <= must be numbers.");
        return;
    }
    auto lst = get(S, 1);
    while (lst != V_EMPTY) {
        // FIXME: check type
        auto x1 = vcons(lst)->head;
        if (!vis_number(x1)) {
            ierror(S, "Arguments to <= must be numbers.");
            return;
        }
        if (vnumber(x0) > vnumber(x1)) {
            push(S, V_FALSE);
            return;
        }
        lst = vcons(lst)->tail;
    }
    push(S, V_TRUE);
}

fn_fun(ge, ">=", "(x0 & args)") {
    auto x0 = get(S, 0);
    if (!vis_number(x0)) {
        ierror(S, "Arguments to >= must be numbers.");
        return;
    }
    auto lst = get(S, 1);
    while (lst != V_EMPTY) {
        // FIXME: check type
        auto x1 = vcons(lst)->head;
        if (!vis_number(x1)) {
            ierror(S, "Arguments to >= must be numbers.");
            return;
        }
        if (vnumber(x0) < vnumber(x1)) {
            push(S, V_FALSE);
            return;
        }
        lst = vcons(lst)->tail;
    }
    push(S, V_TRUE);
}

fn_fun(ceil, "ceil", "(x)") {
    auto x0 = get(S,0);
    if (!vis_number(x0)) {
        ierror(S, "Argument to ceil must be a number.");
        return;
    }
    push_number(S, ceil(vnumber(x0)));
}

fn_fun(add, "+", "(& args)") {
    auto lst = peek(S);
    auto res = 0;
    while (lst != V_EMPTY) {
        // FIXME: check type
        if (!vis_number(vcons(lst)->head)) {
            ierror(S, "+ arguments must be numbers");
            return;
        }
        res += vnumber(vcons(lst)->head);
        lst = vcons(lst)->tail;
    }
    pop(S);
    push_number(S, res);
}

fn_fun(sub, "-", "(& args)") {
    auto lst = peek(S);
    if (lst == V_EMPTY) {
        push_number(S, 0);
        return;
    }

    if (!vis_number(vcons(lst)->head)) {
        ierror(S, "- arguments must be numbers");
        return;
    }
    auto res = vnumber(vcons(lst)->head);
    if (vcons(lst)->tail == V_EMPTY) {
        push_number(S, -vnumber(vcons(lst)->head));
    } else {
        lst = vcons(lst)->tail;
        while (lst != V_EMPTY) {
            // FIXME: check type
            if (!vis_number(vcons(lst)->head)) {
                ierror(S, "- arguments must be numbers");
                return;
            }
            res -= vnumber(vcons(lst)->head);
            lst = vcons(lst)->tail;
        }
        pop(S);
        push_number(S, res);
    }
}

fn_fun(mul, "*", "(& args)") {
    auto lst = peek(S);
    auto res = 1;
    while (lst != V_EMPTY) {
        // FIXME: check type
        if (!vis_number(vcons(lst)->head)) {
            ierror(S, "* arguments must be numbers");
            return;
        }
        res *= vnumber(vcons(lst)->head);
        lst = vcons(lst)->tail;
    }
    pop(S);
    push_number(S, res);
}

fn_fun(div, "/", "(& args)") {
    auto lst = peek(S);
    if (lst == V_EMPTY) {
        push_number(S, 1);
        return;
    }

    if (!vis_number(vcons(lst)->head)) {
        ierror(S, "/ arguments must be numbers");
        return;
    }
    auto res = vnumber(vcons(lst)->head);
    if (vcons(lst)->tail == V_EMPTY) {
        push_number(S, 1 / vnumber(vcons(lst)->head));
    } else {
        lst = vcons(lst)->tail;
        while (lst != V_EMPTY) {
            // FIXME: check type
            if (!vis_number(vcons(lst)->head)) {
                ierror(S, "/ arguments must be numbers");
                return;
            }
            res /= vnumber(vcons(lst)->head);
            lst = vcons(lst)->tail;
        }
        pop(S);
        push_number(S, res);
    }
}

fn_fun(pow, "**", "(base expt)") {
    auto base = get(S,0);
    auto expt = get(S,1);
    if (!vis_number(base) || !vis_number(expt)) {
        ierror(S, "Arguments to ** must be numbers.");
        return;
    }
    push_number(S, pow(vnumber(base), vnumber(expt)));
}



fn_fun(List, "List", "(& args)") {
    // the variadic argument takes care of this for us.
    return;
}

fn_fun(cons, "cons", "(hd tl)") {
    auto hd = get(S, 0);
    auto tl = get(S, 1);
    if (tl != V_EMPTY && !vis_cons(tl)) {
        ierror(S, "cons tail must be a list");
    }
    push_cons(S, hd, tl);
}

fn_fun(head, "head", "(x)") {
    if (!vis_cons(peek(S))) {
        ierror(S, "head argument must be a list");
    }
    push(S, vcons(peek(S))->head);
}

fn_fun(tail, "tail", "(x)") {
    if (!vis_cons(peek(S))) {
        ierror(S, "tail argument must be a list");
    }
    push(S, vcons(peek(S))->tail);
}

fn_fun(empty_q, "empty?", "(x)") {
    push(S, peek(S) == V_EMPTY ? V_TRUE : V_FALSE);
}

fn_fun(mod, "mod", "(x modulus)") {
    auto x = get(S,0);
    auto modulus = get(S,1);
    if (!vis_number(x) || !vis_number(modulus)) {
        ierror(S, "Arguments to mod must be numbers.");
        return;
    }
    auto i = (i64)floor(vnumber(x));
    auto f = vnumber(x) - i;
    auto m = vnumber(modulus);
    if (m != floor(m)) {
        ierror(S, "Modulus for mod must be an integer.");
        return;
    }
    push_number(S, (i % (i64)m) + f);
}

fn_fun(Table, "Table", "(& args)") {
    push_table(S);
    auto res = vtable(peek(S));
    fn_for_list(it, peek(S, 1)) {
        auto tl = vtail(it);
        if (tl == V_EMPTY) {
            ierror(S, "Table requires an even number of arguments.");
            return;
        }
        res->contents.insert(vhead(it), vhead(tl));
        it = tl;
    }
}

fn_fun(get, "get", "(obj & keys)") {
    push(S, get(S, 0));
    auto lst = get(S, 1);
    while (lst != V_EMPTY) {
        if (!vis_table(peek(S))) {
            ierror(S, "get can only descend on tables.");
            return;
        }
        auto x = vtable(peek(S))->contents.get(vcons(lst)->head);
        if (!x.has_value()) {
            ierror(S, "get failed: no such key.");
            return;
        }
        S->stack[S->sp-1] = *x;
        lst = vcons(lst)->tail;
    }
}

fn_fun(with_metatable, "with-metatable", "(meta tbl)") {
    if(!vis_table(get(S,0)) || !vis_table(get(S,1))) {
        ierror(S, "with-metatable arguments must be tables.");
        return;
    }
    vtable(get(S,1))->metatable = get(S,0);
}

fn_fun(metatable, "metatable", "(table)") {
    if(!vis_table(get(S,0))) {
        ierror(S, "metatable argument must be a table.");
        return;
    }
    push(S, vtable(get(S,0))->metatable);
}

void install_builtin(istate* S) {
    fn_add_builtin(S, eq);
    // fn_add_builtin(S, same_q);

    // fn_add_builtin(S, number_q);
    // fn_add_builtin(S, string_q);
    fn_add_builtin(S, list_q);
    // fn_add_builtin(S, table_q);
    // fn_add_builtin(S, function_q);
    // fn_add_builtin(S, symbol_q);
    // fn_add_builtin(S, bool_q);

    // fn_add_builtin(S, intern);
    // fn_add_builtin(S, symname);
    // fn_add_builtin(S, gensym);

    fn_add_builtin(S, add);
    fn_add_builtin(S, sub);
    fn_add_builtin(S, mul);
    fn_add_builtin(S, div);
    fn_add_builtin(S, pow);

    // fn_add_builtin(S, integer_q);
    // fn_add_builtin(S, floor);
    fn_add_builtin(S, ceil);
    // fn_add_builtin(S, frac_part);

    // fn_add_builtin(S, gt);
    // fn_add_builtin(S, lt);
    fn_add_builtin(S, ge);
    fn_add_builtin(S, le);

    // fn_add_builtin(S, String);
    // fn_add_builtin(S, substring);

    // fn_add_builtin(S, fn_not);

    fn_add_builtin(S, List);
    fn_add_builtin(S, cons);
    fn_add_builtin(S, head);
    fn_add_builtin(S, tail);
    // fn_add_builtin(S, nth);

    // fn_add_builtin(S, length);
    // fn_add_builtin(S, concat);
    fn_add_builtin(S, empty_q);

    // fn_add_builtin(S, abs);
    // fn_add_builtin(S, exp);
    // fn_add_builtin(S, log);
    fn_add_builtin(S, mod);

    fn_add_builtin(S, Table);
    fn_add_builtin(S, get);
    // fn_add_builtin(S, get_default);
    // fn_add_builtin(S, has_key_q);
    // fn_add_builtin(S, get_keys);

    fn_add_builtin(S, metatable);
    fn_add_builtin(S, with_metatable);

    // fn_add_builtin(S, error);

    // these should be replaced with proper I/O facilities
    // fn_add_builtin(S, print);
    // fn_add_builtin(S, println);
}

}

