#define BOOST_TEST_MODULE Scanner Test Module
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#include <sstream>

#include "base.hpp"
#include "scan.hpp"

using namespace fn;
using namespace fn_scan;

static inline void testToken(const char* str, TokenKind k, int nth=0) {
    std::istringstream in(str);
    auto sc = new Scanner(&in, "<test-input>");
    auto tok = sc->nextToken();
    while (nth > 0) {
        tok = sc->nextToken();
        --nth;
    }
    BOOST_TEST(tok.tk == k);

    delete sc;
}

static inline void testNumToken(const char* str, f64 num, int nth=0) {
    std::istringstream in(str);
    auto sc = new Scanner(&in, "<test-input>");
    auto tok = sc->nextToken();
    while (nth > 0) {
        tok = sc->nextToken();
        --nth;
    }
    BOOST_TEST(tok.tk == TKNumber);
    BOOST_TEST(tok.datum.num == num);

    delete sc;
}

static inline void testStrToken(const char* str, const char* cmp, int nth=0) {
    std::istringstream in(str);
    auto sc = new Scanner(&in, "<test-input>");
    auto tok = sc->nextToken();
    while (nth > 0) {
        tok = sc->nextToken();
        --nth;
    }
    BOOST_TEST(tok.tk == TKString);
    BOOST_TEST(*tok.datum.str == cmp);

    delete sc;
}

static inline void testSymToken(const char* str, const char* cmp, int nth=0) {
    std::istringstream in(str);
    auto sc = new Scanner(&in, "<test-input>");
    auto tok = sc->nextToken();
    while (nth > 0) {
        tok = sc->nextToken();
        --nth;
    }
    BOOST_TEST(tok.tk == TKSymbol);
    BOOST_TEST(*tok.datum.str == cmp);

    delete sc;
}

static inline void testDotToken(const char* str, const char* cmp, int nth=0) {
    std::istringstream in(str);
    auto sc = new Scanner(&in, "<test-input>");
    auto tok = sc->nextToken();
    while (nth > 0) {
        tok = sc->nextToken();
        --nth;
    }
    BOOST_TEST(tok.tk == TKDot);
    BOOST_TEST(*tok.datum.str == cmp);

    delete sc;
}


BOOST_AUTO_TEST_CASE( token_test ) {
    testToken("{", TKLBrace);
    testToken("}", TKRBrace);
    testToken("[", TKLBracket);
    testToken("]", TKRBracket);
    testToken("(", TKLParen);
    testToken(")", TKRParen);
    testToken("${", TKDollarBrace);
    testToken("$[", TKDollarBracket);
    testToken("$(", TKDollarParen);
    testToken("$`", TKDollarBacktick);
    testToken("'", TKQuote);
    testToken("`", TKBacktick);
    testToken(",", TKComma);
}

BOOST_AUTO_TEST_CASE( num_token_test ) {
    testNumToken("2", 2);
    testNumToken("+2", 2);
    testNumToken("-2.0", -2.0);
    testNumToken("-2.0e2", -200.0);
    testNumToken("+12.5e-2", 0.125);
    testNumToken("+0.5e+2", 50);
    testNumToken("0xef2bCa", 0xef2bCa);
    testNumToken("0x1.2", 0x1.2P0);
    testNumToken("0xa.b", 0xa.bP0);
    testNumToken("0xB62.ba0", 0xB62.ba0P0);
}

BOOST_AUTO_TEST_CASE( str_token_test ) { 
    testStrToken("\"\"", "");
    testStrToken("\"Hello, World!\"", "Hello, World!");
    testStrToken("\"Hello,\n\tWorld!\"", "Hello,\n\tWorld!");

    testStrToken("\"\\'\"", "'");
    testStrToken("\"\\?\"", "\?");
    testStrToken("\"\\\\\"", "\\");
    testStrToken("\"\\\"\"", "\"");
    testStrToken("\"\\a\"", "\a");
    testStrToken("\"\\b\"", "\b");
    testStrToken("\"\\f\"", "\f");
    testStrToken("\"\\n\"", "\n");
    testStrToken("\"\\r\"", "\r");
    testStrToken("\"\\t\"", "\t");
    testStrToken("\"\\v\"", "\v");
}

BOOST_AUTO_TEST_CASE( sym_token_test ) {
    testSymToken("quote", "quote");
    testSymToken("2\\.0", "2.0");
    testSymToken("with\\ space", "with space");
    testSymToken("\\e\\s\\c\\a\\p\\e\\!", "escape!");
}

BOOST_AUTO_TEST_CASE( dot_token_test ) {
    testDotToken("ns.fn.core", "ns.fn.core");
    testDotToken("pk\\.g.m\\.od", "pk\\.g.m\\.od");
    testDotToken("\\+2.0", "\\+2.0");
}


BOOST_AUTO_TEST_CASE( displaced_token_test1 ) {
    testToken("(def x 2) {", TKLBrace, 5);
    testToken("(def x 2) }", TKRBrace, 5);
    testToken("(def x 2) [", TKLBracket, 5);
    testToken("(def x 2) ]", TKRBracket, 5);
    testToken("(def x 2) (", TKLParen, 5);
    testToken("(def x 2) )", TKRParen, 5);
    testToken("(def x 2) ${", TKDollarBrace, 5);
    testToken("(def x 2) $[", TKDollarBracket, 5);
    testToken("(def x 2) $(", TKDollarParen, 5);
    testToken("(def x 2) $`", TKDollarBacktick, 5);
    testToken("(def x 2) '", TKQuote, 5);
    testToken("(def x 2) `", TKBacktick, 5);
    testToken("(def x 2) ,", TKComma, 5);
    testToken("(def x 2) ,@", TKCommaSplice, 5);

    testNumToken("(def x 2) -1.8e4", -1.8e4, 5);
    testStrToken("(def x 2) \"hi\\n\"", "hi\n", 5);
    testSymToken("(def x 2) sym\\ ", "sym ", 5);
}

BOOST_AUTO_TEST_CASE( displaced_token_test2 ) {
    testToken("'quot 0xef \"stri\\ng\" null {", TKLBrace, 5);
    testToken("'quot 0xef \"stri\\ng\" null }", TKRBrace, 5);
    testToken("'quot 0xef \"stri\\ng\" null [", TKLBracket, 5);
    testToken("'quot 0xef \"stri\\ng\" null ]", TKRBracket, 5);
    testToken("'quot 0xef \"stri\\ng\" null (", TKLParen, 5);
    testToken("'quot 0xef \"stri\\ng\" null )", TKRParen, 5);
    testToken("'quot 0xef \"stri\\ng\" null ${", TKDollarBrace, 5);
    testToken("'quot 0xef \"stri\\ng\" null $[", TKDollarBracket, 5);
    testToken("'quot 0xef \"stri\\ng\" null $(", TKDollarParen, 5);
    testToken("'quot 0xef \"stri\\ng\" null $`", TKDollarBacktick, 5);
    testToken("'quot 0xef \"stri\\ng\" null '", TKQuote, 5);
    testToken("'quot 0xef \"stri\\ng\" null `", TKBacktick, 5);
    testToken("'quot 0xef \"stri\\ng\" null ,", TKComma, 5);
    testToken("'quot 0xef \"stri\\ng\" null ,@", TKCommaSplice, 5);
}

// TODO: test exceptions and I/O
