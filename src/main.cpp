#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "init.hpp"
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

void showUsage() {
    std::cout <<
        "Usage: fn [options] [-e string | file] ...\n"
        "Description:\n"
        "  fn programming language interpreter and repl.\n"
        "Options:\n"
        //"  -c            (UNIMPLEMENTED) compile files\n"
        "  -d            output disassembled bytecode instead of executing\n"
        "  -e string     evaluate the fn expressions in the string, printing the result.\n"
        "                  Multiple -e (options and filenames) can be mixed, in which\n"
        "                  case they will be evaluated in the order supplied.\n"
        "  -h            show this help message and exit\n"
        "  -i            start a repl (after running provided strings and files)\n"
        //"  -o file       (UNIMPLEMENTED) write the output of -d or -c to the file\n"
        ;
}

// TODO: Add current path parameter
void compileString(VM* vm, const string& str) {
    auto code = vm->getBytecode();
    std::istringstream in(str);
    fn_scan::Scanner sc(&in, "<cmdline>");
    Compiler c(fs::current_path(), code, &sc);
    c.compile();
}

// TODO: Add current path parameter
// returns -1 on failure
int compileFile(VM* vm, const string& filename) {
    auto code = vm->getBytecode();
    std::ifstream in(filename);
    if (!in.is_open()) {
        // TODO: might be better to make this an FNError so we can have a single toplevel handler
        perror(("error opening file " + filename).c_str());
        return -1;
    } else {
        fn_scan::Scanner sc(&in, filename);
        Compiler c(fs::current_path(), code, &sc);
        c.compile();
    }

    return 0;
}

int main(int argc, char** argv) {
    int opt;
    // this contains filenames and strings to evaluate. The first character of each entry indicates
    // whether it is a filename ('f') or a string ('s').
    vector<string> evals;
    // if true, then output disassembled bytecode rather instead of running
    bool dis = false;
    bool inter = false;
    
    // this depends on GNU getopt
    while ((opt = getopt(argc, argv, "-cde:hio:")) != -1) {
        switch (opt) {
        case 'c':
            std::cerr << "error: -c not yet implemented\n";
            return -1;
        case 'd':
            dis = true;
            break;
        case 'e':
            evals.push_back("s" + string(optarg));
            break;
        case 'h':
            showUsage();
            return 0;
        case 'i':
            inter = true;
            break;
        case 'o':
            std::cerr << "error: -o not yet implemented\n";
            return -1;
        case 1:
            // non-arguments are filenames
            evals.push_back("f" + string(optarg));
            break;
        default:
            std::cerr << "error: unrecognized option\n";
            return -1;
        }
    }

    if (inter && dis) {
        std::cerr << "error: cannot combine -d and -i\n";
    }

    // check for no arguments
    if (optind == 1) {
        inter = true;
    }


    VM vm;
    init(&vm);
    for (auto s : evals) {
        if (s[0] == 's') {
            compileString(&vm, s.substr(1));
        } else {
            if (compileFile(&vm, s.substr(1)) == -1) {
                // exit on error; error message should already be printed
                return -1;
            }
        }
    }

    if (dis) {
        // disassembly mode
        auto code = vm.getBytecode();
        disassemble(*code, std::cout);
        return 0;
    }

    // time to actually run the vm
    vm.execute();

    // FIXME: for now we print out the last value, but we probably really shouldn't
    std::cout << vToString(vm.lastPop(),vm.getBytecode()->getSymbols()) << endl;

    // do the repl if necessary
    if (inter) {
        string line;
        while (!std::cin.eof()) {
            std::cout << "fn> ";
            std::getline(std::cin, line);
            compileString(&vm, line);
            vm.execute();
            // print value
            std::cout << vToString(vm.lastPop(),vm.getBytecode()->getSymbols()) << endl;
        }
    }

    std::cout << "ip = " << vm.getIp() << endl;
    //std::cout << showValue(vm.getGlobal("y")) << endl;
    return 0;
}

