#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "init.hpp"
#include "parse.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include <filesystem>
#include <iostream>
#include <unistd.h>

using namespace fn;
using namespace fn_bytes;

namespace fs = std::filesystem;
using std::endl;

void show_usage() {
    std::cout <<
        "Usage: fn [options] [-e string | file] ...\n"
        "Description:\n"
        "  Fn programming language interpreter and repl.\n"
        "Options:\n"
        "  -D dir      Set the working directory for the interpreter.\n"
        // "  --no-wd     Exclude the working directory from namespace searches.\n"
        // "  --no-home   Exclude the home directory from namespace searches.\n"
        // "  --sys-only  Equivalent to \"--no-wd --no-home\".\n"
        "  -d            Output disassembled bytecode instead of\n"
        "              executing. (Note: imported code will still be run, but\n"
        "              macros in source file will not be compiled.)\n"
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
    bool inter = false;

    string wd{fs::current_path().string()};
    
    // this depends on GNU getopt
    while ((opt = getopt(argc, argv, "-D:e:dhi")) != -1) {
        switch (opt) {
        case 'D':
            wd = string{optarg};
            break;
        case 'd':
            dis = true;
            break;
        case 'e':
            evals.push_back("s" + string{optarg});
            break;
        case 'h':
            show_usage();
            return 0;
        case 'i':
            inter = true;
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

    if (inter && dis) {
        std::cerr << "error: both -d and -i were specified\n";
        return -1;
    }

    // check for no arguments
    if (optind == 1) {
        inter = true;
    }

    virtual_machine vm{wd};
    init(&vm);
    for (auto s : evals) {
        if (s[0] == 's') {
            vm.set_wd(wd);
            if (!dis) {
                vm.interpret_string(s.substr(1));
            } else {
                vm.compile_string(s.substr(1));
            }
        } else {
            fs::path src{s.substr(1)};
            vm.set_wd(src.parent_path().string());
            if (!dis){
                vm.interpret_file(src.string());
            } else {
                vm.compile_file(s.substr(1));
            }
        }
    }

    // print out the last value
    if (!dis) {
        std::cout << v_to_string(vm.last_pop(),vm.get_bytecode().get_symbol_table()) << endl;
    } else {
        disassemble(vm.get_bytecode(), std::cout);
    }

    // run the repl if necessary
    if (inter) {
        vm.set_wd(wd);

        string line;
        while (!std::cin.eof()) {
            std::cout << "fn> ";
            std::getline(std::cin, line);
            vm.interpret_string(line, "repl");
            // print value
            std::cout << v_to_string(vm.last_pop(),
                                     vm.get_bytecode().get_symbol_table())
                      << endl;
        }
    }

    return 0;
}

