#define BOOST_TEST_MODULE Interpreter Test Module
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#include <sstream>

#include "allocator.hpp"
#include "base.hpp"
#include "interpret.hpp"
#include "log.hpp"
#include "values.hpp"

using namespace fn;


BOOST_AUTO_TEST_CASE( interpret_immediate_test ) {
    logger log{nullptr, nullptr};
    interpreter inter{&log};
    auto ns_id = inter.get_symtab()->intern("fn/user");
    fault err;
    auto ws = inter.get_alloc()->add_working_set();

    auto v = inter.interpret_string("-2.0", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(-2.0)));

    v = inter.interpret_string("108.6", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(108.6)));

    v = inter.interpret_string("0xff.8", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(255.5)));

    v = inter.interpret_string("\"my string\"", ns_id, &ws, &err);
    BOOST_TEST((v == ws.add_string("my string")));

    v = inter.interpret_string("'my-sym", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_symbol(inter.intern("my-sym"))));

    v = inter.interpret_string("'\\123", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_symbol(inter.intern("123"))));

    v = inter.interpret_string("'Hello\\,\\ World!", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_symbol(inter.intern("Hello, World!"))));
}

BOOST_AUTO_TEST_CASE( interpret_fnargs_test ) {
    logger log{nullptr, nullptr};
    interpreter inter{&log};
    auto ns_id = inter.get_symtab()->intern("fn/user");
    fault err;
    auto ws = inter.get_alloc()->add_working_set();

    auto v = inter.interpret_string("((fn () 1.5))", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(1.5)));

    v = inter.interpret_string("((fn (x) x) -6)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(-6)));

    v = inter.interpret_string("((fn ((x 6)) x) 1.7)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(1.7)));

    v = inter.interpret_string("((fn ((x 1.7)) x))", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(1.7)));

    v = inter.interpret_string("((fn (x y z) 'sym) 1 2 3)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_symbol(inter.intern("sym"))));

    v = inter.interpret_string("((fn (x y z) x) 1 2 3)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(1)));

    v = inter.interpret_string("((fn (x y z) y) 1 2 3)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("((fn (x y z) z) 1 2 3)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(3)));

    v = inter.interpret_string("((fn (x y z & w) 'sym) 1 2 3 4 5)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_symbol(inter.intern("sym"))));

    v = inter.interpret_string("((fn (x y z & w) x) 1 2 3 4 5)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(1)));

    v = inter.interpret_string("((fn (x y z & w) y) 1 2 3 4 5)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("((fn (x y z & w) z) 1 2 3 4 5)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(3)));

    v = inter.interpret_string("((fn (x y z & w) w) 1 2 3)", ns_id, &ws, &err);
    BOOST_TEST((v == V_EMPTY));

    v = inter.interpret_string("((fn (x y z & w) w) 1 2 3 4 5)", ns_id, &ws, &err);
    BOOST_TEST((vis_cons(v)));
    BOOST_TEST((vis_cons(vtail(v))));
    BOOST_TEST((vhead(v) == vbox_number(4)));
    BOOST_TEST((vhead(vtail(v)) == vbox_number(5)));
    BOOST_TEST((vtail(vtail(v)) == V_EMPTY));
}

BOOST_AUTO_TEST_CASE( interpret_do_test ) {
    logger log{nullptr, nullptr};
    interpreter inter{&log};
    auto ns_id = inter.get_symtab()->intern("fn/user");
    fault err;
    auto ws = inter.get_alloc()->add_working_set();

    auto v = inter.interpret_string("(do)", ns_id, &ws, &err);
    BOOST_TEST((v == V_NIL));

    v = inter.interpret_string("(do 1)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(1)));

    v = inter.interpret_string("(do 1 2)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (let x 2) x)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (let x 2) (let y 3) x)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (let x 2) (let y 3) y)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(3)));

    v = inter.interpret_string("(do (do-inline (let x 2)) x)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (do-inline (let x 2) (let y 3)) x)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (do-inline (let x 2) (let y 3)) y)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(3)));

    v = inter.interpret_string("(do (do-inline (let x 2)) (let y 3) x)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (do-inline (let x 2)) (let y 3) y)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(3)));

    v = inter.interpret_string("(do (let x 2) (do-inline (let y 3)) x)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(2)));

    v = inter.interpret_string("(do (let x 2) (do-inline (let y 3)) y)", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_number(3)));
}

// TODO: lots :'(
// - test remaining special forms
// - test error generation for runtime error
// - don't test import or standard library -- these can be done in Fn directly
// - every test involving the filesystem
