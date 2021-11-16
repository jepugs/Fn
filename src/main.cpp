#include "base.hpp"
#include "bytes.hpp"
//#include "compile.hpp"
//#include "ffi/builtin.hpp"
//#include "interpret.hpp"
//#include "parse.hpp"
//#include "scan.hpp"
#include "table.hpp"
//#include "values.hpp"
//#include "vm.hpp"

#include <filesystem>
#include <iostream>
#include <unistd.h>

using namespace fn;

namespace fs = std::filesystem;
using std::endl;

void show_usage() {
    std::cout <<
        "Usage: fn [options] [-e string | file] ...\n"
        "Description:\n"
        "  Fn programming language interpreter and repl.\n"
        "Options:\n"
        // "  -D dir      Set the working directory for the interpreter.\n"
        // "  --no-wd     Exclude the working directory from namespace searches.\n"
        // "  --no-home   Exclude the home directory from namespace searches.\n"
        // "  --sys-only  Equivalent to \"--no-wd --no-home\".\n"
        // "  -d            Output disassembled bytecode after interpreting\n"
        "  -e string     Evaluate the Fn expressions in the string,\n"
        "              printing the result. Multiple -e (options and filenames)\n"
        "              can be mixed, in which case they are evaluated in the\n"
        "              order provided.\n"
        "  -h            Show this help message and exit.\n"
        "  -i            Start a repl (after running provided strings and files)\n"
        "Running with no options starts a repl.\n"
        ;
}

int main(int argc, char** argv) {
    int opt;
    // this contains filenames and strings to evaluate. the first character of each entry indicates
    // whether it is a filename ('f') or a string ('s').
    vector<string> evals;
    // -d flag
    bool dis = false;
    // -i flag
    bool interact = false;

    // this depends on GNU getopt
    while ((opt = getopt(argc, argv, "-D:e:dhi")) != -1) {
        switch (opt) {
        // case 'd':
        //     dis = true;
        //     break;
        case 'e':
            evals.push_back("s" + string{optarg});
            break;
        case 'h':
            show_usage();
            return 0;
        case 'i':
            interact = true;
            break;
        case 1:
            // non-arguments are filenames
            evals.push_back("f" + string{optarg});
            break;
        default:
            std::cerr << "error: unrecognized option\n";
            return -1;
        }
    }

    if (interact && dis) {
        std::cerr << "error: both -d and -i were specified\n";
        return -1;
    }

    // check for no arguments
    if (optind == 1) {
        interact = true;
    }

    /*
    interpreter inter{};
    install_builtin(inter);
    // TODO: use proper namespaces
    value res;
    for (auto s : evals) {
        if (s[0] == 's') {
            res = inter.interpret_string(s.substr(1));
        } else {
            res = inter.interpret_file(s);
        }
    }

    // run the repl if necessary
    if (interact) {
        string line;
        while (!std::cin.eof()) {
            std::cout << "fn> ";
            std::getline(std::cin, line);
            res = inter.interpret_string(line);
            // print value
            std::cout << v_to_string(res, inter.get_symtab())
                      << endl;
        }
    } else {
        std::cout << v_to_string(res, inter.get_symtab())
                  << endl;
    }
    */

    return 0;
}

