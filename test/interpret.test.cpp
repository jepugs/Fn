#define BOOST_TEST_MODULE Parser Test Module
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

    v = inter.interpret_string("\"my string\"", ns_id, &ws, &err);
    BOOST_TEST((v == ws.add_string("my string")));

    v = inter.interpret_string("'my-sym", ns_id, &ws, &err);
    BOOST_TEST((v == vbox_symbol(inter.intern("my-sym"))));
}

// TODO: lots :'(
