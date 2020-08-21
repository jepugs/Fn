#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "init.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include <iostream>
#include <vector>
#include <unistd.h>

using namespace std;
using namespace fn;
using namespace fn_bytes;


void showUsage() {
    cout <<
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
        //"  -i            (UNIMPLEMENTED) instead of exiting, start a repl after completing other actions\n"
        //"  -o file       (UNIMPLEMENTED) write the output of -d or -c to the file\n"
        ;
}

void compileString(VM* vm, const string& str) {
    auto code = vm->getBytecode();
    istringstream in(str);
    fn_scan::Scanner sc(&in, "<cmdline>");
    Compiler c(code, &sc);
    c.compile();
}

// returns -1 on failure
int compileFile(VM* vm, const string& filename) {
    auto code = vm->getBytecode();
    ifstream in(filename);
    if (!in.is_open()) {
        // TODO: might be better to make this an FNError so we can have a single toplevel handler
        perror(("error opening file " + filename).c_str());
        return -1;
    } else {
        fn_scan::Scanner sc(&in, filename);
        Compiler c(code, &sc);
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
    
    // this depends on GNU getopt
    while ((opt = getopt(argc, argv, "-cde:hio:")) != -1) {
        switch (opt) {
        case 'c':
            cerr << "error: -c not yet implemented\n";
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
            cerr << "error: -i not yet implemented\n";
            return -1;
        case 'o':
            cerr << "error: -o not yet implemented\n";
            return -1;
        case 1:
            // non-arguments are filenames
            evals.push_back("f" + string(optarg));
            break;
        default:
            cerr << "error: unrecognized option\n";
            return -1;
        }
    }

    // check for no arguments
    if (optind == 1) {
        cerr << "error: repl not yet implemented :(\n\n";
        showUsage();
        return -1;
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
        disassemble(*code, cout);
        return 0;
    }

    // time to actually run the vm
    vm.execute();
    // FIXME: for now we print out the last value, but we probably really shouldn't
    cout << showValue(vm.lastPop()) << endl;
    //cout << showValue(vm.getGlobal("y")) << endl;
    return 0;
}

