#define BOOST_TEST_MODULE Scanner Test Module
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#include <sstream>

#include "base.hpp"
#include "scan.hpp"

using namespace fn;
using namespace fn_scan;

static inline void test_token(const char* str, token_kind k, int nth=0) {
    std::istringstream in(str);
    auto sc = new scanner(&in, "<test-input>");
    auto tok = sc->next_token();
    while (nth > 0) {
        tok = sc->next_token();
        --nth;
    }
    BOOST_TEST(tok.tk == k);

    delete sc;
}

static inline void test_num_token(const char* str, f64 num, int nth=0) {
    std::istringstream in(str);
    auto sc = new scanner(&in, "<test-input>");
    auto tok = sc->next_token();
    while (nth > 0) {
        tok = sc->next_token();
        --nth;
    }
    BOOST_TEST(tok.tk == tk_number);
    BOOST_TEST(tok.datum.num == num);

    delete sc;
}

static inline void test_str_token(const char* str, const char* cmp, int nth=0) {
    std::istringstream in(str);
    auto sc = new scanner(&in, "<test-input>");
    auto tok = sc->next_token();
    while (nth > 0) {
        tok = sc->next_token();
        --nth;
    }
    BOOST_TEST(tok.tk == tk_string);
    BOOST_TEST(*tok.datum.str == cmp);

    delete sc;
}

static inline void test_sym_token(const char* str, const char* cmp, int nth=0) {
    std::istringstream in(str);
    auto sc = new scanner(&in, "<test-input>");
    auto tok = sc->next_token();
    while (nth > 0) {
        tok = sc->next_token();
        --nth;
    }
    BOOST_TEST(tok.tk == tk_symbol);
    BOOST_TEST(*tok.datum.str == cmp);

    delete sc;
}

static inline void test_dot_token(const char* str, const char* cmp, int nth=0) {
    std::istringstream in(str);
    auto sc = new scanner(&in, "<test-input>");
    auto tok = sc->next_token();
    while (nth > 0) {
        tok = sc->next_token();
        --nth;
    }
    BOOST_TEST(tok.tk == tk_dot);
    BOOST_TEST(*tok.datum.str == cmp);

    delete sc;
}


BOOST_AUTO_TEST_CASE( token_test ) {
    test_token("{", tk_lbrace);
    test_token("}", tk_rbrace);
    test_token("[", tk_lbracket);
    test_token("]", tk_rbracket);
    test_token("(", tk_lparen);
    test_token(")", tk_rparen);
    test_token("${", tk_dollar_brace);
    test_token("$[", tk_dollar_bracket);
    test_token("$(", tk_dollar_paren);
    test_token("$`", tk_dollar_backtick);
    test_token("'", tk_quote);
    test_token("`", tk_backtick);
    test_token(",", tk_comma);
}

BOOST_AUTO_TEST_CASE( num_token_test ) {
    test_num_token("2", 2);
    test_num_token("+2", 2);
    test_num_token("-2.0", -2.0);
    test_num_token("-2.0e2", -200.0);
    test_num_token("+12.5e-2", 0.125);
    test_num_token("+0.5e+2", 50);
    test_num_token("0xEf", 0xEf);
    test_num_token("0xef2bca", 0xef2bca);
    test_num_token("0xEF2BCA", 0xEF2BCA);
    test_num_token("0x1.2", 0x1.2P0);
    test_num_token("0xa.b", 0xa.bP0);
    test_num_token("0xb62.ba0", 0xb62.ba0P0);
}

BOOST_AUTO_TEST_CASE( str_token_test ) { 
    test_str_token("\"\"", "");
    test_str_token("\"Hello, World!\"", "Hello, World!");
    test_str_token("\"Hello,\n\t_world!\"", "Hello,\n\t_world!");

    test_str_token("\"\\'\"", "'");
    test_str_token("\"\\?\"", "\?");
    test_str_token("\"\\\\\"", "\\");
    test_str_token("\"\\\"\"", "\"");
    test_str_token("\"\\a\"", "\a");
    test_str_token("\"\\b\"", "\b");
    test_str_token("\"\\f\"", "\f");
    test_str_token("\"\\n\"", "\n");
    test_str_token("\"\\r\"", "\r");
    test_str_token("\"\\t\"", "\t");
    test_str_token("\"\\v\"", "\v");
}

BOOST_AUTO_TEST_CASE( sym_token_test ) {
    test_sym_token("quote", "quote");
    test_sym_token("2\\.0", "2.0");
    test_sym_token("with\\ space", "with space");
    test_sym_token("\\e\\s\\c\\a\\p\\e\\!", "escape!");
}

BOOST_AUTO_TEST_CASE( dot_token_test ) {
    test_dot_token("ns.fn.core", "ns.fn.core");
    test_dot_token("pk\\.g.m\\.od", "pk\\.g.m\\.od");
    test_dot_token("\\+2.0", "\\+2.0");
}


BOOST_AUTO_TEST_CASE( displaced_token_test1 ) {
    test_token("(def x 2) {", tk_lbrace, 5);
    test_token("(def x 2) }", tk_rbrace, 5);
    test_token("(def x 2) [", tk_lbracket, 5);
    test_token("(def x 2) ]", tk_rbracket, 5);
    test_token("(def x 2) (", tk_lparen, 5);
    test_token("(def x 2) )", tk_rparen, 5);
    test_token("(def x 2) ${", tk_dollar_brace, 5);
    test_token("(def x 2) $[", tk_dollar_bracket, 5);
    test_token("(def x 2) $(", tk_dollar_paren, 5);
    test_token("(def x 2) $`", tk_dollar_backtick, 5);
    test_token("(def x 2) '", tk_quote, 5);
    test_token("(def x 2) `", tk_backtick, 5);
    test_token("(def x 2) ,", tk_comma, 5);
    test_token("(def x 2) ,@", tk_comma_at, 5);

    test_num_token("(def x 2) -1.8e4", -1.8e4, 5);
    test_str_token("(def x 2) \"hi\\n\"", "hi\n", 5);
    test_sym_token("(def x 2) sym\\ ", "sym ", 5);
}

BOOST_AUTO_TEST_CASE( displaced_token_test2 ) {
    test_token("'quot 0xef \"stri\\ng\" null {", tk_lbrace, 5);
    test_token("'quot 0xef \"stri\\ng\" null }", tk_rbrace, 5);
    test_token("'quot 0xef \"stri\\ng\" null [", tk_lbracket, 5);
    test_token("'quot 0xef \"stri\\ng\" null ]", tk_rbracket, 5);
    test_token("'quot 0xef \"stri\\ng\" null (", tk_lparen, 5);
    test_token("'quot 0xef \"stri\\ng\" null )", tk_rparen, 5);
    test_token("'quot 0xef \"stri\\ng\" null ${", tk_dollar_brace, 5);
    test_token("'quot 0xef \"stri\\ng\" null $[", tk_dollar_bracket, 5);
    test_token("'quot 0xef \"stri\\ng\" null $(", tk_dollar_paren, 5);
    test_token("'quot 0xef \"stri\\ng\" null $`", tk_dollar_backtick, 5);
    test_token("'quot 0xef \"stri\\ng\" null '", tk_quote, 5);
    test_token("'quot 0xef \"stri\\ng\" null `", tk_backtick, 5);
    test_token("'quot 0xef \"stri\\ng\" null ,", tk_comma, 5);
    test_token("'quot 0xef \"stri\\ng\" null ,@", tk_comma_at, 5);
}

// TODO: test exceptions and I/O
