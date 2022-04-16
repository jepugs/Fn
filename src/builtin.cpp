#include "builtin.hpp"

#include "namespace.hpp"
#include "obj.hpp"
#include "vm.hpp"
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
#define get(S, n) ((S->stack[S->bp + ((n))]))


static void def_foreign_fun(istate* S, const string& name, const string& params,
        void (*foreign)(istate*)) {
    auto fullname = symname(S, S->ns_id) + ":" + name;
    push_foreign_fun(S, foreign, fullname, params);
    set_global(S, resolve_symbol(S, intern_id(S, name)), peek(S));
    pop(S);
}

fn_fun(require, "require", "(spec)") {
    if (!vis_string(peek(S))) {
        ierror(S, "require spec must be a string.");
        return;
    }
    load_file_or_package(S, convert_fn_string(vstring(peek(S))));
}

fn_fun(eq, "=", "(x0 & args)") {
    for (u32 i = S->bp+1; i < S->sp; ++i) {
        if (S->stack[i] != get(S,0)) {
            push(S, V_NO);
            return;
        }
    }
    push(S, V_YES);
}

fn_fun(same_q, "same?", "(x0 & args)") {
    for (u32 i = S->bp+1; i < S->sp; ++i) {
        if (!vsame(S->stack[i], get(S,0))) {
            push(S, V_NO);
            return;
        }
    }
    push(S, V_YES);
}

fn_fun(list_q, "list?", "(x)") {
    push(S, vis_cons(peek(S)) || peek(S) == V_EMPTY ? V_YES : V_NO);
}

fn_fun(symbol_q, "symbol?", "(x)") {
    push(S, vis_symbol(peek(S)) ? V_YES : V_NO);
}

fn_fun(le, "<=", "(x0 & args)") {
    auto x0 = get(S, 0);
    if (!vis_number(x0)) {
        ierror(S, "Arguments to <= not a number.");
        return;
    }
    auto n = vnumber(x0);
    for (u32 i = S->bp; i < S->sp; ++i) {
        auto x1 = S->stack[i];
        if (!vis_number(x1)) {
            ierror(S, "Arguments to <= not a number.");
            return;
        }
        auto m = vnumber(x1);
        if (n > m) {
            push(S, V_NO);
            return;
        }
        n = m;
    }
    push(S, V_YES);
}

fn_fun(ge, ">=", "(x0 & args)") {
    auto x0 = get(S, 0);
    if (!vis_number(x0)) {
        ierror(S, "Arguments to >= not a number.");
        return;
    }
    auto n = vnumber(x0);
    for (u32 i = S->bp; i < S->sp; ++i) {
        auto x1 = S->stack[i];
        if (!vis_number(x1)) {
            ierror(S, "Arguments to >= not a number.");
            return;
        }
        auto m = vnumber(x1);
        if (n < m) {
            push(S, V_NO);
            return;
        }
        n = m;
    }
    push(S, V_YES);
}

fn_fun(ceil, "ceil", "(x)") {
    auto x0 = get(S,0);
    if (!vis_number(x0)) {
        ierror(S, "Argument to ceil not a number.");
        return;
    }
    push_number(S, ceil(vnumber(x0)));
}

fn_fun(intern, "intern", "(str)") {
    if (!vis_string(peek(S))) {
        ierror(S, "Argument to intern not a string.");
        return;
    }
    push_symbol(S, intern_id(S, convert_fn_string(vstring(peek(S)))));
}

fn_fun(symname, "symname", "(sym)") {
    if (!vis_symbol(peek(S))) {
        ierror(S, "Argument to symname not a symbol.");
        return;
    }
    push_string(S, symname(S, vsymbol(peek(S))));
}

fn_fun(gensym, "gensym", "()") {
    push_symbol(S, gensym_id(S));
}

fn_fun(add, "+", "(& args)") {
    f64 res = 0;
    for (u32 i = S->bp; i < S->sp; ++i) {
        auto v = S->stack[i];
        if (!vis_number(v)) {
            ierror(S, "Argument to + not a number.");
            return;
        }
        res += vnumber(v);
    }
    push_number(S, res);
}

fn_fun(sub, "-", "(& args)") {
    f64 res = 0;
    if (S->sp - S->bp == 0) {
        push_number(S, 0);
        return;
    } else {
        auto v = S->stack[S->bp];
        if (!vis_number(v)) {
            ierror(S, "Argument to - not a number.");
            return;
        }
        res = vnumber(v);

    }
    // arity 1 => perform negation
    if (S->sp - S->bp == 1) {
        push_number(S, -res);
        return;
    }
    for (u32 i = S->bp + 1; i < S->sp; ++i) {
        auto v = S->stack[i];
        if (!vis_number(v)) {
            ierror(S, "Argument to - not a number.");
            return;
        }
        res -= vnumber(v);
    }
    push_number(S, res);
}

fn_fun(mul, "*", "(& args)") {
    f64 res = 1;
    for (u32 i = S->bp; i < S->sp; ++i) {
        auto v = S->stack[i];
        if (!vis_number(v)) {
            ierror(S, "Argument to * not a number.");
            return;
        }
        res *= vnumber(v);
    }
    push_number(S, res);
}

fn_fun(div, "/", "(& args)") {
    f64 res = 1;
    if (S->sp - S->bp == 0) {
        push_number(S, 1);
        return;
    } else {
        auto v = S->stack[S->bp];
        if (!vis_number(v)) {
            ierror(S, "Argument to / not a number.");
            return;
        }
        res = vnumber(v);

    }
    // arity 1 => take inverse
    if (S->sp - S->bp == 1) {
        push_number(S, 1/res);
        return;
    }
    for (u32 i = S->bp + 1; i < S->sp; ++i) {
        auto v = S->stack[i];
        if (!vis_number(v)) {
            ierror(S, "Argument to / not a number.");
            return;
        }
        res /= vnumber(v);
    }
    push_number(S, res);
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

fn_fun(fn_not, "not", "(arg)") {
    push(S, vtruth(peek(S)) ? V_NO : V_YES);
}

fn_fun(List, "List", "(& args)") {
    pop_to_list(S, S->sp - S->bp);
}

fn_fun(cons, "cons", "(hd tl)") {
    auto tl = get(S, 1);
    if (tl != V_EMPTY && !vis_cons(tl)) {
        ierror(S, "cons tail must be a list");
    }
    push_cons(S, S->bp, S->bp + 1);
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
        return;
    }
    push(S, vcons(peek(S))->tail);
}

fn_fun(empty_q, "empty?", "(x)") {
    push(S, peek(S) == V_EMPTY ? V_YES : V_NO);
}

// fn_fun(concat2, "concat2", "(l r)") {
//     if (!vis_list(get(S,0)) || !vis_list(get(S, 1))) {
//         ierror(S, "concat2 arguments must be lists.");
//         return;
//     }
// }

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
    push_table(S, get_frame_pointer(S));
}

fn_fun(get, "get", "(obj & keys)") {
    push(S, get(S, 0));
    for (u32 i = S->bp+1; i+1 < S->sp; ++i) {
        if (!vis_table(peek(S))) {
            ierror(S, "get can only descend on tables.");
            return;
        }

        auto x = table_get(vtable(peek(S)), S->stack[i]);
        if (!x) {
            ierror(S, "get failed: no such key.");
            return;
        }
        S->stack[S->sp-1] = x[1];
    }
}

fn_fun(set_metatable, "set-metatable", "(meta tbl)") {
    if(!vis_table(get(S,0)) || !vis_table(get(S,1))) {
        ierror(S, "set-metatable arguments must be tables.");
        return;
    }
    vtable(get(S,1))->metatable = get(S,0);
}

fn_fun(metatable, "metatable", "(table)") {
    push(S, get_metatable(S, peek(S)));
}

fn_fun(error, "error", "(msg)") {
    ierror(S, v_to_string(peek(S), S->symtab));
}

fn_fun(println, "println", "(str)") {
    print_top(S);
}

fn_fun(macroexpand_1, "macroexpand-1", "(form)") {
    if (!vis_cons(peek(S))) {
        return;
    }
    if (vis_symbol(vhead(peek(S)))) {
        auto sym = vsymbol(vhead(peek(S)));
        if (push_macro(S, sym)) {
            auto v = vtail(peek(S, 1));
            u32 n = 0;
            while (!vis_emptyl(v)) {
                push(S, vhead(v));
                ++n;
                v = vtail(v);
            }
            call(S, n);
        }
    }
}

fn_fun(def_list_meta, "def-list-meta", "(x)") {
    S->G->list_meta = peek(S);
}

fn_fun(def_string_meta, "def-string-meta", "(x)") {
    S->G->string_meta = peek(S);
}

void install_internal(istate* S) {
    auto save_ns = S->ns_id;
    switch_ns(S, cached_sym(S, SC_FN_INTERNAL));
    fn_add_builtin(S, require);
    fn_add_builtin(S, eq);
    fn_add_builtin(S, same_q);

    // fn_add_builtin(S, number_q);
    // fn_add_builtin(S, string_q);
    fn_add_builtin(S, list_q);
    // fn_add_builtin(S, table_q);
    // fn_add_builtin(S, function_q);
    fn_add_builtin(S, symbol_q);
    // fn_add_builtin(S, bool_q);

    fn_add_builtin(S, intern);
    fn_add_builtin(S, symname);
    fn_add_builtin(S, gensym);

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

    fn_add_builtin(S, fn_not);

    fn_add_builtin(S, List);
    fn_add_builtin(S, cons);
    fn_add_builtin(S, head);
    fn_add_builtin(S, tail);
    // fn_add_builtin(S, nth);

    // fn_add_builtin(S, length);
    // fn_add_builtin(S, concat2);
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
    fn_add_builtin(S, set_metatable);

    fn_add_builtin(S, error);

    // these should be replaced with proper I/O facilities
    // fn_add_builtin(S, print);
    fn_add_builtin(S, println);

    fn_add_builtin(S, macroexpand_1);

    // set up builtin metatables
    fn_add_builtin(S, def_list_meta);
    fn_add_builtin(S, def_string_meta);

    S->ns_id = save_ns;
    copy_defs(S, get_ns(S, save_ns),
            get_ns(S, cached_sym(S, SC_FN_BUILTIN)), "");
}

void install_builtin(istate* S) {
    install_internal(S);
    load_file_or_package(S, string{DEFAULT_PKG_ROOT} + "/fn.builtin");
}

}
