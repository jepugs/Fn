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
        "  -d            Output disassembled bytecode before interpreting each\n"
        "                 each expression.\n"
        "  -l            Print low-level intermediate representation before\n"
        "                 compiling each expression.\n"
        "  -e string     Evaluate the Fn expressions in the string,\n"
        "                 printing the result. Multiple -e options and\n"
        "                 filenames can be mixed, in which case they are\n"
        "                 evaluated in the order provided.\n"
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
    // -l flag
    bool show_llir = false;

    // this depends on GNU getopt
    while ((opt = getopt(argc, argv, "-D:e:dhil")) != -1) {
        switch (opt) {
        case 'd':
            dis = true;
            break;
        case 'l':
            show_llir = true;
            break;
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

    // check for no arguments
    if (evals.size() == 0) {
        interact = true;
    }
    
    interpreter inter{};
    auto ws = inter.get_alloc()->add_working_set();
    install_builtin(inter);
    inter.configure_logging(show_llir, dis);

    // TODO: use proper namespaces
    value res = V_NIL;
    bool err;
    for (auto s : evals) {
        err = false;
        if (s[0] == 's') {
            std::cout << "started string \n";
            res = inter.interpret_string(s.substr(1));
            std::cout << "finished string \n";
        } else {
            res = inter.interpret_file(s.substr(1), &err);
            if (err) {
                std::cout << "Error occurred while interpreting file "
                          << s.substr(1) << '\n';
                return -1;
            }
        }
    }
    
    // run the repl if necessary
    if (interact) {
        string buf;
        u32 pos = 0;
        bool still_reading = false;
        while (!std::cin.eof()) {
            string line;
            if (still_reading) {
                std::cout << " >> ";
                std::cout.flush();
                std::getline(std::cin, line);
                // getline doesn't append the '\n', so we add it back here to
                // put whitespace between the strings
                buf += '\n' + line;
            } else {
                std::cout << "fn> ";
                std::cout.flush();
                std::getline(std::cin, line);
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

