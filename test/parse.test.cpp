#define BOOST_TEST_MODULE Parser Test Module
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#include <sstream>

#include "base.hpp"
#include "parse.hpp"
#include "scan.hpp"

using namespace fn;
using namespace fn_parse;

BOOST_AUTO_TEST_CASE( parse_number_test ) {
    symbol_table symtab;
    fault err;

    auto forms = parse_string("2.0", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    auto test = forms[0];
    BOOST_TEST(test->kind == ak_number_atom);
    BOOST_TEST(test->datum.num == 2.0);
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 3);
    free_ast_form(test);

    forms = parse_string("   -1.0  ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_number_atom);
    BOOST_TEST(test->datum.num == -1.0);
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 7);
    free_ast_form(test);
}

BOOST_AUTO_TEST_CASE( parse_string_test ) {
    symbol_table symtab;
    fault err;

    auto forms = parse_string("\"hello\"", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    auto test = forms[0];
    BOOST_TEST(test->kind == ak_string_atom);
    BOOST_TEST(test->datum.str->as_string() == "hello");
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 7);
    free_ast_form(test);

    forms = parse_string("   \"world!\"  ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_string_atom);
    BOOST_TEST(test->datum.str->as_string() == "world!");
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 11);
    free_ast_form(test);

    forms = parse_string("\"\n\"  ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_string_atom);
    BOOST_TEST(test->datum.str->as_string() == "\n");
    free_ast_form(test);

    forms = parse_string("\"\a\r\t\v\"  ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_string_atom);
    BOOST_TEST(test->datum.str->as_string() == "\a\r\t\v");
    free_ast_form(test);
}

BOOST_AUTO_TEST_CASE( parse_symbol_test ) {
    symbol_table symtab;
    fault err;

    auto forms = parse_string("abc", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    auto test = forms[0];
    BOOST_TEST(test->kind == ak_symbol_atom);
    BOOST_TEST(test->datum.sym == symtab.intern("abc"));
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 3);
    free_ast_form(test);

    forms = parse_string("   abc  ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_symbol_atom);
    BOOST_TEST(test->datum.sym == symtab.intern("abc"));
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 6);
    free_ast_form(test);

    // test escapes
    forms = parse_string("\\\\ ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_symbol_atom);
    BOOST_TEST(test->datum.sym == symtab.intern("\\"));
    free_ast_form(test);

    forms = parse_string("\\123 ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_symbol_atom);
    BOOST_TEST(test->datum.sym == symtab.intern("123"));
    free_ast_form(test);

    forms = parse_string("\\+2 ", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_symbol_atom);
    BOOST_TEST(test->datum.sym == symtab.intern("+2"));
    free_ast_form(test);
}

BOOST_AUTO_TEST_CASE( parse_list_test ) {
    symbol_table symtab;
    fault err;

    auto forms = parse_string("()", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    auto test = forms[0];
    BOOST_TEST(test->kind == ak_list);
    BOOST_TEST(test->list_length == 0);
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 2);
    free_ast_form(test);

    forms = parse_string("(0 sym \"str\" ())", &symtab, &err);
    BOOST_TEST(forms.size == 1);
    test = forms[0];
    BOOST_TEST(test->kind == ak_list);
    BOOST_TEST(test->list_length == 4);
    BOOST_TEST(test->datum.list[3]->kind == ak_list);
    BOOST_TEST(test->loc.line == 1);
    BOOST_TEST(test->loc.col == 16);
    free_ast_form(test);
}

// TODO: compound parsing tests
