#include "base.hpp"
#include "builtin.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include "expand.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

using namespace fn;

namespace fs = std::filesystem;
using std::endl;

void show_usage() {
    std::cout <<
        "Usage: fn [options] [FILE | -]\n"
        "Description:\n"
        "  Fn programming language interpreter and REPL.\n"
        "Options/Arguments:\n"
        "  -h            Show this help message and exit.\n"
        "  -r            Start the REPL after finishing evaluation.\n"
        "  -d            Print disassembled bytecode after compiling.\n"
        "  -             Take input directly from STDIN.\n"
        "  FILE          Main file to interpret. Omitting this starts a REPL.\n"
        "Running with no options starts REPL in namespace fn/user/repl.\n"
        "When evaluating a file, the package and namespace are determined\n"
        "by the filename and package declaration, if present. Refer to\n"
        "the Fn manual for more information.\n"
        ;
}

// specifies where the Fn source code comes from
enum eval_mode {
    em_file,
    em_string,
    em_stdin,
    em_none
};

struct interpreter_options {
    // where to get source code
    eval_mode mode = em_none;
    // holds filename or string to evaluate depending on mode
    string src = "";
    // if true, show help and exit
    bool help = false;
    // whether to start a repl
    bool repl = false;
    // whether to print bytecode
    bool dis = false;
    // whether to print llir
    bool llir = false;

    // if true, the argument list was malformed and the other fields are not
    // guaranteed to be properly initialized
    bool err = false;
    string message = "";
};

// create an interpreter_options object based on CLI options. Returns false on
// malformed command line arguments.
void process_args(int argc, char** argv, interpreter_options* opt) {
    // process options first
    int i;
    for (i = 1; i < argc; ++i) {
        string s{argv[i]};
        if (s[0] == '-') {
            if (s[1] == '\0') {
                break;
            }
            for (u32 j = 1; j < s.size(); ++j) {
                switch(s[j]) {
                case 'r':
                    opt->repl = true;
                    break;
                case 'd':
                    opt->dis = true;
                    break;
                case 'h':
                    opt->help = true;
                    // no sense doing further processing at this point
                    return;
                case '\0':
                    opt->mode = em_stdin;
                    break;
                default:
                    opt->err = true;
                    opt->message = "Unrecognized option in"
                        + string{s[j]} + ".";
                    return;
                }
            }
        }
    }

    if (i == argc) {
        opt->mode = em_none;
        opt->repl = true;
        return;
    } else if (i == argc - 1) {
        opt->src = argv[i];
    } else {
        opt->err = true;
        opt->message = "Provide a single filename.";
    }

    if (opt->src == string{"-"}) {
        opt->mode = em_stdin;
    } else {
        opt->mode = em_file;
    }
}

int main(int argc, char** argv) {
    interpreter_options opt;
    process_args(argc, argv, &opt);
    if (opt.help) {
        show_usage();
        return 0;
    } else if (opt.err) {
        std::cout << "Error processing command line arguments:\n"
                  << opt.message << '\n';
        return -1;
    }

    auto S = init_istate();
    install_builtin(S);

    set_filename(S, "<stdin>");
    scanner sc{&std::cin};
    bool resumable;

    while (!sc.eof_skip_ws()) {
        auto form = parse_next_form(&sc, S, &resumable);
        if (S->err_happened) {
            std::cout << convert_fn_string(S->err_msg) << '\n';
            free_istate(S);
            return -1;
        }
        compile_form(S, form);
        free_ast_form(form);
        if (S->err_happened) {
            std::cout << "Error: " << convert_fn_string(S->err_msg) << '\n';
            print_stack_trace(S);
            free_istate(S);
            return -1;
        }
        if (opt.dis) {
            disassemble_top(S, true);
            print_top(S);
            pop(S);
        }
        call(S, 0);
        if (S->err_happened) {
            std::cout << "Error: " << convert_fn_string(S->err_msg) << '\n';
            print_stack_trace(S);
            free_istate(S);
            return -1;
        }
        print_top(S);
        pop(S);
    }
    free_istate(S);

    return 0;
}

