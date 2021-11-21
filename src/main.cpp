#include "base.hpp"
#include "bytes.hpp"
#include "ffi/builtin.hpp"
#include "interpret.hpp"
#include "table.hpp"
#include "values.hpp"

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

    
    interpreter inter{};
    auto ws = inter.get_alloc()->add_working_set();
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
        string buf;
        u32 pos = 0;
        bool still_reading = false;
        while (!std::cin.eof()) {
            string line;
            std::getline(std::cin, line);
            if (still_reading) {
                std::cout << " >> ";
                // getline doesn't append the '\n', so we add it back here to
                // put whitespace between the strings
                buf += '\n' + line;
            } else {
                std::cout << "fn> ";
                buf = line;
                // TODO: check for keywords for repl commands
                //if(line[0] == ':')
            }


            res = inter.partial_interpret_string(buf, &ws, &pos);

            // note that this doesn't account for trailing whitespace
            if (pos < buf.size() && buf != "\n") {
                buf = buf.substr(pos);
                still_reading = true;
            } else {
                buf = "";
                // print value
                std::cout << v_to_string(res, inter.get_symtab())
                          << endl;
                still_reading = false;
            }
        }
    } else {
        std::cout << v_to_string(res, inter.get_symtab())
                  << endl;
    }
    

    return 0;
}

