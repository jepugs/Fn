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
        "usage: fn [options] [-e string | file] ...\n"
        "description:\n"
        "  fn programming language interpreter and repl.\n"
        "options:\n"
        //"  -c            (u_ni_mp_le_me_nt_ed) compile files\n"
        "  -d            output disassembled bytecode instead of executing\n"
        "  -e string     evaluate the fn expressions in the string, printing the result.\n"
        "                  multiple -e (options and filenames) can be mixed, in which\n"
        "                  case they will be evaluated in the order supplied.\n"
        "  -h            show this help message and exit\n"
        "  -i            start a repl (after running provided strings and files)\n"
        //"  -o file       (u_ni_mp_le_me_nt_ed) write the output of -d or -c to the file\n"
        ;
}

// t_od_o: add current path parameter
void compile_string(virtual_machine* vm, const string& str) {
    auto code = vm->get_bytecode();
    std::istringstream in(str);
    fn_scan::scanner sc(&in, "<cmdline>");
    compiler c(fs::current_path(), code, &sc);
    c.compile();
}

void parse_string(const string& str) {
    std::istringstream in(str);
    fn_scan::scanner sc(&in, "<cmdline>");
    symbol_table symtab;
    fn_parse::ast_node* ast;
    try {
        ast = fn_parse::parse_node(&sc, &symtab);
        std::cout << ast->as_string(&symtab) << "\n";
    } catch(const fn_error& e) {
        std::cerr << e.what() << "\n";
        return;
    }
    delete ast;
}

// t_od_o: add current path parameter
// returns -1 on failure
int compile_file(virtual_machine* vm, const string& filename) {
    auto code = vm->get_bytecode();
    std::ifstream in(filename);
    if (!in.is_open()) {
        // t_od_o: might be better to make this an fn_error so we can have a single toplevel handler
        perror(("error opening file " + filename).c_str());
        return -1;
    } else {
        compiler c(fs::current_path(), code);
        c.compile_file(filename);
    }

    return 0;
}

int main(int argc, char** argv) {
    int opt;
    // this contains filenames and strings to evaluate. the first character of each entry indicates
    // whether it is a filename ('f') or a string ('s').
    vector<string> evals;
    // if true, then output disassembled bytecode rather instead of running
    bool dis = false;
    bool inter = false;
    
    // this depends on g_nu getopt
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
            show_usage();
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

    // ignore everything and just do some parsing
    for (auto s : evals) {
        if (s[0] == 's') {
            parse_string(s.substr(1));
        } else {
            // if (compile_file(&vm, s.substr(1)) == -1) {
            //     // exit on error; error message should already be printed
            //     return -1;
            // }
        }
    }


    // virtual_machine vm;
    // init(&vm);
    // for (auto s : evals) {
    //     if (s[0] == 's') {
    //         compile_string(&vm, s.substr(1));
    //     } else {
    //         if (compile_file(&vm, s.substr(1)) == -1) {
    //             // exit on error; error message should already be printed
    //             return -1;
    //         }
    //     }
    // }

    // if (dis) {
    //     // disassembly mode
    //     auto code = vm.get_bytecode();
    //     disassemble(*code, std::cout);
    //     return 0;
    // }

    // // time to actually run the vm
    // vm.execute();

    // // f_ix_me: for now we print out the last value, but we probably really shouldn't
    // std::cout << v_to_string(vm.last_pop(),vm.get_bytecode()->get_symbols()) << endl;

    // // do the repl if necessary
    // if (inter) {
    //     string line;
    //     while (!std::cin.eof()) {
    //         std::cout << "fn> ";
    //         std::getline(std::cin, line);
    //         compile_string(&vm, line);
    //         vm.execute();
    //         // print value
    //         std::cout << v_to_string(vm.last_pop(),vm.get_bytecode()->get_symbols()) << endl;
    //     }
    // }

    // vm.get_alloc()->print_status();
    //std::cout << "ip = " << vm.get_ip() << endl;
    //std::cout << show_value(vm.get_global("y")) << endl;
    return 0;
}

